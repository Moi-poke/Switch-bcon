# Troubleshooting ・故障の見分け方とログ読解

## 二大問題の見分け方（SUB問題とWDT問題は別件）

- 問題 S（解決済み）：HID open後にSwitchがSUBを送らず約1秒で`0x13`切断する。SNIFF受容が必須条件で、現ツリーは既定で有効化している (`src/main.c:1504-1505`)。SUBが線路上に存在しなかった事はhci_dump有効時代にSwitchからPico方向ACLデータゼロ件で直接確認済みで、「送ったが落とした」でなく「送っていない」が確定である (`docs/history/2026-09-15-wdt/verification-status.md:30-33`)。
- 問題 W（解決済み）：BT動作中のFlash書込のうち入れ子（二重`flash_safe_execute`）のものがcore0のIRQを置き去りにし、約2.0秒後にWDT再起動する。BCON含むタイマ系が止まり以後アプリログが出ない。Fault reporterは沈黙する（非Fault停止）。書込自体は約2msで`rc=0`正常完了に見える (`docs/wdt_phenomena_brief_20260915.md:49-55`)
- 死亡 A（Session-1型、単発）：書込ゼロ死。暗号化から約2秒沈黙してWDTで、HID openなし・Flash書込ゼロの例がある。T4でBTstack内部ログを消した区間のためL2CAP進行が不可視で、W7計装での再捕獲待ちとして同一機序に統一しない (`docs/wdt_phenomena_brief_20260915.md:23-28`, `docs/history/2026-09-15-wdt/verification-status.md:84-86`)
- 死亡 B（解決済みの本命）：入れ子flash機序による書込から約2秒後死で、9回以上再現した (`docs/history/2026-09-15-wdt/trial-history.md:40-44`)。W10cで`pm=1`を3/3実測し機序確定、外側除去とguardで修正した

## WDT 危険の確定事実（証拠付き、日付順の要点）

- 初期発見：open時TLV書込（host住所録6B、`BCHO`）とWDT死の相関。保存停止版（4:24）で死亡停止・60秒超生存・BCON継続を確認した (`docs/wdt_phenomena_brief_20260915.md:18-21`)
- Phase-3（`log/COM3_2026_09_14.23.51.02.287.txt`）：S1書込なし死、S2初回書込から約2秒後死（保存の永続化は次起動の`host=1`で証明）、S3保存スキップ（dedupe一致）からSUBフル完走・生存。教訓は「書込なし死が存在する（書込単独犯説は不十分）」「初回書込から死は再現する」である (`docs/wdt_phenomena_brief_20260915.md:23-28`)
- 定量 A/B（単一変数・復元証明済み）：W0統制は安定・WDTなし。W1（host書込をopenから3秒後タイマへ遅延）は保存から約2秒後死を3/3再現（`log/COM3_2026_09_15.00.45.36.087.txt`）。W3（breadcrumb計装のみ）は安定。W4（遅延とbreadcrumb）は`stage=2` 4/4完全一致で死に場所は`poll_tick`本体外と確定。W5（書込時間計測）は`store dt_us≈2070 rc=0`から約2秒後死が5/5同一（`log/COM3_2026_09_15.01.44.40.359.txt`）。通算で書込から死9/9、無書込から生存約12セッションである (`docs/wdt_phenomena_brief_20260915.md:30-41`)
- W7（統計装、`log/COM3_2026_09_15.02.43.25.327.txt:138-143`）：`w7flash safe/tag/del=1/1`、`w7tmr`全timerでaddsからfires=2、`w7stage`正常復帰trail。対照boot（`log/COM3_2026_09_15.02.42.39.163.txt`）はdedupe skipからSUB 17件完走・生存で、書込から死／skipから生存の対応は9/9則と整合する (`docs/wdt_w7_postmortem_brief_20260915.md:17-26,38-42`)
- W10cと修正PASS：2秒窓計測で最終poll完走・timer list健全・全timer発火中・HCI RX生存のもと`PRIMASK=1`を3/3実測した。SDK pico_flashはコア毎単一スロット共有のため外側と内側で上書きされ、外側exitがPRIMASK=1を復元する。修正（外側除去とguardとBCON `pm`）は検証run（2026-09-16）でPASSした。両store完走からSUB完走から20秒生存、`pm=0`全行、guard沈黙である（`docs/history/2026-09-15-wdt/sdd/plan-a-task-{12,13,14}-report.md`）
- L2CAP机上デコード（Step-1）：死亡・勝利でPSM順・identifier・MTU・FCS・HNCP/NCP収支すべて一致。差分はSSP nonce・link key更新のみとTLV書込の有無で、判定はscheduler不発である (`docs/history/2026-09-15-wdt/verification-status.md:34-37`)。旧本命のat-time alarm再予約経路説は上記PRIMASKリーク機序への置換でmootである

## 棄却済み仮説（再試行不要）

- 結論：棄却済み再入・実行系：単純コールバック再入（W1でtimer文脈に移しても同死）、reconnect二重登録（ガード済みと発火経路なしでも死亡）、Core1/DMA/割込み干渉（製品Core1はprintfなし・短mutex・iters生存）、inbox洪水（frames=3一定）、単純ストール（W5で2ms正常完了を確認）、`poll_tick`本体内wedged（W4 stage完走）は棄却済みである。
- 結論：棄却済み対照・受信系：空レポート原因（wakeconが同一方式で動作）、SDP/descriptor/CoD/名前/OUI/MTU/送信先CIDの差分（wakeconと同一を確認）、受信落下（線路上SUBなしを直接確認）、erase級長時間停止（W5で2ms確定）は棄却済みである。
- 結論：棄却済み経路・登録系：SNIFF突入単独、送信路（ゲート中も死亡・復帰後も生存）、二重登録（二重add痕跡ゼロ）である (`docs/wdt_phenomena_brief_20260915.md:57-67`, `docs/history/2026-09-15-wdt/verification-status.md:71-76`)。

## 未確定・係争点（U1-U6、意見募集中）

- 結論：未確定群残存：書込とSNIFF突入の交互作用は未分離（U1）、Session-1型の位置づけ（U2）、WDT給餌の1ms周期依存（U6）が残る。
- 結論：解決済群整理：`del=1/1`の正体（U3）は別電源混入でCLOSED、BTstack由来鍵保存セッションの非対称（U4）は単発と入れ子の差で解決済み、alarm再予約疑義（U5）はDeath-B確定機序（PRIMASKリーク）への置換でmootである。全文は検証状態書を見ること (`docs/history/2026-09-15-wdt/verification-status.md:78-102`)。

> ✅ Done: WDT問題は解決済み。Death-Bは入れ子flash機序を外側除去とguardで修正しHW検証PASS。quiesced workerは不採用確定。

## ログ読解ガイド（出す順・見る所）

起動bannerから取り逃がさない事 (`docs/history/2026-09-15-wdt/hw-batch-2026-09-15.md:7`)。起動直後の順序はbanner（`=== switch-bcon ===`）(`src/main.c:1382`)からMAC (`src/main.c:1397-1399`)からTLV有無（`tlv=1`）(`src/main.c:1414`)から`wired/host/cap/wdt`行 (`src/main.c:1422-1424`)からCore1 boot（`victim=1`確認）(`src/main.c:1472-1474`)から`ready.`行 (`src/main.c:1562-1566`)である。

- 生存表示 BCON：1秒周期の`BCON t=... hs=... cid=...`行がstats timerの出力である (`src/main.c:1087-1108,1115-1119`)。停止はタイマ系停止の証拠 (`docs/history/2026-09-15-wdt/trial-history.md:40-44`)。`pm=`はPRIMASKで0がIRQ開、store直後の`pm=1`はリークの証拠である (`src/main.c:1072-1077,1107`)
- HID open/close：`hid open. host ... saved`はopen毎に出る表示で、実書込の証拠はBTstackの`write '4243484F'`行と`host saved (n=..)`である (`src/main.c:1221-1224`)。dedupe skip時は`write`行なしでSUB完走・生存の対照になる。`hid closed`は切断の始末である (`src/main.c:1229-1233`)
- SUB群：`SUB=0x..`行がSwitchからPico方向サブコマンドの到達証拠である (`src/bt/hid.c:407-415`)。AB1勝利ログ（`log/COM3_2026_09_14.22.32.18.050_ab1.txt`、680KB）はSUBフル完走x2セッションの証拠である
- 旧死亡パターン：`hid open done`から（tick/cansend/TX）から沈黙からバナー`wdt=1`。`0x66`は`linkkey req`から`hid open FAIL 0x66`から`auth complete 0x05`（基本outgoing発）。stallは`conn ok`から`disc 0x05`約80ms・SSPなし。現行（4:04以降）はopenから約1秒無言からSwitchが切断（`0x13`）からBCON継続（死亡なし）である (`docs/handoff_bt_20260914.md:46-52`)
- 認証：`auth complete status=0x..`が成功/失敗を示す。失敗時は鍵を捨てて再ペアに回す (`src/main.c:1313-1325`)
- RUMBLE：`0x10`受信は復号・蓄積し、変化時に`0x22`で送出する。振幅式は実測接地である (`src/proto/rumble.h`, `src/bt/hid.c:430-443`)
- 中立化：`timeout-neutral`は200ms無受信の全解放で、WDTとは別機構である (`src/main.c:1039`)
- 鍵削除：`keys deleted (classic + host tag)`は`FX_KEY_DELETE`の発火証拠である (`src/main.c:791`)。死亡前ログ全域にゼロ件なら`FX_KEY_DELETE`否定の根拠になる
- 秘密厳守：link-key・LTKバイトは出さない。peer BD_ADDR程度は可 (`src/main.c:1258-1272,1303-1334`)

## よくある落とし穴

- Switch 2ドックでUSB列挙しない：CYW43給電中（無線一式が上がった状態）では実測で列挙しない。有線起動では無線を上げない事。最終FWは`WIRED_MODE`で無線停波を管理する (`spec/protocol_v3.md:290-292`, `src/main.c:1340-1342,1480-1482`)
- 有線中にCAPTURE/BEACONが拒否される（`0x10`/`0x11`）：無線起動でのみ有効である。先に`WIRED_MODE=0`と再起動が必要 (`spec/protocol_v3.md:157-158`)
- 色を変えてもSwitch表示が変わらない：Switchは初回接続時の色をキャッシュするため、登録解除から再接続で取り直させる (`spec/protocol_v3.md:287-288`)
- `tusb.h`と`btstack.h`を同じTUに含める：`hid_report_type_t`が二重定義になる。`src/usb/`と`src/bt/`を分離する事 (`AGENTS.md:30`)
- BTコールバック内にFlash書込を足す：入れ子にすると約2秒後のWDT死を起こす。延期/quiesceしHW検証する事 (`AGENTS.md:37`)。`flash_safe_execute`の入れ子禁止とguard監視が恒久規則である (`src/bt/store.c:73-108`)
- `WIRED_MODE`切替が即時反映されない：再起動適用で、受理後約500msで自発再起動する (`src/main.c:804-808`)
- CH340で1Mbpsが通らない：CH340は1Mbps以下推奨で、FTDI推奨・latency timer 1msである (`spec/protocol_v3.md:32`)
- SCR値を直接信じる：2回とも単独bootモデルと矛盾したため不信扱いである。timer/BCON/dump表示を正とする (`docs/handoff_bt_20260914.md:43`)
