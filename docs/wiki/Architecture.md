# Architecture — デュアルコア構成と tick パイプライン

## 全体像

統合ファームウェアの本体は `src/main.c` である (`AGENTS.md:28`)。PC→UART1→Pico→USB-HID/Classic-BT→Switch 1 という経路を取り、BLE は wake 取込・再生にのみ使う (`src/main.c:1-11`)。

有線起動では `wired_loop()` が回り、CYW43/BTstack を一切上げない (`src/main.c:1340-1342,1480-1482`)。無線起動では BTstack run loop + TinyUSB + CYW43 が上がり、1ms 周期の `poll_tick` が全体を駆動する (`src/main.c:1485-1489,1550-1552`)。

## Core1 の責務（UART 受信・パース専用）

- データ UART（UART1、既定 GP4/5、既定 1Mbps、8N1、フロー制御なし）の初期化を行う (`src/main.c:54-59,580-586`)
- UART1 RX を DMA で 16KB リングバッファ（`RING_BITS=14`、`RING_SIZE`）に流し込む (`src/main.c:64-66,589-597`)
- DMA の write ポインタ差分をポーリングで回収し、`parser_feed_buf` に渡す。IRQ は使わない (`src/main.c:683-703`)
- 受信フレームのコールバック `bcon_frame_cb` では STATE の live 適用・NEUTRAL の即時解放・その他フレームの inbox 退避・mutex 保護下の push のみを行う (`src/main.c:263-335`)
- STATE 以外の受信フレーム（CONFIG/HELLO/PING 等）は 8 スロットの inbox（`IB_N=8`）へ退避し、Core0 が dispatch する (`src/main.c:68-73,314-327`)。inbox が満杯なら `ib_drop` を数えて落とす (`src/main.c:317-319`)
- Flash・USB・BT・CYW43・printf 系の重い処理は使わない。Flash 保護のための lockout victim 初期化（`flash_safe_execute_core_init()`）だけは Core1 側で呼ぶ (`src/main.c:520`)
- 起動時に selftest（STATE→NEUTRAL→PING の 3 フレーム合成＋パース確認）を行い、`boot_code`/`boot_detail` を残す (`src/main.c:377-421,534-535`)。Core0 は起動時に Core1 の boot 完了を最大 2 秒待つ (`src/main.c:1463-1471`)
- UART オーバーラン（`UARTRSR_OE`）と poll 反復回数（`iters`）を数え、4096 反復ごとに共有状態へ反映する (`src/main.c:709-719`)。オーバーランがあると STATUS bit4 が立つ (`src/main.c:752`)

## Core0 の責務（BTstack・USB・ tick 駆動）

- TLV 永続層の初期化、保存値の読込（色・host・取込・有線既定）、Core1 起動、USB 初期化、無線/有線分岐、BTstack・CYW43・SDP・HID の初期化を順に行う (`src/main.c:1369-1571`)
- 無線起動では 3 本の周期タイマを登録する。`stats_timer` 1 秒（生存表示 BCON）、`usb_timer` 1ms（`poll_tick` 駆動）、`empty_timer`（空レポート要求、周期は `probe_send_interval_ms()`）である (`src/main.c:1546-1556`)。再接続タイマ `reconnect_timer` も登録する (`src/main.c:1558-1559`)
- 生存 WDT（2 秒）は `usb_timer` による `poll_tick` の末尾 `watchdog_update()` で給餌する (`src/main.c:1046,1569-1570`)。WDT 起因の再起動は起動時に読み取り、STATUS bit3 に反映する (`src/main.c:1390,1422-1424`)

## dispatch の純粋性（必ず守る分離）

- `src/proto/`（`protocol.c`、`pack.c`、`spi.c`、`dispatch.c`）は Pico/BTstack 非依存でホスト試験可能である (`AGENTS.md:29`)
- `dispatch.h` は純粋である。HW 副作用を持たず、効果 `FX_*` と送信箱 `ACT_*` を返すだけである (`src/proto/dispatch.h:1-4`)
- CONFIG 受理時の効果は `FX_CAPTURE_START`、`FX_BEACON_START`、`FX_COLOR_SET`、`FX_KEY_DELETE`、`FX_WIRED_MODE` の列挙で返す (`src/proto/dispatch.h:33-44`)
- Pico→PC 送信箱の動作は `ACT_SEND_STATUS`、`ACT_SEND_PONG`、`ACT_SEND_HELLO_ACK`、`ACT_SEND_PLAYER_INFO` の列挙で返す (`src/proto/dispatch.h:17-24`)。outbox は 8 スロット（`V3_OB_N=8`）で、溢れは `ob_dropped` に数える (`src/proto/dispatch.h:31,79`, `src/proto/dispatch.c:15-23`)
- すべての HW 副作用（Flash/TLV/BT/UART-TX）は `main.c` 側にある。`exec_fx` が FX を実行し (`src/main.c:767-836`)、`flush_outbox` が ACT を UART 送信に変える (`src/main.c:839-866`)

## tick パイプライン（`poll_tick`、1ms）

実行順は固定である (`src/main.c:909-1060`)：

1. 共有状態の複写と telemetry 供給。`cap_valid`、`player_lamp`、`player_flags`、`player_valid` を取り込み、`v3_player_tick` で変化検出する (`src/main.c:919-924`)。変化時のみ `ACT_SEND_PLAYER_INFO` が積まれる (`src/proto/dispatch.c:127-138`)
2. inbox drain。live index で空になるまで回し、各フレームを `v3_on_frame` に渡す。STATE/NEUTRAL の live 適用は Core1 高速路が担うため、ここでは CONFIG 等の fx/ob のみ扱う (`src/main.c:985-987`)
3. `exec_fx` で FX を実行する (`src/main.c:989`)
4. `flush_outbox` で outbox を UART 送信に変える (`src/main.c:991`)。STATUS は新鮮な HW 状態で組み立て (`src/main.c:738-763`)、PONG は要求 SEQ をエコーし (`src/main.c:844-845`)、HELLO_ACK は採用版・FW 版・RESULT の 4B を返し (`src/main.c:851-857`)、PLAYER_INFO は最終送出値の 2B を返す (`src/main.c:858-862`)
5. 共有 u32→3B pack。`ctrl_pack_btn3` でボタン 3B を作り、`bcon_*` と `probe_*` の両方へ配る。スティックは 8bit 値をそのまま保持する (`src/main.c:994-1008`)
6. STATE 到着追跡と timeout-neutral。200ms 無受信で全解放し、STATUS bit2 を立てる (`src/main.c:61,1022-1042`)
7. `usb_wired_task` と `link_poll` を回し (`src/main.c:1044-1045`)、`watchdog_update` で給餌する (`src/main.c:1046`)。`WIRED_MODE` 切替時は 500ms 後に自発再起動する (`src/main.c:1054-1058`)

## 有線/無線の分岐

- 有線起動（`s_wired==true`）では `wired_loop()` に入り、1ms tick＋1 秒 log を回すだけで戻らない。CYW43 給電中は Switch 2 ドックが USB 列挙しない実測があるため、無線一式を上げない (`src/main.c:1340-1342,1480-1482`)
- 無線起動では CYW43 初期化後に USB pump、MAC 再設定、GAP/クラス/名前/SNIFF ポリシー設定、SDP 登録、HID 初期化、BD_ADDR 設定の順で上げる (`src/main.c:1498-1538`)
- `WIRED_MODE`（`0x34`）は Flash 保存され、起動時に復元される。未保存時はビルド既定 `WIRED_DEFAULT`（既定 1=有線）を使う (`src/main.c:1421`)
- 切替は再起動で適用する。CYW43/BTstack の有無が起動時確定のためであり、受理後約 500ms で自発再起動する (`src/main.c:804-808`)

## してはいけないこと（アーキテクチャ制約）

- `tusb.h` と `btstack.h` を同じ翻訳単位に含めない。`hid_report_type_t` が二重定義になる (`AGENTS.md:30`)
- Core1 に printf/BT/CYW43/Flash を持ち込まない。Core1 が触るのは DMA リング・parser・短時間 mutex のみである (`AGENTS.md:28`)
- `dispatch.h` に HW 副作用を足さない。副作用は `main.c` に集約する (`AGENTS.md:29`)
- BT コールバック内に新規の Flash 書込経路を足さない。BT 動作中の Flash 書込は約 2 秒後に WDT 死を起こす（9/9 再現）。詳細は [Flash-and-Persistence](Flash-and-Persistence.md) と [Troubleshooting](Troubleshooting.md) を見ること
