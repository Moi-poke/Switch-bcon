# Roadmap — 完了記録と次の順序

優先順の考え方は共通である。W8 判定ダンプ＋振動取込＋色確認＋PLAYER_INFO 確認を 1 回の HW 占有で回収し、机上側で U3/U5 確定・rumble 連鎖 unpark・最終レビューに進める (`docs/history/2026-09-15-wdt/hw-batch-2026-09-15.md:1-4`)。

## 1. rumble 連鎖（完了 Commit C） — 完了

> ✅ Done (2026-09-18, Commit C `2a3057c`): 非ゼロ `A2 10` 採取（4種・3強度・帰属確定）→ Task 3→4→5→6→9 すべて完了。証拠は台帳＋`log/rumble_A2_extract.txt`。

- Task 2（spike、読取専用）: `log/COM3_*.txt` の hci_dump ACL 行から実 `0x10` ペイロード 3 種以上（中立＋非ゼロ含む）を採取し、写像式 `ampL/R = f(raw8)` と検査ベクタを成果物にする。採取不能なら BLOCKED 報告であり、Task 3 以降は待機である
- Task 3（純粋復号器＋host 試験、TDD）: `src/proto/rumble.c/.h`（BTstack/SDK 取込なし、C11＋stdint のみ）に `rumble_decode_010` を作り、中立→0,0・非ゼロベクタ・NULL→0,0 を試験する
- Task 4（hid intake 蓄積）: `0x10` 分岐（現状 `src/bt/hid.c:430-443`）で復号して RAM 保持し計数する。60Hz spam 防止のため log は初回のみのままである
- Task 5-6（dispatch＋配線）: `ACT_SEND_RUMBLE`＋変化時のみ送出の tick helper＋`flush_outbox` 送信 arm。`STATUS` 7B は変更しない
- HW 確認: Switch の振動チェック画面等で実振動を発生させ、`RUMBLE` 到達を確認する。送出契機（変化時のみ vs 間引き周期）は実装計画で確定する

## 2. spec v4 改訂（Plan A Task 9、文書のみ） — 完了

> ✅ Done (2026-09-18, Commit C `2a3057c`): 仕様書は v4 文面である。Task 9 閉鎖。

改訂内容は次の通り。
- 表題・verdict を `(v4 / PROTO_VER=4)` へ
- 改訂履歴に 4.0 行（RUMBLE 送出開始・PLAYER_INFO 新設・`PROTO_VER=4`・DOWNGRADED 廃止）を追加
- §4 表の `0x22` 用途を送出中へ・`0x23` 行を追加
- `§5.7` を現役記述（左・右振幅 0-255、変化時のみ送出、中立=0,0）へ書換え
- 新 `§5.8` PLAYER_INFO（lamp 写し・flags bit0 IMU/bit1 振動）を追加
- `§5.4` HELLO の RESULT を `0x00` OK／`0x02` UNSUPPORTED のみに直す

検証は `PROTO_VER=3`・`0x03` 版参照・予約記述・DOWNGRADED 動作 claims の残存 grep がゼロであること（`protocol.h` の列挙残骸は仕様に書かない）。

## 3. quiesced worker（不採用・記録のみ、WDT 恒久修正は別途完了） — 不採用確定

> ❌ Rejected / not adopted: Death-B（入れ子 flash_safe_execute）は外側除去＋guard＋BCON `pm` で修正・HW 検証 PASS 済みのため、quiesced worker は採用しない。設計書は記録として保持する (`docs/history/2026-09-15-wdt/plans/quiesced-worker-design.md`)。

- W8 判定ダンプで U3（`del=1/1` の正体）・U5（adds−fires=+2）を単発判定する。判定核心は `w8flash-boot`、`w8last delete`、`w8tmr-boot` の 3 行である (`docs/history/2026-09-15-wdt/hw-batch-2026-09-15.md:11-17`)
- 次実験の妥当性評価を求めるものはフェーズ別 A/B（A 未接続／B ACL 済／C 暗号化済／D open 直後／E 安定後／F 切断後＋世代番号で毎回物理書込を保証）、timer 周期 A/B（給餌 2 秒固定・処理周期のみ変更）、恒久案（dirty-flag＋安全条件付き単一 worker）、WDT 複合 heartbeat＋scratch breadcrumb の恒久計装である (`docs/history/2026-09-15-wdt/verification-status.md:100-112`)
- worker 設計の全文（ゲート 6 条件、排出順、合体規則、`WIRED_MODE` 期限規則、失敗時意味論、host 試験影響、検証手順 11 シナリオ、受入 checklist）は設計書を見ること (`docs/history/2026-09-15-wdt/plans/quiesced-worker-design.md:45-67,228-544`)。grep 証明（BT-active 経路上に worker 外の `store_tag`／`delete_tag`／`gap_delete_all_link_keys` 残存なし）が受入条件に含まれる
- WDT 複合 heartbeat＋scratch breadcrumb の恒久計装は worker とは別承認・別実装であり、同梱しない (`docs/history/2026-09-15-wdt/plans/quiesced-worker-design.md:545-573`)

## 4. personality 基盤（将来の機種偽装） — 将来

> 🚧 In-progress: Phase C-0（spike、設計入力）から着手する。出力は接触点リスト（file:line 付き）のみであり、コード変更なしである (`docs/superpowers/specs/2026-09-15-uart-features-design.md:51`)。

- C-0 既知候補: `switch_hid.h:59-76`（VID/PID/GAP 名）、`usb_descriptors.c:21-22,175-176`（VID/PID/文字列）、`main.c:1370-1376,1504-1505,1514-1527`（SDP・GAP 名・SDP 初期化）、`hid.c` report builder 群、`spi.c` 色・シリアル応答。本 Wiki の [Bluetooth](Bluetooth.md) 接触点表は現行値の所在確認に使うこと
- C-1（基盤）: personality テーブル（id→USB VID/PID/文字列・GAP 名・SDP・builder 選択・SPI 応答選択）＋`PERSONALITY_SET`（番号未定）＋再起動適用（無線構成と同じ制約クラス）。`0=ProCon` は現行値の verbatim 移管であり、挙動不変が gate である
- NOTE: `0x37` は `T_BOOTSEL` 割当て済みのため、`PERSONALITY_SET` には別番号が必要（要所有者判断）
- HoriCon／Joy-Con の実値は C-1 の範囲外であり、「実測値の採取→テーブル 1 行追加」の繰返しとする。同時多機種・実行時無再起動切替は非目標である

## 5. ボーレート Plan B（動的切替） — 完了

> ✅ Done (2026-09-18, Commit B `7cc4cc9`): B-0 hunt＋B §3 `BAUD_SET` を実装し、B5 sweep 捕捉・BREAK 再確定・ladder 全 4 rung ADOPTED を HW 実証済みである。

| index | bps | 備考 |
| --- | --- | --- |
| 0 | 115200 | derated・低速アダプタ用 |
| 1 | 460800 | 中速 |
| 2 | 921600 | 高速 |
| 3 | 1000000 | 既定 |
| 4 | 2000000 | 上限。仕様 §2 と整合し、以後は表拡張のみ (`docs/superpowers/specs/2026-09-15-uart-features-design.md:43`) |

- 手順（2 相・自動復帰付き）: PC が `BAUD_SET (0x36)` 送信→Pico は旧レートのまま ACK 相当（STATUS 即時返送）を返す→双方ガードタイム（例 Pico 100ms 後・PC は ACK 受信＋150ms 後）に切替→切替後に有効フレームを受理できなければ起動時レートへ自動復帰（復帰 timeout 例 2s、新規 timer なし・`poll_tick` 時刻比較）(`docs/superpowers/specs/2026-09-15-uart-features-design.md:44`)
- 適用範囲はデータ UART（UART1）のみであり、ログ UART（UART0 115200）は不変である。変更点は `uart_init` 再実行＋`uart_set_baudrate` の 1 箇所に限定する。永続化は `WIRED_MODE` 準拠の TLV 保存（新 TAG `BCBR`）であり、新レートでの初回有効フレーム受理後に行う
- 範囲外 index は拒否（ERRCODE `0x16`）である
- HW 確認（mystery なし）: FTDI 環境で 115200↔1M 往復＋自動復帰の誘発試験である

## 6. その他の申送り（HW バッチ・集計・commit） — 将来

- W8 判定ダンプ（U3・U5 確定用、最優先）: `firmware/pico-bcon-w8-epoch.uf2` を flash し、通常ペア→open（host 保存）→約 2 秒後 WDT 死を 1 回再現し、次 boot の `w8epoch`〜`w8ev` 行一式を回収する (`docs/history/2026-09-15-wdt/hw-batch-2026-09-15.md:11-17`)
  - →完了: U3/U5 とも CLOSED
- 色確認（#2 実装済み判定の裏取り）: 現 tree ビルドを flash し、`COLOR_SET`→登録解除→再接続で Switch UI 表示＋`0x6050` 応答バイトが新色であることを確認する。読戻し `COLOR_GET` は要望が出てから追加する（YAGNI）(`docs/history/2026-09-15-wdt/hw-batch-2026-09-15.md:27-31`, `docs/superpowers/specs/2026-09-15-uart-features-design.md:38-39,70-72`)
  - →完了 Commit D `9915acf`: C-1/C-3/C-4/C-6 PASS
- PLAYER_INFO 確認: SUB `0x30` 到達後のセッションで `STATUS_REQ` を送り、応答の `PLAYER_INFO (0x23, LEN2)` と Switch 表示の一致を確認する (`docs/history/2026-09-15-wdt/hw-batch-2026-09-15.md:33-35`)
  - →完了 Commit D: P-1/P-2/P-5 PASS、P-3/P-4/P-6 は未検証残
- P3-T8 集計・Step 4 commit 承認要求: HW バッチ完了後に回す (`docs/history/2026-09-15-wdt/README.md:29-33`)。Step 3 フェーズ別 A/B（A〜F 窓）＋timer 周期 A/B も W7 結果次第である
- PC 送信ラッパ本実装・`COLOR_GET` 読戻し・周期的 RUMBLE/PLAYER_INFO 配信・`STATUS` 流用案・BLE 入力エミュレーションは scope 外（YAGNI）であり、新規証拠なしに再提案しない (`docs/superpowers/specs/2026-09-15-uart-features-design.md:70-72`, `docs/superpowers/plans/2026-09-15-plan-a-telemetry-v4.md:406-413`)
