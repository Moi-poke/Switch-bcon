# 試行錯誤の時系列記録（2026-09-14〜15）

凡例：✅勝利（仮説支持）／❌敗北（仮説棄却）／🔬計装（測定）／📦恒久実装

## 9/14：SUB未到達問題の解決まで

| 時刻 | 作業 | 結果 |
|---|---|---|
| 04:30 | 引継ぎ受領（WDT死の真因はopen時TLV書込、鍵呪咒の機構特定済み） | 出発点 |
| — | 机上監査3系統（timer寿命・HID送信経路・SDP/鍵/outgoing）＋BTstack実コード裏取り | 二重A1棄却、OPEN=両PSM完成を確認、`hid_device_send_interrupt_message`素通しを確認 |
| — | wakecon比較 | 空レポート・SDP・outgoing同一→三者とも無罪化 |
| — | PA調査（Arduino-Source／HOJA／debugprobe） | Pico FW非公開・HOJA BTはstub・debugprobeは別物のため比較対象外に確定 |
| — | retro-pico-switch（起原）・Wilstride・NXBT調査 | 起原は0x30先行self-pump型と特定（wakecon系との差分化） |
| — | NXBT v12修正コミット差分取得 | MACマスク・SDP清掃・初回低速化を特定、計画へ反映 |
| 21:59 | **4:24版実機**（`pico-bcon-wireless-test.uf2`） | 両PSM open・SUBなし・`0x13`切断。hci_dumpで線路上SUBゼロを直接確認 |
| 22:32 | **AB1** SNIFF復帰（link-policy 1行） | ✅ SUBフル完走×2セッション、切断ゼロ。**SUB問題解決** |
| 22:36 | **AB5** BUMP=0（SNIFFはOFFのまま） | ❌ open→SUBなし→`0x13`×4。安定MAC単独は無力と確定 |
| — | AB1/AB5からSNIFF必須を結論 | SUB問題クローズ |

## 9/14深夜〜9/15：WDT問題の定量化

| 時刻 | 作業 | 結果 |
|---|---|---|
| 23:51 | **Phase-3テスト版実機**（恒久実装T1〜T7適用後初回） | S1：書込なし死（暗号化→2秒沈黙→WDT）。S2：初回書込→約2秒後死（保存自体は永続化を確認）。S3：保存スキップ→SUB完走・生存。**二種の死を発見** |
| — | **W0** 統制ビルド（無変更） | 正常動作の基準線 |
| — | **W1** 遅延書込（open→3秒後） | ❌ 保存→約2秒後死を3/3再現。コールバック文脈は無関係と確定 |
| — | **W3** breadcrumb計装 | post-mortemで`poll`停止＋IRQ進行を確認（ただし`ev`はcaveat付き参考値） |
| — | **W4** 遅延＋breadcrumb複合 | trail `2,3,4,5,7,8,9,10`＋last=10：`poll_tick`・`usb_handler`は正常復帰済み。死は「次回発火なし」 |
| — | **W5** 書込時間計測 | `store dt_us≈2070 rc=0`が5/5同一。ストール死を棄却、書込後破壊に確定 |
| — | **W6** hci_dump限定復活 | 死亡セッションのL2CAP全取得に成功（下記Step-1の材料） |
| — | **Step-1 机上デコード**（死亡vs勝利） | L2CAPバイト同一（SSP乱数のみ差）。未完了のHCI/L2CAP応答なし。判定B：scheduler不発 |
| — | **W7** 統合計装（counter＋微細stage＋timer会計＋hci_dump） | post-mortem取得体制の完成 |
| 02:43 | **W7実機** | `w7flash safe/tag/del=1/1`、`w7tmr`全timerでadds−fires=2、`w7stage`正常復帰trail。解釈は係争中（§4） |
| — | SDK実コード監査 | run loop＝async-context over `threadsafe_background`を特定。二重addは`log_error`（有効パス）に痕跡ゼロのため棄却。残る本命はat-time alarm再予約経路 |
| — | 外部レビュー#2（W7 post-mortem） | trail確定：本命はADD_TIMER_DONE→次FIRE間の下位scheduler経路。del=+2系は跨起動蓄積が最有力。W8拡張（epoch/baseline/delta）を採用 |
| — | **W8** 計装完成（HW待ち） | `log/pico-bcon-w8-epoch.uf2`（824832B）。`w8epoch/w8flash-total+boot/w8last/w8tmr-total+boot`でU3・U5を単発判定可能。treeは復元済み |
| 21:09 | **W8実機**（`log/COM3_2026_09_15.21.09.02.303.txt`、6 boots） | **U5確定**：全timer全bootで正確に+1蓄積（W7の+2は2boot分と確定）。**U3確定**（機序上）：死亡5boot＋生存1bootすべて`del=0/0`。**二署名発見**：ep1-4はop内死（begin>end・write行なし・host=0のまま、最終eventはPSM-0x11 open直後のhid-open先頭）、ep5は完走後死（write行＋host saved→host=1→約2秒後死）。ep6（bond成立・BTL鍵保存・BCHO skip）はSUB完走＋26分生存。W8実装bug：boot差分行は恒常0/0（baselineがdumpより先に取られる構造）— totals＋epochで判定したため実害なし |
| 23:29 | **W9-observe実機#1**（`log/COM3_2026_09_15.23.29.33.344.txt`、2 boots） | op内死の再現なし。ep1死＝Death-B完全stage付き：S0→S12完走trail・単一clean program（wroff 2255→2269・active不変・migrateなし、`dur=10793`、`host=1`永続）→約2秒後死。ep2はBTL鍵保存完走（8dd連続）＋BCHO skip→SUB完走×2＋長期生存。SNIFF遷移・Switch主導0x13・Grip往復の再接続（`hid open FAIL 0x0e` 1回含む）すべて生存。残件：op内死への9c分岐は未決（bank前提条件不明） |
| 23:50 | **Phase-0再現成功**（`log/COM3_2026_09_15.23.50.44.146.txt`、3 boots、T_KEY_DELETE→電源cycleのtombstone bank） | **op内死を再現**：boot1はincoming接続→0x11 open直後にS0-S3で停止（write行なし・`chg0=chg1=0`・wroff不変・host=0のまま→WDT）。**分岐(b)確定**：物理無変異→pre-write scanのCPU側無限ループが本命（tombstone走査）。H2-migrateはop内死から後退。boot2は同一bankで完走→Death-B（write 901・S12・host=1）。boot3はoutgoing再接続（自動再接続の実証）→bond鍵保存（90f）→host skip→SUB完走・生存。初回store hang→2回目完走の「first touch」パターン |
| 01:30 | **9b A/B実機**（B=`...01.30.17.104_b.txt`、A=`...01.31.20.274_a.txt`） | **両arm死亡でouterはhang地点から除外確定**。B-boot1：通常保存が先に完走（S12・write a69＋host saved＋Erase 2665）→forced gen=1がS3停止→死亡（outerなしでのS3 hang＝scan域or初回inner-lock取得待ち）。A-boot1：通常skip→forcedがwrite＋Erase行まで進行後にS10未到達で死亡（後続inner-lock再取得かstore末尾）。全死亡のoffsetは連続追記（a69→a85→aa9→ae1→b21…、active不変、migrateゼロ）でH2-migrateは全部事例から除外。残件：B-boot2の結末（aa9完走後の生死＝Death-B without wrapperの判定）とA-boot2のcleanな結末 |

## 用語・略号

- SUB：Switch→コントローラへのサブコマンド（Output 0x01）。`SUB=0x..`ログで行単位に可視
- Death-A：書込なし死（Session-1型、単発）。Death-B：書込→約2秒後死（9回以上再現）
- BCON：1秒生存表示。停止＝タイマ系停止の証拠
- TLV：Flash永続層。BCHO＝host住所録タグ、BTL＝BTstack鍵タグ
- SNIFF：省電力リンクモード。受容設定がSUB到達の必須条件（AB1/AB5で確定）

## 未解決のまま新セッションへ送るもの

1. Session-1型の死因（W7計装での再捕獲待ち）
2. `del=1/1`の呼出し元（自前forget経路の発火なし）
3. adds−fires=+1/timerの意味（跨起動蓄積か実在の余剰armか）
4. BTstack鍵保存セッションが生存した非対称の理由
5. 恒久quiesced workerの実装承認
6. P3-T8集計・Step 4 commit承認
