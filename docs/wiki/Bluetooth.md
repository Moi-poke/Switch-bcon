# Bluetooth — Classic BT 接続と identity

対象は Switch 1 Pro Controller の Classic BT エミュレーションである。相手の実測は Nintendo Switch 2（peer 終始同一）であり、SUB 到達・WDT の各試験はこの相手で行われた (`docs/wdt_phenomena_brief_20260915.md:9`)。

## identity 接触点（将来の personality 作業用）

> 🚧 In-progress: 機種偽装基盤（personality テーブル、`PERSONALITY_SET 0x37`、再起動適用）は設計のみで未実装であり、変更手順ではない。下表は現行 ProCon 値の所在を示すものであり、Phase C-0（spike、接触点の列挙）の設計入力として指定されている (`docs/superpowers/specs/2026-09-15-uart-features-design.md:52-54`)。なお `0x37` は `T_BOOTSEL`（開発用）に割当て済みのため、`PERSONALITY_SET` には別番号が必要（要所有者判断）。

| 接触点 | 所在 | 値・内容 |
|---|---|---|
| BT 識別情報（CoD・VID/PID・記述子・名前） | `src/bt/switch_hid.h:59-76` | `SWITCH_VENDOR_ID 0x057E`、`SWITCH_PRODUCT_ID 0x2009`、版 `0x0001`、CoD `0x2508`、OUI `7c:bb:8a`、GAP 名 `Pro Controller`、HID 名 `Wireless Gamepad` |
| 自 MAC 生成 | `src/bt/link_conn.c:14-24` | OUI 固定＋基板 unique ID 下 3B。bump なし安定 MAC が proven 組合せである旨の注記付き |
| GAP 名・クラス・SDP | `src/main.c:1500-1501,1514-1527` | `gap_set_class_of_device`、`gap_set_local_name(SWITCH_GAP_NAME)`、HID/PNP の SDP 登録。HID パラメタは `hid_params` で初期化 (`src/main.c:1370-1376,1514-1527`) |
| report builder 群 | `src/bt/hid.c:283-449` | SUB 応答 `answer_subcmd`、入力 report 送出、振動 intake。SUB `0x30` で `probe_player_id` 取得、SUB `0x31` で応答に使用 (`src/bt/hid.c:334-345`) |
| SPI 応答（色・シリアル） | `src/proto/spi.c`、`src/proto/spi.h` | ProCon SPI フラッシュの中身。移植元は pico-wakecon (`src/proto/spi.h:4`) |
| USB 側 VID/PID/文字列 | `src/usb/usb_descriptors.c:21,176` | `idVendor 0x057E`、`idProduct 0x2009`、製品名 `Pro Controller`（grep で確認） |

自 MAC は起動時に印字される (`src/main.c:1397-1399`)。無線再投入で transport の MAC が既定に戻るため、`link_radio_update` の電源再投入時は `hci_set_bd_addr(probe_addr)` を掛け直す。掛けないと別機器扱いになり再接続できない (`src/bt/link_conn.c:128-131`)。

## レポート配置と polling（再構築用、由来は References R2・R8）

- BT `0x30` 14B は `A1 30 timer 80 btn3 stick6 08`、応答 `0x21` は `A1 21 timer 80 btn3 stick6 08 ack sub …` を 50B へ 0 埋めする (`src/bt/hid.c:136-151,161-174,417-426`)。電池は `0x08` 固定であり USB の `0x91` とは別値である (`src/bt/hid.c:147,172`, `src/usb/usb_hid.c:61,67`)
- 送信優先度は応答 > `0x30` > 空 `A1 00` である (`src/bt/hid.c:154-185`)。周期は full 7ms・ペア前 100ms であり (`src/bt/hid.c:129-132`)、USB 側 8ms (`src/usb/usb_wired.c:26-27`) とは別である。SUB 受付一覧と `0x03`/`0x30` の意味は [Protocol](Protocol.md) ハンドシェイク節を見ること
- 値の公開由来（2wiCC ControllerData・dekuNukem系・retro-pico-switch）と HW 証拠（AB1完走）は [References](References.md) R2・R4・R7・R8 を見ること

## SNIFF 要件（SUB 到達の必須条件）

Switch 2 は SNIFF 受容なしでは HID open 後に SUB を送らず、約 1 秒で `0x13` 切断する。AB1（link-policy 1 行変更）で SUB フル完走 2/2・切断ゼロである。AB5（BUMP のみ・SNIFF OFF のまま）で open→SUB なし→`0x13` を 4/4 反復・SUB ゼロ件であり、単一変数分離は clean である (`docs/history/2026-09-15-wdt/verification-status.md:14-19`, `docs/history/2026-09-15-wdt/trial-history.md:16-18`)。

コードは既定で SNIFF を有効化し、`gap_set_default_link_policy_settings` に `ROLE_SWITCH | SNIFF_MODE` を渡す (`src/main.c:1504-1505`)。コメントにも AB1 proven の旨が記録されている。SNIFF 突入自体は無害であり、AB1 ログに `MODE_CHANGE` 6 回＋完走、W6 生存セッションに 3 回＋30 秒生存があるため「SNIFF 突入が殺す」説は棄却済みである (`docs/history/2026-09-15-wdt/verification-status.md:39-41`)。

## ペアリング / 非ボンディング動作

- SSP は入出力なし・自動受諾である (`src/main.c:1507-1508`)
- Switch は常時 `AuthReq=0x00`（non-bonding）のため、BTstack は fresh 鍵を保存しない設計である（`hci.c` 保存条件をコード確認）。それでも起動時 `link keys=1` が出るという奇妙だが確定した観測がある (`docs/history/2026-09-15-wdt/verification-status.md:64-66`)
- 鍵関連イベントは可視化のみ行い、秘密は出さない。SSP 系（`0x31/0x32/0x33/0x36`）は計数し、BD_ADDR 応答・接続要求・link-key 要求・認証完了・暗号化変更は peer BD_ADDR 程度まで印字する (`src/main.c:1258-1334`)
- HID open 失敗 `0x66`/`0x6a` は鍵拒否であり、両側のペアリング削除＋再ペアを案内する (`src/main.c:1207-1210`)。元祖 `0x66` の半分は outgoing 競合（`Create outgoing HID Control`→認証競合）であり、受動接続のみへの切替は繰延である (`docs/handoff_bt_20260914.md:9,41`)
- T3 の `0x66`→鍵破棄→再ペア成功は実地で複数回動作している。認証失敗時は stale 鍵を忘れて次回を clean にする (`src/main.c:1320-1324`, `docs/history/2026-09-15-wdt/verification-status.md:59-60`)
- T3 の鍵削除が 2 回発火して無事だったのは、非 bonding で残留鍵なし→実質 no-op との整合である (`docs/history/2026-09-15-wdt/verification-status.md:67-69`)

## 再接続設計（拒否回数を数えて打ち切る）

判定は BTstack に依存しない純関数 `src/bt/reconnect_policy.c` に切り出してあり、`tests/host` の `reconnect_policy` で固定する（`link_conn.c` 自体は BTstack を含むため host test できない）。

### 第1版は効かなかった（実測で反証済み・设计上.Goalse している）

最初は「page した回数」を数える予算方式にし、HID open 成功 (`link_mark_connected`) で予算を戻す設計にした。**2026-10-01 実測で完全に無効**：`page budget spent.` が一度も出ず、70秒で 7 回の再接続が反復した。

原因は予算の補充位置。Switch が「開いて即切る」ため、**拒否されるたびに予算が補充されて永久に枯渇しない**。page の成否ではなく、**セッションの使われ方**を見る必要があった。

### 現行設計

- 再接続ハンドラは 5 秒周期。`reconnect_should_page` は以下を全て満たすときだけ page する (`src/bt/reconnect_policy.c:4-27`)：有線でない／再生中でない／未接続／host 既知／応答待ちでない／**拒否回数が `RECONNECT_REJECT_LIMIT`(3) 未満**／**予算が残っている**
- **拒否の定義**：HID 開いてから `RECONNECT_HEALTHY_MS`(5000ms) も経ずに切られたもの (`reconnect_is_rejection`)。実測の `held=472/490/494/508ms` はすべてこれに当たる
- **予算の補充は `link_mark_connected` ではなく `link_note_disconnected`**。そこで判定し、健全だったときだけ `s_page_budget` と `s_rejects` をリセットする (`src/bt/link_conn.c:108-135`)
- `link_note_disconnected` は HID open 後と disc reason の 2 経路から 1 セッション 2 回呼ばれるため、`s_session_active` で 1 セッション 1 回数える
- 拒否が上限に達するとログに `switch rejected us repeatedly. stop paging, wait for Switch` を出して予算を 0 にし、受動待機へ落ちる
- 有線中は Classic に出ず、タイマだけ繋ぎ直す。再生中も繋ぎに行かない

### 2026-10-01 実測（修正前・無制限ループの証拠）

```
conn status=0x00 ok → linkkey req → auth 0x00 → hid open → SUB 0x02…0x30 kinds=7
hid closed
disc reason=0x13 held=472ms      ← Switch が 0.5 秒で能動切断
reconnect armed
```

これが t≈2, 9, 18, 28, 37, 49, 59 秒の **7/7 回**。`conn status` と `auth` は毎回成功しているので「接続できない」のではなく **「Switch が拒否している」**（ゲームがコントローラ1台制限）。本体側の「使用するコントローラのボタンを押してください」と「登録しました」は、この1本が共通の原因と推定される。独立した原因ではない。

> ✅ 2026-10-01 実測で確認済み: 「1台制限ゲーム」状態で `switch rejected us repeatedly.` が 1 回だけ出て以降出ないことを確認した。同時に健全セッション (`held=7073ms`) では拒否に数えず予算が回復することも確認した。拒否 3 回という閾値の妥当性はこの 1 セッションから決めたものであり、汎化の妥当性は未検証。

### 待機状態からの復帰（実コントローラーのボタン相当）

拒否上限で page を打ち切ると受動待機に入る。**時間では自動では復帰しない**（周期的に本体をノックしないことがこの修正の目的）。復帰手段は3つ。

| 手段 | 経路 | 備考 |
|---|---|---|
| **ホストから起こす** | `T_RECONNECT` (0x3B, LEN0) → `FX_RECONNECT` → `link_rearm_reconnect()` | 推奨。PokeCon の `BconSetup.py` に「再接続を試す」ボタンがある。`request_reconnect()` が送る |
| Switch から page される | 着信接続 | budget を見ない経路なので、Switch が自力で呼びに行けば通る |
| Pico を再起動 | `link_init()` で予算回復 | 確実だが重い。ログが飛ぶ |

`T_RECONNECT` の応答が返ったことは「Pico が再接続を試みた」ことまでで、**Switch が受け入れたことは含まない**。接続できたかは UART0 ログの `hid open` を見る。`spec/protocol_v3.md` §5.10 が正。

> 🚧 In-progress: `T_RECONNECT` による実機での明示復帰は**未確認**。上の 3 経路のうちどれが実際に効くかは実機検証が要る。

### 診断ログの注意点（2026-10-01 発見）

`BCON` 行は `usb_wired_is_configured()` を引数に渡しながら**ラベルを1つ書き忘れていた**ため、`wired=` 以降のラベルが1つずつずれていた（snprintf の変換指定子26個に対し引数27個）。`wired=4/6/7/8` が決定的な証拠で、`s_wired` は bool なので 4/6/7/8 になり得ない。実際の値は `probe_hid_cid`、すなわち BT の CID である。`cfg=%d` を挿入してラベルずれを解消済み。**終端側のどのフィールドがずれているかは未確認**（`baud_locked` の L/H は出力されていない）。今後 `BCON` 行を診断に使うときは、ラベルを鵜呑みにせず実際の出所と照らすこと。


### discoverable はペアリング状態に従う

- 起動時の `gap_discoverable_control(1)` は廃し、`link_apply_discoverable` に置き換えた。判断は `reconnect_should_be_discoverable(host_known, quiet)` で、**未ペアのときだけ 1**、ペア済みは 0、有線（quiet）は常に 0 (`src/bt/reconnect_policy.c:37-44`)
- `store_host`（ペアリング成立）と `store_host_forget`（ホストタグ削除）から再評価する。ホストを忘れたときだけ登録画面から見つけてもらえる状態へ戻る。ただし「Switch 側でだけ消した」場合は Pico 側のホストタグが残るので再ペアできない。`T_KEY_DELETE` が既存の解除経路である

- `connectable` は従来どおり quiet でなければ常に 1（受動接続の前提）(`src/bt/link_conn.c:170-172`)
- 有線/無線の電波制御は `link_radio_update` に一元化する。有線中は電波を止め、未列挙のときだけ USB を蹴る

### 鍵の永続化 — 2026-10-01 実測で「問題なし」と確定（従来の記述は誤りだった）

症状1「登録画面のたびに再ペア」を非 bonding SSP の仕様問題とする説は、**2026-10-01 の実測ログで反証された**。以下がその一次証拠。

```
link keys=1                              ← 起動時、再起動を跨いで鍵が生きている
conn status=0x00 ok
linkkey req peer=3ca9ab37cfd3            ← Switch が保存鍵を要求してきている
auth complete status=0x00                ← 認証成功。0x05 → 0x66 の循環なし
auth ok. link keys=1
encrypt change status=0x00 en=1
hid open. host 3C:A9:AB:37:CF:D3 saved  ← host 保存は dedupe 済み。Flash 書込 0
```

- `linkkey req` が出る = Switch 側が保存鍵を持っている。毎回フルSSPを masquerade しているわけではない
- `auth complete status=0x00` = 保存鍵で認証が通る。`0x66` を出さない
- `host saved (n=)` が出ない = `store_host` の dedupe が効いており、再接続時に Flash 書いていない (`src/bt/store.c:117-119`)。WDT 危険経路に入らない

#### 過去の `link keys=0` 観測は別の原因だった

| ログ | MAC | keys | 説明 |
|---|---|---|---|
| 09-14 系 | `…:CB` | 0 | BUMP 時代の MAC。現行 `…:CA` とは別機器として Switch から見ても新規。`keys+host wiped (temp)` も出た |
| 09-16 22:41 | `…:CA` | 0 | 起動時 wipe がまだ tree に残っていた世代 |
| 09-17 00:18 | `…:CA` | 0 | wipe 撤去後。**この1本のみ説明がつかない** |
| 2026-10-01 | `…:CA` | **1** | 正常。認証成功 |

つまり恒久的な欠陥ではなく、**1 セッションだけ再現した非決定的**な事象として扱う。候補は非 bonding SSP で Switch が ACL を先に破棄した際の接続参照失敗 `hci.c:4782`（`if (!conn) break;`）だが、**主因ではない**ので実害が出るまで追わない。

#### 残す観測

`auth ok. link keys=%d`（`src/main.c:1381-1389`）は鍵そのものを出さずに件数だけ可視化する。鍵周りが壊れたときに即座に分かるので常設する。bondable 設定の変更は**しない** — 既定が既に 1 (`hci.c:5544`) であり、上乗せしても意味がない。

#### 症状1が再発したら

本件と無関係に「登録画面のたびに再ペア」が再現したら、本節の証拠を捨てて切り直すこと。上記ログを取り直さないまま推測で直さない。
