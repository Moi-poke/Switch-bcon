# 検証状態書：他AI評価用（2026-09-15時点）

対象：Raspberry Pi Pico 2 W＋BTstack Classic＋Switch Pro Controllerエミュレーション
（相手はNintendo Switch 2、peer `3C:A9:AB:37:CF:D3`終始同一）。
評価してほしいのは「何が検証済みか」「機序仮説の強度」「次実験の妥当性」である。

## 0. 二大問題の分離（前提）

- **問題S（解決済み）**：HID open後にSwitchがSUBを送らず約1秒で`0x13`切断 → **SNIFF受容が必須条件**と確定
- **問題W（継続中）**：BT動作中のFlash書込→約2.0秒後にWDT再起動。本書の主題

## 1. 検証済み事項（証拠付き）

### V1：SNIFF受容がSUB到達の必要条件
- AB1（link-policy 1行のみ変更）：SUBフル完走×2セッション、切断ゼロ
 （`log/COM3_2026_09_14.22.32.18.050_ab1.txt`、680KB）
- AB5（BUMPのみ変更、SNIFFはOFFのまま）：open→SUBなし→`0x13`を×4反復、SUBゼロ件を全件grepで確認
  （`log/COM3_2026_09_14.22.36.22.184_ab5.txt`）
- 単一変数分離は clean。スコア：SNIFF-on勝利 2/2、SNIFF-off敗北 4/4

### V2：Flash書込→約2.0秒後WDT死（問題Wの核心）
- Phase-3セッション2、W1×3、W5×5の**計9/9**で同一署名
- 対照：無書込セッション約14回はすべて生存（数分維持・正常`0x13`切断のみ）
- WDTタイムアウト自体が2.0秒のため、「書込時点で給餌停止→2秒後発火」と読むのが自然

### V3：書込は約2msで正常完了する（ストール死ではない）
- W5計測：`store dt_us≈2070 rc=0`が5/5同一。後続ログも出力される
- よって死因は「書込中の停止」ではなく「書込後の破壊／停止」

### V4：SUBは線路上に存在しなかった（受信落下説の棄却）
- hci_dump有効時代にopen〜切断間のSwitch→Pico方向ACLデータがゼロ件であることを直接確認
- 「送ったが落とした」ではなく「送っていない」が確定

### V5：L2CAP交渉は死亡・勝利でバイト同一
- Step-1机上デコード：PSM順・identifier・MTU（672/100）・FCS・HNCP/NCP収支すべて一致。
  差分はSSP nonce・link key更新のみ＋TLV書込の有無
- 1-outstanding ACLは正常パイプライン（controller credit 3に対し1）

### V6：SNIFF突入は無害
- AB1ログに`MODE_CHANGE` 6回＋完走、W6生存セッションに3回（2→0→2）＋30秒生存
- よって「SNIFF突入が殺す」説は棄却。殺すのは書込（との組合せは未分離、下記U1）

### V7：post-mortemは「正常終了後の不発」を示す（W4）
- trail `2,3,4,5,7,8,9,10`＋last=10：`poll_tick`・`usb_handler`とも正常復帰済み
- `poll`停止＋IRQ進行。`ev=0x6e`は既知caveat（SDK競合）のため参考値扱い
- ただし §3 のW4計装限界（stage=2≠完走）に注意：tail／再登録パスは不可視だった

### V8：二重登録は犯人ではない（コード＋ログの両面）
- BTstack `base_add_timer`の二重登録は`log_error("already registered!")`を出す実装
- `log_error`は`hci_dump_log`経由の有効パス（`ENABLE_LOG_ERROR`定義済み）
- dump有効の死亡ログ4件に痕跡ゼロ → 二重addは起きていない
- 帰結：adds−firesの+1差は計数由来（跨起動蓄積の可能性）であり証拠から外す

### V9：実行基盤の特定（SDK実コード確認済み）
- run loop＝async-context over `threadsafe_background`（none系も使用）
- タイマコールバックは低優先度IRQ内でrecursive mutex保持下に実行、mainはsem待機
- よって疑域は「timer list操作」より「at-time alarm再予約経路・低優先度IRQ処理」に絞られる

### V10： fieldで動作確認済みの副次機能
- T3の`0x66`→鍵破棄→再ペア成功が実地で複数回動作
- Gripなし自動再接続（outgoing）が実地で動作（AB5の4サイクル等）
- グリップ色表示パス正常（L=`464646`・R=`FFFFFF`で左右分離を目視確認）

### V11：鍵まわりの奇妙だが確定した観測
- Switchは常時AuthReq=0x00（non-bonding）のためBTstackはfresh鍵を保存しない設計
  （`hci.c`保存条件をコード確認）。にもかかわらず起動時`link keys=1`が出る
- セッション中にBTstack由来のlink key TLV書込（`BTL`タグ・28B）が発生し、
  **そのセッションは生存した**（無書込ではないのに生存した唯一例）
- T3の鍵削除は2回発火して無事（非bondingで残留鍵なし→実質no-opとの整合）

## 2. 棄却済み（証拠あり）

単純コールバック再入／reconnect二重登録／Core1・DMA・割込み干渉／inbox洪水／
単純ストール／`poll_tick`本体 wedged（W4 stage完走）／空レポート原因／
SDP・descriptor・CoD・名前・OUI・MTU・送信先CIDの差分／受信落下／
erase級長時間停止（W5で2ms確定）／SNIFF突入単独／二重登録（二重add痕跡ゼロ）

## 3. 未確定・係争点（意見を求める）

### U1：書込とSNIFF突入の交互作用は未分離
全死亡例で書込の前後にSNIFF突入が近接している。書込単独か複合条件かは、
フェーズ別A/B（特に「安定後アイドル中の書込」E）が決める。

### U2：Session-1型（書込ゼロ死、単発）の位置づけ
T4でBTstack内部ログを消したためL2CAP進行が不可視だった区間の死。
同一機序に統一しない（W7カウンタ装備で再発待ち）。link keys=1の出所とも関係する可能性。

### U3：`del=1/1`の正体
W7死亡セッションで自前forget経路の呼出しがないのにdeleteカウンタが1。
BTstack bank内部のinvalidateの可能性。store本体の副作用範囲の確定が必要。

### U4：BTstack由来鍵保存の生存との非対称
自前host保存は9/9で死ぬのに、BTstack鍵保存セッションは生存。
op種別・タイミング・bank状態のどれが効いているか不明。恒久worker設計に影響する。

### U5：alarm再予約喪失は未証明
現状の本命だが、at-time workerのarm状態を死直前で直接観測した例はない。
W7のusb adds/fires会計は継続監視に使える。

### U6：WDT給餌の1ms周期依存
2.07ms停止に対し1ms給餌は毎回deadlineを跨ぐ（9/9再現率の説明候補）。
周期A/B（1/5/10/50ms）で確定化する価値あり。給餌と実処理の分離も検討。

## 4. 次実験の妥当性評価を求めるもの

1. フェーズ別A/B（A:未接続／B:ACL済／C:暗号化済／D:open直後／E:安定後／F:切断後）＋世代番号で毎回物理書込を保証
2. timer周期A/B（給餌2秒固定、処理周期のみ変更）
3. 恒久案：BT動作中のFlash変更を全面禁止し、dirty-flag＋安全条件付き単一worker
   （ACL=0・HID切断・outgoingなし・pairingなし・L2CAPなし・TX待ちなし）へ集約。
   BT停止→保存→再開は第二段階
4. WDT複合heartbeat＋scratch breadcrumbの恒久計装

## 5. 制約（評価の前提）

- HEAD `c366bf4`、commit・PRは明示指示まで禁止
- `C:\Users\moilo\pico-wakecon`参照専用・改変禁止、秘密鍵バイトの記録禁止
- SDK・BTstackソースは改変対象外（計装は自前層のみ）
