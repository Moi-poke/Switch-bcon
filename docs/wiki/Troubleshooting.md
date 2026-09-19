# Troubleshooting — 故障の見分け方とログ読解

## 二大問題の見分け方（SUB 問題と WDT 問題は別件）

- 問題 S（解決済み）：HID open 後に Switch が SUB を送らず約 1 秒で `0x13` 切断する。SNIFF 受容が必須条件であり、現ツリーは既定で有効化している (`src/main.c:1504-1505`)。SUB が線路上に存在しなかったことは hci_dump 有効時代に Switch→Pico 方向 ACL データゼロ件で直接確認済みであり、「送ったが落とした」ではなく「送っていない」が確定である (`docs/history/2026-09-15-wdt/verification-status.md:30-33`)
- 問題 W（継続中）：BT 動作中の Flash 書込→約 2.0 秒後に WDT 再起動する。最終マイルストーン（暗号化完了 or Flash 保存完了）から約 2.0 秒後に再起動し、BCON 含むタイマ系がマイルストーン時点で止まり、以後アプリログが出ない。Fault reporter は沈黙し（非 Fault 停止として確定）、書込自体は約 2ms で `rc=0` 正常完了する (`docs/wdt_phenomena_brief_20260915.md:49-55`)
- 死亡 A（Session-1 型、単発）：書込ゼロ死。暗号化→約 2 秒沈黙→WDT であり、HID open なし・Flash 書込ゼロの例がある。T4 で BTstack 内部ログを消した区間のため L2CAP 進行が不可視であり、W7 計装での再捕獲待ちとして同一機序に統一しない (`docs/wdt_phenomena_brief_20260915.md:23-28`, `docs/history/2026-09-15-wdt/verification-status.md:84-86`)
- 死亡 B（書込→約 2 秒後死）：9 回以上再現している本命パターンである (`docs/history/2026-09-15-wdt/trial-history.md:40-44`)

## WDT 危険の確定事実（証拠付き、日付順の要点）

- 初期発見：open 時 TLV 書込（host 住所録 6B、`BCHO`）と WDT 死の相関。保存停止版（4:24）で死亡停止・60 秒超生存・BCON 継続を確認した (`docs/wdt_phenomena_brief_20260915.md:18-21`)
- Phase-3（`log/COM3_2026_09_14.23.51.02.287.txt`）：S1 書込なし死、S2 初回書込→約 2 秒後死（保存の永続化は次起動の `host=1` で証明）、S3 保存スキップ（dedupe 一致）→SUB フル完走・生存。教訓は「書込なし死が存在する（書込単独犯説は不十分）」「初回書込→死は再現する」である (`docs/wdt_phenomena_brief_20260915.md:23-28`)
- 定量 A/B（単一変数・復元証明済み）：W0 統制は安定・WDT なし。W1（host 書込を open→3 秒後タイマへ遅延）は保存→約 2 秒後死を 3/3 再現（`log/COM3_2026_09_15.00.45.36.087.txt`）。W3（breadcrumb 計装のみ）は安定。W4（遅延＋breadcrumb）は `stage=2` 4/4 完全一致で死に場所は `poll_tick` 本体外と確定。W5（書込時間計測）は `store dt_us≈2070 rc=0`→約 2 秒後死が 5/5 同一（`log/COM3_2026_09_15.01.44.40.359.txt`）。通算で書込→死 9/9、無書込→生存 約12 セッションである (`docs/wdt_phenomena_brief_20260915.md:30-41`)
- W7（統計装、`log/COM3_2026_09_15.02.43.25.327.txt:138-143`）：`w7flash safe/tag/del=1/1`、`w7tmr` 全 timer で adds−fires=2、`w7stage` 正常復帰 trail。対照 boot（`log/COM3_2026_09_15.02.42.39.163.txt`）は dedupe skip→SUB 17 件完走・生存であり、書込→死／skip→生存の対応は 9/9 則と整合する (`docs/wdt_w7_postmortem_brief_20260915.md:17-26,38-42`)
- L2CAP 机上デコード（Step-1）：死亡・勝利で PSM 順・identifier・MTU・FCS・HNCP/NCP 収支すべて一致。差分は SSP nonce・link key 更新のみ＋TLV 書込の有無であり、判定は scheduler 不発である (`docs/history/2026-09-15-wdt/verification-status.md:34-37`)
- 実行基盤の特定（SDK 実コード確認）：run loop＝async-context over `threadsafe_background` であり、timer callback は低優先度 IRQ 内 recursive mutex 保持下に実行される。疑域は timer list 操作より at-time alarm 再予約経路・低優先度 IRQ 処理に絞られる (`docs/history/2026-09-15-wdt/verification-status.md:55-58`)

## 棄却済み仮説（再試行不要）

単純コールバック再入（W1 で timer 文脈に移しても同死）、reconnect 二重登録（ガード済み＋発火経路なしでも死亡）、Core1/DMA/割込み干渉（製品 Core1 は printf なし・短 mutex・iters 生存）、inbox 洪水（frames=3 一定）、単純ストール（W5 で 2ms 正常完了を確認）、`poll_tick` 本体内 wedged（W4 stage 完走）、空レポート原因（wakecon が同一方式で動作）、SDP/descriptor/CoD/名前/OUI/MTU/送信先 CID の差分（wakecon と同一を確認）、受信落下（線路上 SUB なしを直接確認）、erase 級長時間停止（W5 で 2ms 確定）、SNIFF 突入単独、送信路（ゲート中も死亡・復帰後も生存）、二重登録（二重 add 痕跡ゼロ）である (`docs/wdt_phenomena_brief_20260915.md:57-67`, `docs/history/2026-09-15-wdt/verification-status.md:71-76`)。

## 未確定・係争点（U1-U6、意見募集中）

書込と SNIFF 突入の交互作用は未分離（U1）、Session-1 型の位置づけ（U2）、`del=1/1` の正体（U3。現ツリーの静的呼出グラフでは説明できない）、BTstack 由来鍵保存セッションが生存した非対称（U4）、alarm 再予約喪失は未証明（U5）、WDT 給餌の 1ms 周期依存（U6）である。全文は検証状態書を見ること (`docs/history/2026-09-15-wdt/verification-status.md:78-102`)。

> 🚧 In-progress: W8（epoch/baseline/delta 拡張の最小差分・単一変数）は計装完成・HW 待ちである。`log/switch-bcon-w8-epoch.uf2` で死亡再現→次 boot ダンプ取得により U3・U5 を単発判定する計画である (`docs/history/2026-09-15-wdt/trial-history.md:36`, `docs/history/2026-09-15-wdt/hw-batch-2026-09-15.md:11-17`, `docs/wdt_w7_postmortem_brief_20260915.md:76-82`)。

## ログ読解ガイド（出す順・見る所）

起動 banner から取り逃がさないこと (`docs/history/2026-09-15-wdt/hw-batch-2026-09-15.md:7`)。起動直後の順序は banner（`=== switch-bcon ===`）(`src/main.c:1382`)→MAC (`src/main.c:1397-1399`)→TLV 有無（`tlv=1`）(`src/main.c:1414`)→`wired/host/cap/wdt` 行 (`src/main.c:1422-1424`)→Core1 boot（`victim=1` 確認）(`src/main.c:1472-1474`)→`ready.` 行 (`src/main.c:1562-1566`) である。

- 生存表示 BCON：1 秒周期の `BCON t=... hs=... cid=...` 行が stats timer の出力である (`src/main.c:1087-1108,1115-1119`)。停止＝タイマ系停止の証拠 (`docs/history/2026-09-15-wdt/trial-history.md:40-44`)
- HID open/close：`hid open. host ... saved` は open 毎に出る表示であり、実書込の証拠は BTstack の `write '4243484F'` 行＋`host saved (n=..)` である (`src/main.c:1221-1224`)。dedupe skip時は `write` 行なしで SUB 完走・生存の対照になる。`hid closed` は切断の始末である (`src/main.c:1229-1233`)
- SUB 群：`SUB=0x..` 行が Switch→Pico 方向サブコマンドの到達証拠である (`src/bt/hid.c:407-415`)。AB1 勝利ログ（`log/COM3_2026_09_14.22.32.18.050_ab1.txt`、680KB）は SUB フル完走×2 セッションの証拠である
- 旧死亡パターン：`hid open done`→（tick/cansend/TX）→沈黙→バナー `wdt=1`。`0x66` は `linkkey req`→`hid open FAIL 0x66`→`auth complete 0x05`（基本 outgoing 発）。stall は `conn ok`→`disc 0x05` 約 80ms・SSP なし。現行（4:04 以降）は open→約 1 秒無言→Switch が切断（`0x13`）→BCON 継続（死亡なし）である (`docs/handoff_bt_20260914.md:46-52`)
- 認証：`auth complete status=0x..` が成功/失敗を示す。失敗時は鍵を捨てて再ペアに回す (`src/main.c:1313-1325`)
- RUMBLE：`0x10` 受信は復号・蓄積し、変化時に `0x22` で送出する。振幅式は実測接地である (`src/proto/rumble.h`, `src/bt/hid.c:430-443`)
- 中立化：`timeout-neutral` は 200ms 無受信の全解放であり、WDT とは別機構である (`src/main.c:1039`)
- 鍵削除：`keys deleted (classic + host tag)` は `FX_KEY_DELETE` の発火証拠である (`src/main.c:791`)。死亡前ログ全域にゼロ件なら `FX_KEY_DELETE` 否定の根拠になる
- 秘密厳守：link-key・LTK バイトは出さない。peer BD_ADDR 程度は可 (`src/main.c:1258-1272,1303-1334`)

## よくある落とし穴

- Switch 2 ドックで USB 列挙しない：CYW43 給電中（無線一式が上がった状態）では実測で列挙しない。有線起動では無線を上げないこと。最終 FW は `WIRED_MODE` で無線停波を管理する (`spec/protocol_v3.md:290-292`, `src/main.c:1340-1342,1480-1482`)
- 有線中に CAPTURE/BEACON が拒否される（`0x10`/`0x11`）：無線起動でのみ有効である。先に `WIRED_MODE=0`＋再起動が必要 (`spec/protocol_v3.md:157-158`)
- 色を変えても Switch 表示が変わらない：Switch は初回接続時の色をキャッシュするため、登録解除→再接続で取り直させる (`spec/protocol_v3.md:287-288`)
- `tusb.h` と `btstack.h` を同じ TU に含める：`hid_report_type_t` が二重定義になる。`src/usb/` と `src/bt/` を分離すること (`AGENTS.md:30`)
- BT コールバック内に Flash 書込を足す：約 2 秒後の WDT 死を起こす。延期/quiesce し HW 検証すること (`AGENTS.md:37`)
- `WIRED_MODE` 切替が即時反映されない：再起動適用であり、受理後約 500ms で自発再起動する (`src/main.c:804-808`)
- CH340 で 1Mbps が通らない：CH340 は 1Mbps 以下推奨であり、FTDI 推奨・latency timer 1ms である (`spec/protocol_v3.md:32`)
- SCR 値を直接信じる：2 回とも単独 boot モデルと矛盾したため不信扱いである。timer/BCON/dump 表示を正とする (`docs/handoff_bt_20260914.md:43`)
