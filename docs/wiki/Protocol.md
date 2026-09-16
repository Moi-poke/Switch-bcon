# Protocol — PC⇔Pico バイナリプロトコル

SSOT は `spec/protocol_v3.md` である。ただし現ツリーは v4 移行中のため、本ページは「仕様書 v3 の記述＋コード上の v4 差分」を分けて記録する。仕様とコードが衝突した場合は仕様 + `src/proto/*` を正とする (`AGENTS.md:3`)。

> 🚧 In-progress: コードは `PROTO_VER=0x04` だが (`src/proto/protocol.h:11`)、仕様書の表題はまだ `(v3 / PROTO_VER=3)` のままである (`spec/protocol_v3.md:1`)。v4 改訂（§4 表、`§5.7` 予約解除、新 `§5.8`、改訂履歴）は Plan A Task 9 の未着手項目である (`docs/superpowers/plans/2026-09-15-plan-a-telemetry-v4.md:384-404`)。

## フレーム構成

合計は `N + 5`（`N`=ペイロード長、上限 32）である (`spec/protocol_v3.md:38`)：

| Offset | Field | Size | 説明 |
|---|---|---|---|
| 0 | SYNC | 1 | 固定 `0xAB` (`src/proto/protocol.h:12`) |
| 1 | TYPE | 1 | 種別（下表） |
| 2 | LEN | 1 | ペイロード長。既知型は正確長を強制 (`src/proto/protocol.c:83-90`) |
| 3 | PAYLOAD | N | TYPE 依存 |
| 3+N | SEQ | 1 | 方向独立・mod256。送信ごとに +1 (`spec/protocol_v3.md:46`) |
| 4+N | CRC8 | 1 | CRC-8/SMBUS、対象 TYPE..SEQ（SYNC 除外）(`spec/protocol_v3.md:48`) |

正準生成器は `frame_build` である。`out` に全フレームを書き、合計長を返す。`len > 32` は 0 を返す (`src/proto/protocol.c:134-145`)。Pico→PC 方向 SEQ は `g_tx_seq` で方向独立に数える (`src/main.c:106,351-358`)。PC 送信側も方向別 SEQ を守り、1 フレーム 1 `write()` で送る（`src/poc_dualcore/poc_send.py:17,331-334`）。

原則は固定長バイナリ、SYNC+CRC+スライディング再同期、全状態送信、輸送非依存の論理 u32、双方向・方向独立 SEQ、UART バイナリ専用（ASCII なし）である (`spec/protocol_v3.md:18-23`)。

## CRC8 と受信ステートマシン

- CRC-8/SMBUS（poly `0x07`、init `0x00`、refin/refout なし、xorout `0x00`）。検査値 `crc8("123456789")=0xF4` (`spec/protocol_v3.md:158-159`)。実装はテーブル駆動である (`src/proto/protocol.c:5-29`)
- SYNC はペイロード中にも出現し得る。最終判定は必ず CRC で行う (`spec/protocol_v3.md:50`)
- 蓄積バッファ上で反復処理する。先頭 `0xAB` 整列→LEN 検証（既知型は正確長）→CRC 検証の順であり、不一致なら ERR 加算＋先頭 1B 前進（フレーム全体を捨てない）(`spec/protocol_v3.md:163`, `src/proto/protocol.c:73-101`)
- 未知型は 32B 上限で可変スキップする（前方互換）(`spec/protocol_v3.md:51`, `src/proto/protocol.c:83-85`)
- SEQ 欠番は mod256 で検出する。`expect=(last+1)&0xFF` で、不一致は欠落イベントとして 1 加算する（欠落数ではない）。初回フレームは計数しない (`spec/protocol_v3.md:52`, `src/proto/protocol.c:103-114`)
- バッファ満杯時は最古 1B 破棄＋ERR 加算する (`spec/protocol_v3.md:163`, `src/proto/protocol.c:121-128`)

## フレーム種別表（v3 仕様 + v4 差分）

仕様書の v3 表 (`spec/protocol_v3.md:56-71`) に v4 差分を注記したものである：

| TYPE | 名称 | 方向 | LEN | 用途 |
|---|---|---|---|---|
| `0x01` | STATE | PC→Pico | 8 | 全状態（ホットパス） |
| `0x02` | NEUTRAL | PC→Pico | 0 | 全入力解放（安全停止） |
| `0x03` | PING | PC→Pico | 0 | 生存確認/RTT |
| `0x10` | HELLO | PC→Pico | 2 | 版通知・FLAGS。v4 のみ受付（下記） |
| `0x11` | HELLO_ACK | Pico→PC | 4 | 版応答・RESULT |
| `0x20` | STATUS | Pico→PC | 7 | 状態・統計・errcode |
| `0x21` | PONG | Pico→PC | 1 | PING 自身 seq をエコー |
| `0x22` | RUMBLE | Pico→PC | 2 | 振動振幅。v3 仕様では予約・未送出 (`spec/protocol_v3.md:65`)。コードも現状は送出しない（下記） |
| `0x23` | PLAYER_INFO | Pico→PC | 2 | v4 追加。ランプ＋フラグ。dispatch+配線済み、HW 裏取り待ち |
| `0x30` | CAPTURE_START | PC→Pico | 1 | wake 取込開始・秒数 1-60 |
| `0x31` | BEACON_START | PC→Pico | 0 | wake 再生（約 1.5s） |
| `0x32` | COLOR_SET | PC→Pico | 12 | 本体色 RGB×4 |
| `0x33` | KEY_DELETE | PC→Pico | 0 | Classic リンク鍵全削除 |
| `0x34` | WIRED_MODE | PC→Pico | 1 | 0=無線・1=有線（Flash 保存） |
| `0x35` | STATUS_REQ | PC→Pico | 0 | STATUS 即時返送要求（+ PLAYER_INFO 付随） |

`proto_expected_len` が既知型の正確長を一元管理する。`T_PLAYER_INFO` は `return 2` が追加済みであり、未知 `0x24` は `-1` のままである (`src/proto/protocol.c:31-50`, `tests/host/test_config.c:160-164`)。

> 🚧 In-progress: `T_RUMBLE` の LEN2 予約解除・送出開始、`T_PLAYER_INFO` の仕様書追記、`0x36 BAUD_SET` / `0x37 PERSONALITY_SET` は未実装である。設計書の新フレーム一覧が定義のみ先行している (`docs/superpowers/specs/2026-09-15-uart-features-design.md:16-23`)。CONFIG 拒否 ERRCODE の延長規則（`0x10+TYPE 下位`→`0x36` は `0x16`、`0x37` は `0x17`）も同設計書の定義段階である。

## STATE（`0x01`、LEN8）

`BTN` u32-LE（VIIPER 互換ビット順、22bit 使用・残り予約 0）+ スティック 4B（LX LY RX RY、0-255、`0x80` 中央）である (`spec/protocol_v3.md:77-84`)。BTN ビット割当は B/A/Y/X/R/ZR/Plus/R 押込/十字/L/ZL/Minus/L 押込/Home/Capture/GR/GL/C/Headset の順で bit0-21 を使い、bit22-31 は予約である (`src/proto/protocol.h:53-76`, `spec/protocol_v3.md:88-101`)。

約束事は次の通り。十字キーはボタン扱い（斜めは 2bit 同時）で HAT フィールドなし。予約 bit は 0 固定で送信し、受信側は無視する（拒否しない）。スティック Y 反転・12bit 化等の座標変換は PC 送信ラッパ 1 箇所に集約し、Pico は 8bit 値をそのまま保持して輸送 pack 時に 12bit 化する (`spec/protocol_v3.md:103-105`, `spec/protocol_v3.md:226-229`)。

線路 u32 は輸送非依存の論理値であり、USB/BT への写像差は Pico 側 pack 関数（`ctrl_pack_btn3`、`pack_stick_12bit`）で吸収する (`spec/protocol_v3.md:186-189`)。pack 関数の実体は `src/proto/pack.c:4` にある。BT 3B と USB `0x30` ボタン 3B は任天堂が同一順序のため両表は同値であり、実装は単一 pack 関数を共有する (`spec/protocol_v3.md:188-189`)。

## HELLO（v4 のみ）/ HELLO_ACK

- HELLO ペイロードは `[0]=PROTO_VER（要求版）、[1]=FLAGS（bit0=STATUS 自動送信要求、他予約 0）` である (`spec/protocol_v3.md:117`)
- 現コードは v4 のみ受け付ける。`ver == PROTO_VER`（=4）なら `RESULT_OK` + `state_accept=true`、それ以外は `RESULT_VER_UNSUPPORTED` + `state_accept=false` である。`DOWNGRADED` 分岐は削除済みである (`src/proto/dispatch.c:52-58`)。`DOWNGRADED` 列挙値自体は `protocol.h` に残置されているが無害な残骸であり、計画書も削除不要としている (`src/proto/protocol.h:36-40`, `docs/superpowers/plans/2026-09-15-plan-a-telemetry-v4.md:79`)
- HELLO_ACK ペイロードは `[0]=採用版、[1]=FW_MAJOR、[2]=FW_MINOR、[3]=RESULT` である (`spec/protocol_v3.md:118`)。送信は `flush_outbox` が行う (`src/main.c:446-452`)。FW 版は `FW_MAJOR=0`、`FW_MINOR=1` である (`src/proto/dispatch.h:13-14`)
- v3 HELLO は UNSUPPORTED になる。STATE は拒否されるが NEUTRAL は安全停止として通す (`src/proto/dispatch.c:34-39`, `tests/host/test_config.c:36-48`)
- HELLO なし既定では STATE を受け付ける（sweep/PoC 互換のため `state_accept` 初期値は true）(`src/proto/dispatch.c:6-12`, `tests/host/test_config.c:50-56`)
- PC 送信側は版バイト 4 で送る (`src/poc_dualcore/poc_send.py:66-68`)。`--hello` は HELLO_ACK＋自動 STATUS を確認する (`src/poc_dualcore/poc_send.py:104-122`)

## STATUS（`0x20`、LEN7）/ ERRCODE

組立は純粋関数 `v3_pack_status` が担う。`flags/last_seq/err_crc(LE16)/err_drop(LE16)/errcode` の順である (`src/proto/dispatch.c:116-125`, `spec/protocol_v3.md:125-133`)。送信側 `send_status` は新鮮な HW 状態で flags を作る。USB mounted、Switch ready、timeout-neutral、WDT recovered、UART overrun、wired mode、BT connected、RUMBLE 受信ありの各 bit である (`src/main.c:361-386`)。

各 bit の対応は `ST_USB_MOUNTED`、`ST_SWITCH_READY`、`ST_TIMEOUT_NEUTRAL`、`ST_WDT_RECOVERED`、`ST_UART_OVERRUN`、`ST_WIRED_MODE`、`ST_BT_CONNECTED`、`ST_RUMBLE_SEEN` である (`src/proto/protocol.h:81-90`)。bit7（前回 STATUS 以降の振動受信あり）は v3 仕様書にはなく、未コミット差分で追加されたものである。受信有無のみを示し、振幅値は含まない。

ERRCODE は `0x00` 正常／`0x01` LEN 不正／`0x02` CRC 不一致／`0x03` SEQ 欠番／`0x04` UNSUPPORTED 版／`0x05` UART overrun／`0x06` parser overflow／`0x10-0x1F` CONFIG 拒否（`0x10+TYPE 下位`）である (`spec/protocol_v3.md:135`)。CONFIG 拒否コードは `cfg_err` が作る (`src/proto/dispatch.c:24-27`)。周期送信時の errcode は直近エラーを保持し、正常復帰後に 0 へ戻す (`spec/protocol_v3.md:136`, `src/main.c:527-529`)。

`STATUS_REQ` は STATUS 即時返送に加え、PLAYER_INFO を付随送出する（outbox に両方積む）(`src/proto/dispatch.c:94-97`, `tests/host/test_config.c:120-132`)。即時 STATUS の errcode は `0x00` 扱いである (`spec/protocol_v3.md:150`)。

## PLAYER_INFO（`0x23`、LEN2）— dispatch+配線済み、HW 裏取り待ち

ペイロードは `[0]=player lamp byte（SUB `0x30` report[10] の写し）、[1]=flags（bit0=IMU on、bit1=vibration on）` である (`docs/superpowers/specs/2026-09-15-uart-features-design.md:19`)。

実装の到達点は次の通り。`T_PLAYER_INFO=0x23` 列挙と LEN2 (`src/proto/protocol.h:28`, `src/proto/protocol.c:41`)、送信箱 `ACT_SEND_PLAYER_INFO` (`src/proto/dispatch.h:22`)、セッション欄（`player_lamp/player_flags/player_valid/player_sent_*/player_ever_sent`）(`src/proto/dispatch.h:58-61`)、変化検出 `v3_player_tick`（初回は無条件送出、以後は変化時のみ）(`src/proto/dispatch.c:103-114`)、`STATUS_REQ` 付随 (`src/proto/dispatch.c:94-97`)、hid 側の `probe_player_id`/`probe_player_seen`/`probe_imu_enabled`/`probe_vibration_enabled` 取得 (`src/bt/hid.c:328-366`, `src/bt/hid.h:36-39`)、tick 供給＋flush 送信 (`src/main.c:474-479,453-458`)、ホスト試験 (`tests/host/test_config.c:160-191`)。

> 🚧 In-progress: HW 裏取りが未了である。`STATUS_REQ` 応答に `PLAYER_INFO (0x23, LEN2)` が含まれること、`[0]` が Switch のプレイヤーランプ表示と一致することの確認は HW バッチ手順の項目である (`docs/history/2026-09-15-wdt/hw-batch-2026-09-15.md:33-35`)。

## RUMBLE（`0x22`、LEN2）— 現状は計数のみ、振幅転送は駐車中

v3 仕様では予約であり送出しない。Switch 出力受信パーサの受け口のみ確保し、ACK 返送＋破棄する (`spec/protocol_v3.md:154`)。コードも現状はその通りで、BT `0x10` 受信は初回のみ log し、カウンタ `bcon_bt_rumble_n` を増やすだけである (`src/bt/hid.c:424-431`)。所有元は `main.c` であり (`src/bt/hid.h:60-62`, `src/main.c:101-102`)、STATUS bit7 の源になる (`src/main.c:378-382`)。

> 🚧 In-progress: 振幅復号〜送出の連鎖（純粋復号器 `rumble.c`、intake 蓄積、`ACT_SEND_RUMBLE`、変化時のみ送出）は設計・計画済みだが、HW 振動キャプチャ待ちで駐車中である。復号式は捏造禁止であり、実ログの非ゼロ `A2 10` 行が 1 行以上必要である (`docs/superpowers/specs/2026-09-15-uart-features-design.md:28-31`, `docs/superpowers/plans/2026-09-15-plan-a-telemetry-v4.md:98-116`, `docs/history/2026-09-15-wdt/hw-batch-2026-09-15.md:20-24`)。`RUMBLE` を「送出中」と読めるのは HW 確認の後である。

## CONFIG（`0x30-0x35`）

- `CAPTURE_START`：`[0]=` 秒数 1-60。範囲外は拒否（ERRCODE `0x10`）(`spec/protocol_v3.md:140`, `src/proto/dispatch.c:62-71`, `tests/host/test_config.c:66-79`)
- `BEACON_START`：LEN0。未保存時は拒否（`0x11`）。保存有無は `cap_valid` で判定する (`spec/protocol_v3.md:141`, `src/proto/dispatch.c:72-75`, `tests/host/test_config.c:81-89`)
- `COLOR_SET`：`[0..11]=RGB×4`（本体・ボタン・左・右）。有線中は再列挙して読み直させる (`spec/protocol_v3.md:142`, `src/proto/dispatch.c:76-80`, `tests/host/test_config.c:91-99`)。実装済み判定であり、残件は HW 確認のみである (`docs/superpowers/specs/2026-09-15-uart-features-design.md:12,38-39`)
- `KEY_DELETE`：LEN0。Classic 鍵全削除（Switch 側登録解除も要案内）(`spec/protocol_v3.md:143`, `src/proto/dispatch.c:81-83`, `tests/host/test_config.c:101-106`)
- `WIRED_MODE`：`[0]=0/1`。範囲外拒否（`0x14`）。Flash 保存・起動時復元。切替は再起動適用（受理後約 500ms で自発再起動）。有線起動では無線一式を上げない。取込・再生は無線起動でのみ有効であり、有線中の要求は `0x10`/`0x11` で拒否する（先に `WIRED_MODE=0`＋再起動が必要）(`spec/protocol_v3.md:144-149`, `src/proto/dispatch.c:84-93`, `src/main.c:416-433`, `tests/host/test_config.c:108-118`)
- `STATUS_REQ`：LEN0。即時 STATUS 返送＋PLAYER_INFO 付随（上記）(`src/proto/dispatch.c:94-97`)

## PING/PONG と NEUTRAL

- PING（LEN0）に対し、PONG（LEN1）は受信 PING のヘッダ SEQ をエコーする（RTT 対応を一意に）(`spec/protocol_v3.md:113`, `src/proto/dispatch.c:40-42`, `src/main.c:444-445`)。ホスト試験は `0xAB` エコーを確認する (`tests/host/test_config.c:58-64`)
- NEUTRAL（LEN0）は BTN=0・全スティック `0x80` を即時適用する。STATE 全中立と等価の短縮形である (`spec/protocol_v3.md:109`)。UNSUPPORTED 下でも安全停止は通す (`src/proto/dispatch.c:38-39`, `src/main.c:220-224`)

## タイミング・安全

STATE 送信は変化時即送＋定期リフレッシュ（既定 60Hz、上限 1kHz）。USB ポーリングは bInterval 8（実機写し）で、低遅延は到着即 report 更新で確保する。timeout-neutral は直近 STATE から 200ms 無受信で全解放する。WDT は 2s であり、timeout-neutral とは別機構（前者=入力安全、後者=生存性）である。USB 監視は unmount/suspend で即中立、再 mount で復帰する (`spec/protocol_v3.md:169-173`, `src/main.c:58,518-550`)。

## PC 送信ガイド（`poc_send.py` は PoC 専用）

PC 送信ラッパ 1 箇所に集約すべき項目は HAT 相当・Y 反転・12bit pack・差分＋リフレッシュ・単一 `write()`・方向別 SEQ・HELLO・CONFIG 送信・FTDI latency 1ms である (`spec/protocol_v3.md:182`)。現行の PoC 送信器 `src/poc_dualcore/poc_send.py` は負荷試験専用であり、本番送信ラッパとは別物である (`src/poc_dualcore/poc_send.py:2-5`)。

使い方の要点は次の通り。既定 `--baud 1000000 --hz 1000` で STATE を送り続ける。115200bps 上限アダプタでは `--baud 115200 --hz 500` に落とす（derated 試験）(`src/poc_dualcore/poc_send.py:7-11`)。`--hello` は HELLO→HELLO_ACK＋自動 STATUS 確認用 (`src/poc_dualcore/poc_send.py:280,306-310`)。`--ping N` は PING→PONG の RTT 計測用 (`src/poc_dualcore/poc_send.py:278-279,311-315`)。`--sweep` は全ボタンの系統的確認用であり、Home は確認画面から抜けるため最後尾に回す (`src/poc_dualcore/poc_send.py:184-200,272-277,316-324`)。sweep 既定は目視用に 60Hz に落とす (`src/poc_dualcore/poc_send.py:317-319`)。FTDI 系は latency timer 1ms 推奨であり、設定できなければ警告のみ出す (`src/poc_dualcore/poc_send.py:297-301`)。帯域目安として STATE=13B を 1kHz で送ると約 13KB/s（リンク使用率約 13%）である (`spec/protocol_v3.md:34`)。

## 輸送写像と USB 写し固定値

u32→Switch 1 輸送 3B の写像表、輸送 report 層の固定加工（USB `0x30`/`0x21` の `btn[1] |= 0x80` 等、電池・振動バイト）、スティック 12bit 化（`x12 = x8 << 4`、`y12 = 4096 − (y8 << 4)`、4095 clamp）は仕様書の写像表に従う (`spec/protocol_v3.md:191-229`)。USB の VID/PID・ディスクリプタ・文字列・SPI 応答・ハンドシェイク等の写し固定値は仕様書 §12 に集約されている (`spec/protocol_v3.md:232-252`)。有線 FW は無線を上げず、無線停波は `WIRED_MODE` で管理する (`spec/protocol_v3.md:250-252`)。公開資料の由来は [References](References.md) R1–R8 を見ること。

## レポート配置（in-repoピン、再構築用）

- USB ControllerData 12B は timer・電池 `0x91`・ボタン3B（`btn[1]|=0x80`・`btn[2]&=0xCF`）・スティック6B・振動 `0x09` である (`src/usb/usb_hid.c:56-68`)。`0x30` 64B は ID + 12B + 36B IMU(0) + 15B 埋めである (`src/usb/usb_hid.c:70-79`, `src/usb/usb_hid.h:33-35`)。IMU無効時は 0 のままにする (`src/usb/usb_hid.h:33-35`)。マスクの期待値は host 試験 `[6]` が縛る (`tests/host/test_usb.c:154-166`)
- USB `0x21` 64B は ID + 12B 状態 + ack/sub + 本文である (`src/usb/usb_hid.c:140-143`)。`0x01` 対 `01/02/03` の雛形は 2wiCC `procon_data.c` の実働値であり type 1 のみ自 MAC を ASCII 埋めする (`src/usb/usb_hid.c:81-91,144-161`)。`0x02` 本文は `03 48 03 02 + MAC6 + 01 02` であり末尾 `02` が SPI 色有効の条件である (`src/usb/usb_hid.c:162-175`)。`80 xx` 応答は常に 64B ゼロパディングで返し `81 01 00 <type=0x03> <mac6>` を含む (`src/usb/usb_hid.c:19-41`)
- BT `0x30` 14B は `A1 30 timer 80 btn3 stick6 08` であり (`src/bt/hid.c:155-168`)、応答共通部 16B は `A1 21 timer 80 btn3 stick6 08 ack sub` である (`src/bt/hid.c:131-145`)。応答は `A1+ID+本体48=50B` へ 0 埋めする (`src/bt/hid.c:411-416`)。ペア前は 100ms の空 `A1 00 timer` で生かす (`src/bt/hid.c:169-179`, `src/bt/hid.c:123-126`)
- スティック 12bit 化は `pack_stick_12bit` のみが行い (`src/proto/pack.c:31-38`，`src/main.c:501-516`)、ボタン 3B は `ctrl_pack_btn3` が作る (`src/proto/pack.c:4-29`)。GR/GL/C/Headset・予約bitは輸送位置なしのため落とす (`src/proto/pack.c:25-28`)

## ハンドシェイク SUB 順（コード受付順、Switch発行順はHW観測）

- BT は `0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x10,0x21,0x30,0x31,0x33,0x40,0x43,0x48,0x50` を受け、既定は `80 sub` ack である (`src/bt/hid.c:277-378`)。`0x03` で full-mode 開始＋入力モード保持し (`src/bt/hid.c:295-304`)、`0x30` で player lamp を latch し (`src/bt/hid.c:328-334`)、`0x31` 応答にそれを載せる (`src/bt/hid.c:335-339`)。`0x10` SPI は番地・長さで表引きし、未知・不足は答えない (`src/bt/hid.c:243-275`)
- USB は `80 04` で入力開始、`80 05`・unmount で中立＋再待機である (`src/usb/usb_wired.c:217-221,315-322`)。`0x01` SUB は `01/02/03/10/30/40/48` と既定 ack を返し (`src/usb/usb_hid.c:143-228`)、`0x03 mode 0x30` も full 開始合図にする (`src/usb/usb_wired.c:270-275`)。`0x10` 振動のみと sub `0x10` SPI 読出は別物であり混同しない (`src/usb/usb_wired.c:231-234,259-269`)
- Switch の発行順そのものは AB1 勝利ログの SUB 完走で確認する (`log/COM3_2026_09_14.22.32.18.050_ab1.txt`)。コードは順序非依存に受ける

## SPI 番地動作（再構築用）

- 表は `0x6010`/16・`0x601B`/1・`0x6000`/16・`0x6050`/13可変・`0x6080`/`0x6098`/`0x603D`/`0x6020`・`0x8010`/`0x8028`→`0xFF` である (`src/proto/spi.c:62-76`)。`spi_find` は完全一致検索である (`src/proto/spi.c:78-86`)
- `0x601B=0x01` が無いと Switch は `0x6050` を無視する (`src/proto/spi.c:15-19`)。USB の `0x6000` 域は `0xFF` で答え (`src/usb/usb_hid.c:200-205`)、表にない `0x60xx` は `0xFF` 埋め、範囲外は無応答である (`src/usb/usb_hid.c:206-215`)。`0x6050` 13B 目は仕様値のため保存復元しない (`src/bt/store.c:139-151`)
- 色 13B の由来と HW 裏取りは [References](References.md) R3・R6 を見ること

## ERRCODE 完全表（再構築用）

- `0x00` 正常・`0x01` LEN不正・`0x02` CRC不一致・`0x03` SEQ欠番・`0x04` UNSUPPORTED版・`0x05` UART overrun・`0x06` parser overflow である (`spec/protocol_v3.md:135`, `src/proto/protocol.h:42-50`)
- CONFIG 拒否は `0x10 + (TYPE & 0x0F)` で作る (`src/proto/dispatch.c:24-27`)。すなわち `0x30`→`0x10`・`0x31`→`0x11`・`0x34`→`0x14` であり (`spec/protocol_v3.md:140-149`)、範囲外秒数・未保存再生・範囲外有線値で発火する (`src/proto/dispatch.c:62-93`)
