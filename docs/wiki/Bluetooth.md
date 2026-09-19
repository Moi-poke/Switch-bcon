# Bluetooth — Classic BT 接続と identity

対象は Switch 1 Pro Controller の Classic BT エミュレーションである。相手の実測は Nintendo Switch 2（peer 終始同一）であり、SUB 到達・WDT の各試験はこの相手で行われた (`docs/wdt_phenomena_brief_20260915.md:9`)。

## identity 接触点（将来の personality 作業用）

> 🚧 In-progress: 機種偽装基盤（personality テーブル、`PERSONALITY_SET 0x37`、再起動適用）は設計のみであり未実装である。Phase C-0（spike、接触点の列挙）が設計入力として指定されている (`docs/superpowers/specs/2026-09-15-uart-features-design.md:52-54`)。下表は現行 ProCon 値の所在を示すものであり、変更手順ではない。

| 接触点 | 所在 | 値・内容 |
|---|---|---|
| BT 識別情報（CoD・VID/PID・記述子・名前） | `src/bt/switch_hid.h:59-76` | `SWITCH_VENDOR_ID 0x057E`、 `SWITCH_PRODUCT_ID 0x2009`、版 `0x0001`、OUI `7c:bb:8a`、GAP 名 `Pro Controller` (`src/bt/switch_hid.h:59-76` 付近の定義群。grep で確認) |
| 自 MAC 生成 | `src/bt/link_conn.c:14-24` | OUI 固定＋基板 unique ID 下 3B。bump なし安定 MAC が proven 組合せである旨の注記付き |
| GAP 名・クラス・SDP | `src/main.c:1500-1501,1514-1527` | `gap_set_class_of_device`、`gap_set_local_name(SWITCH_GAP_NAME)`、HID/PNP の SDP 登録。HID パラメタは `hid_params` で初期化 (`src/main.c:1370-1376,1514-1527`) |
| report builder 群 | `src/bt/hid.c:283-449` | SUB 応答 `answer_subcmd`、入力 report 送出、振動 intake。SUB `0x30` で `probe_player_id` 取得、SUB `0x31` で応答に使用 (`src/bt/hid.c:334-345`) |
| SPI 応答（色・シリアル） | `src/proto/spi.c`、`src/proto/spi.h` | ProCon SPI フラッシュの中身。移植元は pico-wakecon (`src/proto/spi.h:4`) |
| USB 側 VID/PID/文字列 | `src/usb/usb_descriptors.c:21,176` | `idVendor 0x057E`、製品名 `Pro Controller`（grep で確認） |

自 MAC は起動時に印字される (`src/main.c:1397-1399`)。無線再投入で transport の MAC が既定に戻るため、`link_radio_update` の電源再投入時は `hci_set_bd_addr(probe_addr)` を掛け直す。掛けないと別機器扱いになり再接続できない (`src/bt/link_conn.c:128-131`)。

## レポート配置と polling（再構築用、由来は References R2・R8）

- BT `0x30` 14B は `A1 30 timer 80 btn3 stick6 08`、応答 `0x21` は `A1 21 timer 80 btn3 stick6 08 ack sub …` を 50B へ 0 埋めする (`src/bt/hid.c:136-151,161-174,417-426`)。電池は `0x08` 固定であり USB の `0x91` とは別値である (`src/bt/hid.c:147,172`, `src/usb/usb_hid.c:61,67`)
- 送信優先度は応答 > `0x30` > 空 `A1 00` である (`src/bt/hid.c:154-185`)。周期は full 7ms・ペア前 100ms であり (`src/bt/hid.c:129-132`)、USB 側 8ms (`src/usb/usb_wired.c:26-27`) とは別である。SUB 受付一覧と `0x03`/`0x30` の意味は [Protocol](Protocol.md) ハンドシェイク節を見ること
- 値の公開由来（2wiCC ControllerData・dekuNukem系・retro-pico-switch）と HW 証拠（AB1完走）は [References](References.md) R2・R4・R7・R8 を見ること

## SNIFF 要件（SUB 到達の必須条件）

Switch 2 は SNIFF 受容なしでは HID open 後に SUB を送らず、約 1 秒で `0x13` 切断する。AB1（link-policy 1 行変更）で SUB フル完走×2 セッション・切断ゼロ、AB5（BUMP のみ・SNIFF OFF のまま）で open→SUB なし→`0x13` を×4 反復・SUB ゼロ件であり、単一変数分離は clean である (`docs/history/2026-09-15-wdt/verification-status.md:14-19`, `docs/history/2026-09-15-wdt/trial-history.md:16-18`)。

コードは既定で SNIFF を有効化する。`gap_set_default_link_policy_settings` に `ROLE_SWITCH | SNIFF_MODE` を渡す (`src/main.c:1504-1505`)。コメントにも AB1 proven の旨が記録されている。SNIFF 突入自体は無害であり、AB1 ログに `MODE_CHANGE` 6 回＋完走、W6 生存セッションに 3 回＋30 秒生存があるため「SNIFF 突入が殺す」説は棄却済みである (`docs/history/2026-09-15-wdt/verification-status.md:39-41`)。

## ペアリング / 非ボンディング動作

- SSP は入出力なし・自動受諾である (`src/main.c:1507-1508`)
- Switch は常時 `AuthReq=0x00`（non-bonding）のため、BTstack は fresh 鍵を保存しない設計である（`hci.c` 保存条件をコード確認）。それでも起動時 `link keys=1` が出るという奇妙だが確定した観測がある (`docs/history/2026-09-15-wdt/verification-status.md:64-66`)
- 鍵関連イベントは可視化のみ行い、秘密は出さない。SSP 系（`0x31/0x32/0x33/0x36`）は計数し、BD_ADDR 応答・接続要求・link-key 要求・認証完了・暗号化変更は peer BD_ADDR 程度まで印字する (`src/main.c:1258-1334`)
- HID open 失敗 `0x66`/`0x6a` は鍵拒否であり、両側のペアリング削除＋再ペアを案内する (`src/main.c:1207-1210`)。元祖 `0x66` の半分は outgoing 競合（`Create outgoing HID Control`→認証競合）であり、受動接続のみへの切替は繰延である (`docs/handoff_bt_20260914.md:9,41`)
- T3 の `0x66`→鍵破棄→再ペア成功は実地で複数回動作している。認証失敗時は stale 鍵を忘れて次回を clean にする (`src/main.c:1320-1324`, `docs/history/2026-09-15-wdt/verification-status.md:59-60`)
- T3 の鍵削除が 2 回発火して無事だったのは、非 bonding で残留鍵なし→実質 no-op との整合である (`docs/history/2026-09-15-wdt/verification-status.md:67-69`)

## 再接続設計（自動再接続あり）

- 再接続ハンドラは 5 秒周期で回る。HID 未接続・outgoing 未試行・host 既知なら `hid_device_connect` で出ていき、15 秒待っても繋がらなければ諦めて次周回に回す (`src/bt/link_conn.c:36-38,40-84`)
- 有線モード中は Classic に出ていかず、タイマだけ繋ぎ直して周期を保つ (`src/bt/link_conn.c:44-49`)。再生中も繋ぎに行かない（偽装 MAC で名乗るのを防ぐ）(`src/bt/link_conn.c:59-65`)
- 切断時は `link_note_disconnected` で HID CID・outgoing 状態を片付ける (`src/bt/link_conn.c:86-91`)。接続時は `link_mark_connected` で outgoing 済みにする (`src/bt/link_conn.c:93-97`)
- Grip なし自動再接続（outgoing）は実地で動作確認済みである（AB5 の 4 サイクル等）(`docs/history/2026-09-15-wdt/verification-status.md:60-62`)
- 有線/無線の電波制御は `link_radio_update` に一元化する。有線中は電波を止め、未列挙のときだけ USB を蹴る (`src/bt/link_conn.c:118-140`)。`link_apply_wired_mode` は接続中なら先に切り、無線復帰時は USB 側を外して二重認識を防ぐ (`src/bt/link_conn.c:142-158`)
