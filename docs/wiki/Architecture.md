# Architecture: デュアルコア構成と tick パイプライン

## 全体像

統合ファームウェアの本体は `src/main.c` である (`AGENTS.md:28`)。PC→UART1→Pico→USB-HID/Classic-BT→Switch 1 という経路を取り、BLE は wake 取込・再生にのみ使う (`src/main.c:1-11`)。

有線起動では `wired_loop()` が回り、CYW43/BTstack を一切上げない (`src/main.c:1340-1342,1480-1482`)。無線起動では BTstack run loop + TinyUSB + CYW43 が上がり、1ms 周期の `poll_tick` が全体を駆動する (`src/main.c:1485-1489,1550-1552`)。

USB は起動時に profile を選ぶ。有線起動は HID-only（Switch 接続用）、無線起動は CDC-only（PC 専用、data と log の 2 系統）で、単一 UF2 のまま `usb_cdc_set_profile(s_wired)` を `tud_init` 前に呼んで確定する。切替は再起動適用である (`src/usb/usb_cdc.c`, `src/main.c`)。無線 profile の USB ID は `2E8A:000A` で、有線 HID の `057E:2009` とは別である (`src/usb/usb_descriptors.c`)。

## Core1 の責務（UART 受信・パース専用）

前章で全体像が済み、この節は受信側が何を受け持つかに答える。

- データ UART（UART1、既定 GP4/5、既定 1Mbps、8N1、フロー制御なし）を初期化する (`src/main.c:54-59,580-586`)
- UART1 RX を DMA で取り込み、16KB リングバッファ（`RING_BITS=14`、`RING_SIZE`）へ流し込む (`src/main.c:64-66,589-597`)
- DMA の write ポインタ差分をポーリングで回収し、`parser_feed_buf` に渡す。IRQ は使わない (`src/main.c:683-703`)
- 受信フレームのコールバック `bcon_frame_cb` でやるのは、STATE の live 適用、NEUTRAL の即時解放、その他フレームの inbox 退避と mutex 保護下の push だけである (`src/main.c` `bcon_frame_cb`)
- STATE/NEUTRAL/IMU_SAMPLE 以外の受信フレーム（CONFIG/HELLO/PING 等）は 8 スロットの inbox（`IB_N=8`）へ退避し、Core0 が dispatch する (`src/main.c`)。inbox が満杯なら `ib_drop` を数えて落とす
- `T_IMU_SAMPLE`（LEN12）は live 適用で、共有 `imu_hist`/`imu_frames` を更新する（inbox には積まない）。UART 路は baud 確定＋state_accept、CDC 路は state_accept のみを条件とする (`src/main.c` `bcon_frame_cb`, `cdc_frame_cb`)。
- Flash・USB・BT・CYW43・printf 系の重い処理は使わない。Flash 保護のための lockout victim 初期化（`flash_safe_execute_core_init()`）だけは Core1 側で呼ぶ (`src/main.c:520`)
- 起動時に selftest（STATE→NEUTRAL→PING の 3 フレーム合成＋パース確認）を行い、`boot_code`/`boot_detail` を残す (`src/main.c:377-421,534-535`)。Core0 は起動時に Core1 の boot 完了を最大 2 秒待つ (`src/main.c:1463-1471`)
- UART オーバーラン（`UARTRSR_OE`）と poll 反復回数（`iters`）を数え、4096 反復ごとに共有状態へ反映する (`src/main.c:709-719`)。オーバーランがあると STATUS bit4 が立つ (`src/main.c:752`)

## Core0 の責務（BTstack・USB・tick 駆動）

前章で受信側の切り分けが済み、この節は駆動側が何を受け持つかに答える。

- TLV 永続層の初期化、保存値の読込（色・host・取込・有線既定）、Core1 起動、USB 初期化、無線/有線分岐、BTstack・CYW43・SDP・HID の初期化を順に行う (`src/main.c:1369-1571`)
- 無線起動では 3 本の周期タイマを登録する。内訳は `stats_timer` が 1 秒（生存表示 BCON）、`usb_timer` が 1ms（`poll_tick` 駆動）、`empty_timer` が空レポート要求（周期は `probe_send_interval_ms()`）である (`src/main.c:1546-1556`)。再接続タイマ `reconnect_timer` も登録する (`src/main.c:1558-1559`)
- 生存 WDT（2 秒）は、`usb_timer` が回す `poll_tick` の末尾にある `watchdog_update()` で給餌する (`src/main.c:1046,1569-1570`)。WDT 起因の再起動は起動時に読み取り、STATUS bit3 に反映する (`src/main.c:1390,1422-1424`)
- 無線起動時は USB を CDC-only にする（`usb_cdc_set_profile(false)`）。CDC は interface 0/1=data（v3 frame）と interface 2/3=log（`probe_line` の ASCII 写し）の 2 系統で、data 路は `cdc_poll_feed` が汲み、応答は要求元輸送（UART/CDC）へ返す。周期 STATUS は CDC 接続中なら CDC、そうでなければ UART (`src/main.c` `cdc_poll_feed`, `cdc_tx_frame`, `src/usb/usb_cdc.h`)
- `IMU_SAMPLE` の最新値と3スロットを `bcon_imu*` へ転記し、100ms/500ms の timeout を適用する (`src/main.c` `poll_tick`, `IMU_GYRO_ZERO_MS`/`IMU_STATIC_MS`)
- `EMULATE_MODE`（0x38）は Flash `BCEM` に保存し、起動時に `store_emulate_load_def` で復元する。role は起動時1回だけ確定し、USB 応答（0x02 dev_type・0x30・SPI）・BT GAP名/MAC・pack に効く。切替は約500ms後の再起動で適用する (`src/bt/store.c`, `src/bt/personality.c`, `src/main.c`)

## dispatch の純粋性（必ず守る分離）

前章で両コアの分担が済み、この節は分離をどう守るかに答える。

- `src/proto/`（`protocol.c`、`pack.c`、`spi.c`、`dispatch.c`）は Pico/BTstack 非依存で、ホスト試験が可能である (`AGENTS.md:29`)
- `dispatch.h` は純粋である。HW 副作用を持たず、効果 `FX_*` と送信箱 `ACT_*` を返すだけである (`src/proto/dispatch.h:1-4`)
- CONFIG 受理時の効果は `FX_CAPTURE_START`、`FX_BEACON_START`、`FX_COLOR_SET`、`FX_KEY_DELETE`、`FX_WIRED_MODE`、`FX_BAUD_SET`、`FX_BOOTSEL`、`FX_EMULATE_MODE`、`FX_RECONNECT` の列挙で返す (`src/proto/dispatch.h`)
- Pico→PC 送信箱の動作は `ACT_SEND_STATUS`、`ACT_SEND_PONG`、`ACT_SEND_HELLO_ACK`、`ACT_SEND_PLAYER_INFO`、`ACT_SEND_RUMBLE`、`ACT_SEND_COLOR_INFO` の列挙で返す (`src/proto/dispatch.h`)
- すべての HW 副作用（Flash/TLV/BT/UART-TX）は `main.c` 側にある。`exec_fx` が FX を実行し (`src/main.c:767-836`)、`flush_outbox` が ACT を UART 送信に変える (`src/main.c:839-866`)

## tick パイプライン（`poll_tick`、1ms）

前章で分離の約束が済み、この節は1ms の実行順に答える。

実行順は固定である (`poll_tick`)：

1. CDC pump。`cdc_poll_feed` が `usb_cdc_task_poll` で最大64Bを汲み、Core0-local parser に投入する（UARTのCore1 parserとは別。baud hunt非接触）。
2. 共有状態の複写と telemetry 供給。`cap_valid`、`player_lamp`、`player_flags`、`player_valid` を取り込み、`v3_player_tick` で変化を検出する。PLAYER_INFO flags は bit0=IMU、bit1=振動、bit2=cap_saved、bit3-4=gyroレンジ、bit5-6=accelレンジ、bit7=0x41受信済み。
3. inbox drain。live index で空になるまで回し、各フレームを `v3_on_frame` に渡す。STATE/NEUTRAL の live 適用は Core1 高速路（UART）または `cdc_frame_cb`（CDC）が担う。IMU_SAMPLE も live 適用で、inbox には積まない。
4. 各 frame の dispatch 直後に `exec_fx` を実行し、その frame 由来の outbox 分だけ要求元輸送へ送る（同一tickにCONFIGが2つ来てもfxを上書きしない）。
5. `flush_outbox` で残りの outbox を送る。
6. 共有 u32→3B pack。`ctrl_pack_btn3` でボタン3Bを作り、`bcon_*` と `probe_*` へ配る。role別の変換は BT 側 `joy_pack_btn3` / USB 側 `usb_role_pack_btn3`。
7. IMU転記＋timeout。`imu_frames` の変化で最新値と3スロットを更新し、100msでgyro→0、500msで静止値（0,0,0x1000,0,0,0）。STATEのtimeout-neutralとは独立。
8. STATE到着追跡と timeout-neutral（200ms）。`usb_wired_task`・`link_poll`・`watchdog_update` を回す。`WIRED_MODE`/`EMULATE_MODE` 切替は約500ms後に自発再起動する。

## 有線/無線は起動時に確定し、切替は再起動で適用する

- 有線起動（`s_wired==true`）では `wired_loop()` に入って 1ms tick と 1 秒 log を回し続け、戻らない。CYW43 給電中は Switch 2 ドックが USB 列挙しない実測があるため、無線一式を上げない (`src/main.c:1340-1342,1480-1482`)
- 無線起動では CYW43 初期化後に USB pump、MAC 再設定、GAP/クラス/名前/SNIFF ポリシー設定、SDP 登録、HID 初期化、BD_ADDR 設定の順で上げる (`src/main.c:1498-1538`)
- `WIRED_MODE`（`0x34`）は Flash 保存され、起動時に復元される。未保存時はビルド既定 `WIRED_DEFAULT`（既定 1=有線）を使う (`src/main.c:1421`)
- 切替は再起動で適用する。CYW43/BTstack を上げるかどうかは起動時に確定するためで、受理後約 500ms で自発再起動する (`src/main.c:804-808`)
- `EMULATE_MODE`（0x38）も Flash 保存・起動時復元で、role は USB 応答（0x02 dev_type・0x30・SPI）・BT GAP名/MAC・pack に効く。切替は `WIRED_MODE` と同型で約500ms後に自発再起動する (`src/main.c` `FX_EMULATE_MODE`, `src/bt/personality.c`)

## してはいけないこと（アーキテクチャ制約）

- `tusb.h` と `btstack.h` を同じ翻訳単位に含めない。`hid_report_type_t` が二重定義になる (`AGENTS.md:30`)
- Core1 に printf/BT/CYW43/Flash を持ち込まない。Core1 が触るのは DMA リング・parser・短時間 mutex のみである (`AGENTS.md:28`)
- `dispatch.h` に HW 副作用を足さない。副作用は `main.c` に集約する (`AGENTS.md:29`)
- USB profile は HID と CDC の排他である。無線起動時に Switch へ USB を挿さない（CDC-only のため列挙しない）。有線起動では CDC は使えない (`src/usb/usb_descriptors.c`, `src/usb/usb_cdc.c`)
- BT コールバック内に新規の Flash 書込経路を足さない。BT 動作中の Flash 書込は約 2 秒後に WDT 死を起こす（9/9 再現）。詳細は [Flash-and-Persistence](Flash-and-Persistence.md) と [Troubleshooting](Troubleshooting.md) を見ること
