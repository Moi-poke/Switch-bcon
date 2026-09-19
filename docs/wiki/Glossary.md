# Glossary — 用語集
本文で使った略語をここでもう一度引ける。

## プロトコル・輸送

- SUB：Switch→コントローラ方向のサブコマンド（BT HID Output `0x01`）。到達判定は `report_id == 0x01` 分岐で行う (`src/bt/hid.c:403-429`)
- STATE：PC→Pico の全状態フレーム（`0x01`、LEN8/LEN12）。BTN u32-LE（VIIPER順、22bit使用）＋スティックで、LEN8はu8x4、LEN12はu16LEx4である (`spec/protocol_v3.md:79-88,111-114`)
- HELLO / HELLO_ACK：版交渉フレーム（`0x10` LEN2／`0x11` LEN4）。Picoはv4のみ受付で、RESULTは `0x00` OK／`0x02` UNSUPPORTEDである (`src/proto/dispatch.c:44-62`)
- STATUS：Pico→PC の状態フレーム（`0x20`、LEN7）。STATE_FLAGS・最終SEQ・CRC累計・欠番累計・ERRCODEの順である (`src/proto/dispatch.c:156-165`)
- PING / PONG：生存確認フレーム（`0x03` LEN0／`0x21` LEN1）。PONGの payload は PING の SEQ エコーである (`src/proto/dispatch.c:41-43`)
- RUMBLE：Pico→PC の振動振幅フレーム（`0x22`、LEN2）。変化時のみ送出する (`src/proto/rumble.h`)
- PLAYER_INFO：Pico→PC のランプ＋フラグフレーム（`0x23`、LEN2）。変化時と STATUS_REQ 付随に送出する (`src/proto/dispatch.h:62-65`, `src/proto/dispatch.c:127-138`)
- SEQ：方向独立・mod256 の連番。PC→Pico と Pico→PC で別カウンタである (`spec/protocol_v3.md:23,47`)
- CRC8：CRC-8/SMBUS（対象 TYPE..SEQ、SYNC除外）。検査値は `crc8("123456789")=0xF4` である (`spec/protocol_v3.md:198-199`)
- ERRCODE：直近エラー理由の1B。`0x00` 正常、`0x01` LEN不正、`0x02` CRC不一致、`0x03` SEQ欠番、`0x04` UNSUPPORTED、`0x05` overrun、`0x06` overflow、`0x10-0x1F` CONFIG拒否である (`spec/protocol_v3.md:144`)

## ファームウェア内部

- FX：dispatch 受理が返す効果要求（`FX_CAPTURE_START`、`FX_BEACON_START`、`FX_COLOR_SET`、`FX_KEY_DELETE`、`FX_WIRED_MODE`）。HW副作用を持たず `main.c` の `exec_fx` が実行する (`src/proto/dispatch.h:33-44`, `src/main.c:767-836`)
- ACT：Pico→PC 送信箱への送信要求（`ACT_SEND_STATUS`、`ACT_SEND_PONG`、`ACT_SEND_HELLO_ACK`、`ACT_SEND_PLAYER_INFO`、`ACT_SEND_RUMBLE`）。`flush_outbox` が UART 送信に変える (`src/proto/dispatch.h:17-24`, `src/main.c:839-866`)
- inbox：Core1→Core0 の8スロット退避列。STATE／NEUTRAL 以外の受信フレームを渡す (`src/main.c:68-73`)
- outbox：dispatch の8スロット送信箱（`V3_OB_N=8`）。溢れは `ob_dropped` に数える (`src/proto/dispatch.h:31`)
- `poll_tick`：Core0 の1ms周期処理。inbox drain→dispatch→FX実行→outbox flush→pack→USBの順に回す (`src/main.c:909-1060`)
- timeout-neutral：直近STATEから200ms無受信で全入力を解放する安全機構。WDTとは別物である (`src/main.c:61,1022-1042`)
- BCON：1秒周期の生存表示行（`stats_timer` の `BCON t=...`）。停止はタイマ系停止の証拠である (`src/main.c:1087-1108`)
- DMA ring：UART1 RX用の16KBリング（`RING_BITS=14`）。Core1がポーリングで回収する (`src/main.c:64-66,683-703`)
- derated：115200bps上限アダプタ向けの `-DPOC_DATA_BAUD=115200` 構成。既定1Mbpsは不変である (`AGENTS.md:16`)

## Flash・永続化

- TLV：Flash永続層（BTstack flash-bank TLV）。bconは `BCxx` 名前空間を使う (`src/bt/store.c:14-22`)
- BCHO / BCCL / BCW1 / BCWR / BCBR：host住所録・色・取込blob・有線モード・ボーレートの各TAG名である。BCBRはrate indexを保存する (`src/bt/store.c:14-18,283-323`)
- dedupe：変化時のみ保存する重複排除。`store_host` の早期復帰のことである (`src/bt/store.c:116-118`)
- `flash_safe_execute`：全割込み禁止＋Core1 lockout下でFlash操作を行う保護実行。書込はすべてこの経路である (`src/bt/store.c:24-28,33-108`)
- victim：Core1側のlockout参加状態。起動logの `victim=1` が成立の証拠である (`src/main.c:1472-1474`)
- quiesced worker：BT動作中のFlash変更を禁止し、dirty-flag＋安全条件付き単一workerへ集約する修正方針。設計のみである (`docs/history/2026-09-15-wdt/plans/quiesced-worker-design.md:45-67`)

## Bluetooth・WDT 来歴

- SNIFF：省電力リンクモード。SUB到達の必須条件で、現コードは既定で有効化する (`src/main.c:1504-1505`)
- SUB 問題：HID open後にSwitchがSUBを送らず約1秒で `0x13` 切断した不具合。SNIFF有効化で解決済みである
- WDT：約2秒の生存タイマ。`poll_tick` 末尾で給餌し、停止後は約2.0秒で再起動する (`src/main.c:1046,1569-1570`)
- 死亡 A：書込なし死（Session-1型、単発）。暗号化→約2秒沈黙→WDTで、HID openなし・Flash書込ゼロの例である
- 死亡 B：書込→約2秒後死。9回以上再現の本命パターンである
- SSP：Secure Simple Pairing。入出力なし・自動受諾で運用する (`src/main.c:1507-1508`)。鍵の秘密は出さず `0x31/0x32/0x33` 等で可視化する (`src/main.c:1258-1264`)
- non-bonding：Switchが常時 `AuthReq=0x00` で来るためBTstackがfresh鍵を保存しない動作様式である (`docs/history/2026-09-15-wdt/verification-status.md:64-66`)
- `0x66`：HID open失敗の代表符号。鍵拒否またはoutgoing競合で、両側削除＋再ペアで対処する (`src/main.c:1207-1210`)
- `0x13`：切断理由の代表符号。SUB未到達時のSwitch側切断などで見る
- AB1 / AB5：SNIFF必須を確定させた単一変数A/B試験。AB1（SNIFF復帰）勝利2/2、AB5（BUMPのみ）敗北4/4である (`docs/history/2026-09-15-wdt/trial-history.md:16-18`)
- W0-W8：WDT定量A/Bの各UF2群。W0統制、W1遅延書込、W3 breadcrumb、W4遅延＋breadcrumb、W5時間計測、W6 hci_dump、W7統合計装、W8 epoch拡張である (`docs/history/2026-09-15-wdt/trial-history.md:22-36`)
- hci_dump：一時診断用のHCI記録。鍵バイトを含むため最終版から必ず除去する (`docs/handoff_bt_20260914.md:70-77`)
- BUMP：診断用の一時MAC末尾加算。安定MAC単独は無力と確定し、恒久アドレスは以後不変の方針である (`docs/history/2026-09-15-wdt/trial-history.md:17`)
- personality：将来の機種偽装基盤（ProCon以外のVID/PID・GAP名・SDP・report builder切替）。設計のみである (`docs/superpowers/specs/2026-09-15-uart-features-design.md:49-54`)
