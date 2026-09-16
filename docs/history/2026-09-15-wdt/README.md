# BT安定化・WDT調査アーカイブ（2026-09-14〜15）

別AIレビュー用・新セッション引継ぎ用の全部入り。Temp（一時領域）のSDDワークスペースはいい
ずれ消えるため、要点をここへ永続化した。

## 構成

- `verification-status.md` — 他AI評価用：検証済み事項・棄却事項・未確定事項＋証拠（まずこれを読む）
- `trial-history.md` — 時系列の試行錯誤記録（UF2・ログ・結果の対応表）
- `plans/` — 実装計画書3件（09-13全体計画、09-14 BT安定化AB、09-14 Phase3恒久修正）
- `ledgers/` — SDD進捗台帳3件（bcon-sdd＝AB実行、bcon-phase3＝恒久実装、bcon-wab＝WDT定量A/B）
- `sdd/` — 各タスクのbrief／report／diff（再現・検証用原文）
- 関連文書（同階層外）：`docs/handoff_bt_20260914.md`（出発点の引継ぎ）、
  `docs/wdt_phenomena_brief_20260915.md`（第三者レビュー用・事象整理）

## 生ログ・バイナリの所在（git管理外・`log/`）

- UF2：`pico-bcon-wireless-test.uf2`（4:24診断版）、`pico-bcon-ab{1..5}-*.uf2`、
  `pico-bcon-w{0,1,3,4,5,6,7}-*.uf2`、`pico-bcon-phase3-test.uf2`、`pico-bcon-colors-verify.uf2`
- ログ：`COM3_2026_09_*.txt`（本文書の日付別表と対応）

## 現ツリー状態（2026-09-15時点）

- HEAD `c366bf4`、未commit作業あり（Phase 3 T1〜T7＋T6V色＋T6R相当の現状は trial-history 参照）
- commit・PRは明示指示まで禁止（`docs/handoff_bt_20260914.md` §7）
- `C:\Users\moilo\pico-wakecon` は参照専用・改変禁止
- 秘密鍵バイトをログ・文書に出さないこと

## 未完了・申送り

1. WDT恒久修正（quiesced単一worker：host・鍵・色の3経路）— 実装承認待ち
2. Step 3フェーズ別A/B（A〜F窓）＋timer周期A/B — W7結果次第
3. P3-T8集計・Step 4 commit承認要求 — HWバッチ完了後
