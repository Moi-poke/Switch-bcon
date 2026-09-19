# Glossary — 用語集

## プロトコル・輸送

- SUB：Switch→コントローラへのサブコマンド（Output `0x01`）。`SUB=0x..` ログで行単位に可視である (`docs/history/2026-09-15-wdt/trial-history.md:40-44`)。到達は `probe_report_handler` の `report_id == 0x01` 分岐で処理する (`src/bt/hid.c:397-423`)
- STATE：PC→Pico の全状態フレーム（`0x01`、LEN8）。BTN u32-LE＋スティック 4B である (`spec/protocol_v3.md:75-84`)
- HELLO / HELLO_ACK：版交渉フレーム（`0x10`/`0x11`）。現コードは v4 のみ受付である (`src/proto/dispatch.c:43-61`)
- STATUS：Pico→PC の状態フレーム（`0x20`、LEN7）。flags・最終 SEQ・CRC 累計・欠番累計・ERRCODE である (`src/proto/dispatch.c:116-125`)
- PING / PONG：生存確認（`0x03`/`0x21`）。PONG は PING 自身 seq をエコーする (`src/proto/dispatch.c:40-42`)
- RUMBLE：Pico→PC の振動振幅（`0x22`、LEN2）。v4 で送出する（変化時のみ）。復号式は実測接地である (`src/proto/rumble.h`)
- PLAYER_INFO：Pico→PC のランプ＋フラグ（`0x23`、LEN2）。dispatch＋配線＋HW裏取り済みである（P-1/P-2/P-5） (`src/proto/dispatch.h:58-61`, `src/proto/dispatch.c:103-114`)
- SEQ：方向独立・mod256 の連番。PC→Pico と Pico→PC は別カウンタである (`spec/protocol_v3.md:23,46`)
- CRC8：CRC-8/SMBUS（対象 TYPE..SEQ）。検査値 `0xF4` である (`spec/protocol_v3.md:156-159`)
- ERRCODE：直近エラー理由。`0x00` 正常、`0x01` LEN 不正、`0x02` CRC 不一致、`0x03` SEQ 欠番、`0x04` UNSUPPORTED、`0x05` overrun、`0x06` overflow、`0x10-0x1F` CONFIG 拒否である (`spec/protocol_v3.md:135`)

## ファームウェア内部

- FX：dispatch 受理の効果（`FX_CAPTURE_START`、`FX_BEACON_START`、`FX_COLOR_SET`、`FX_KEY_DELETE`、`FX_WIRED_MODE`）。HW 副作用は持たず、`main.c` の `exec_fx` が実行する (`src/proto/dispatch.h:33-40`, `src/main.c:390-436`)
- ACT：Pico→PC 送信箱の動作（`ACT_SEND_STATUS`、`ACT_SEND_PONG`、`ACT_SEND_HELLO_ACK`、`ACT_SEND_PLAYER_INFO`）。`flush_outbox` が UART 送信に変える (`src/proto/dispatch.h:17-24`, `src/main.c:439-461`)
- inbox：Core1→Core0 の 8 スロット退避列（STATE/NEUTRAL 以外の受信フレーム用）(`src/main.c:65-70`)
- outbox：dispatch の 8 スロット送信箱（`V3_OB_N=8`）。溢れは `ob_dropped` に数える (`src/proto/dispatch.h:30`)
- `poll_tick`：Core0 の 1ms 周期処理。inbox drain→dispatch→FX 実行→outbox flush→pack→USB の順である (`src/main.c:464-561`)
- timeout-neutral：直近 STATE から 200ms 無受信で全解放する入力安全機構。WDT とは別物である (`src/main.c:58,518-550`)
- BCON：1 秒生存表示（`stats_timer` の `BCON t=...` 行）。停止＝タイマ系停止の証拠である (`src/main.c:580-604`)
- DMA ring：UART1 RX の 16KB リング（`RING_BITS=14`）。Core1 がポーリングで回収する (`src/main.c:61-63,319-330`)
- derated：115200bps 上限アダプタ向けの `-DPOC_DATA_BAUD=115200` 構成。既定 1Mbps は不変である (`AGENTS.md:16`)

## Flash・永続化

- TLV：Flash 永続層（BTstack flash-bank TLV）。bcon は `BCxx` 名前空間を使う (`src/bt/store.c:13-20`)
- BCHO / BCCL / BCW1 / BCWR：host 住所録・色・取込 blob・有線モードの各 TAG である (`src/bt/store.c:13-16`)
- BCBR：ボーレート永続の計画 TAG。未実装である (`docs/superpowers/specs/2026-09-15-uart-features-design.md:46`)
- dedupe：変化時のみ保存する重複排除。`store_host` の早期復帰が該当する (`src/bt/store.c:76-82`)
- `flash_safe_execute`：全割込み禁止＋Core1 lockout 下で Flash op を行う保護実行。書込はすべてこの経路である (`src/bt/store.c:28-30,40-72`)
- victim：Core1 側 lockout 参加状態。起動 log の `victim=1` は生値である (`src/main.c:929-931`)
- quiesced worker：BT 動作中の Flash 変更を全面禁止し、dirty-flag＋安全条件付き単一 worker へ集約する恒久修正方針。設計のみである (`docs/history/2026-09-15-wdt/plans/quiesced-worker-design.md:45-67`)

## Bluetooth・WDT 来歴

- SNIFF：省電力リンクモード。受容設定が SUB 到達の必須条件であることが AB1/AB5 で確定した (`docs/history/2026-09-15-wdt/verification-status.md:14-19`)。現コードは既定で有効化する (`src/main.c:959-963`)
- SUB 問題：HID open 後に Switch が SUB を送らず約 1 秒で `0x13` 切断した問題。SNIFF で解決済みである
- WDT：2 秒の生存タイマ。`poll_tick` 末尾で給餌する (`src/main.c:554,1021-1022`)。給餌停止→約 2.0 秒後に再起動（`wdt=1` で識別）である (`docs/wdt_phenomena_brief_20260915.md:11-12`)
- 死亡 A：書込なし死（Session-1 型、単発）。暗号化→約 2 秒沈黙→WDT であり、HID open なし・Flash 書込ゼロの例がある
- 死亡 B：書込→約 2 秒後死。9 回以上再現の本命パターンである
- BCON：上記生存表示行の略称。trial-history ではタイマ系停止の証拠として使う
- SSP：Secure Simple Pairing。入出力なし・自動受諾で運用する (`src/main.c:964-965`)。`0x31/0x32/0x33` 等の可視化は鍵の秘密を出さずに行う (`src/main.c:749-755`)
- non-bonding：Switch が常時 `AuthReq=0x00` で来るため BTstack が fresh 鍵を保存しない動作様式である (`docs/history/2026-09-15-wdt/verification-status.md:64-66`)
- `0x66`：HID open 失敗の代表符号。鍵拒否または outgoing 競合であり、両側削除＋再ペアで対処する (`src/main.c:694-704`)
- `0x13`：切断理由の代表符号。SUB 未到達時の Switch 側切断などで見る
- AB1 / AB5：SNIFF 必須を確定させた単一変数 A/B。AB1（SNIFF 復帰）勝利 2/2、AB5（BUMP のみ）敗北 4/4 である (`docs/history/2026-09-15-wdt/trial-history.md:16-18`)
- W0-W8：WDT 定量 A/B の各 UF2。W0 統制、W1 遅延書込、W3 breadcrumb、W4 遅延＋breadcrumb、W5 時間計測、W6 hci_dump、W7 統合計装、W8 epoch 拡張である (`docs/history/2026-09-15-wdt/trial-history.md:22-36`)
- hci_dump：一時診断の HCI 記録。鍵バイトを出すため最終版から必ず除去する (`docs/handoff_bt_20260914.md:70-77`)
- BUMP：診断用の一時 MAC 末尾加算。安定 MAC 単独は無力と確定し、恒久アドレスは以後不変の方針である (`docs/history/2026-09-15-wdt/trial-history.md:17`)
- personality：将来の機種偽装基盤（ProCon 以外の VID/PID・GAP 名・SDP・report builder 切替）。設計のみである (`docs/superpowers/specs/2026-09-15-uart-features-design.md:49-54`)
