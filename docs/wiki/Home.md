# switch-bcon 開発者Wiki — Home

switch-bcon は Raspberry Pi Pico 2 W 上で動くファームウェアである。
PC からの UART 入力を Nintendo Switch 1 向け Pro Controller 入力に変換し、有線 USB-HID または Classic Bluetooth で Switch に届ける。
無線起動時は USB-CDC でも同じ v3 フレームを受けられる（シリアル変換器不要）。
PC 側で生成した IMU（ジャイロ・加速度）を `IMU_SAMPLE` として送ると、0x30 の IMU 36B として Switch に渡る。
BLE は wake 取込・再生専用であり、Switch 2 の BLE 入力エミュレーションは対象外である (`spec/protocol_v3.md`)。

この Wiki の初版は未コミット作業時点のツリーを記録したものである。プロトコル仕様書の SSOT は `spec/protocol_v3.md` である。仕様と他文書が衝突した場合は、仕様 + `src/proto/*` を正とする (`AGENTS.md:3`)。

## 状態バッジ（文字版、2026-10-30 時点）

- プロトコル: v4 完了（`PROTO_VER=4`、`FW_MINOR=12`）(`src/proto/protocol.h`, `src/proto/dispatch.h`)
- 有線 USB-HID: 実機動作済み（Pro Controller / Joy-Con L/R）
- 無線 Classic BT: 実機動作済み（SNIFF 必須は解決済み）
- 無線 USB-CDC 輸送: 実装済み（単一 UF2・起動時 profile 選択。COM は data と log の 2 本。UART 変換器不要）
- IMU（PC→Pico）: 実装済み。有線 0x30 は ProCon のみ、無線 BT は全 role。PC 側で IMU を生成する方式
- EMULATE_MODE: 実装済み（ProCon / Joy-Con L / Joy-Con R、Flash 保存・再起動適用）
- COLOR_GET/COLOR_INFO: 実装済み（RAM 12B 読戻し）
- RECONNECT: 実装済み（無線の待機状態を明示解除。Switch 受入の保証ではない）
- SUB 到達問題: 解決済み（SNIFF 受容が必須条件。AB1 勝利 2/2、AB5 敗北 4/4）
- WDT 問題: 解決済み（Death-B＝入れ子 flash 機序を外側除去＋guard で修正、HW 検証 PASS）
- RUMBLE 振幅転送: 完了（HW 実測接地）
- 仕様書 v4 改訂: 完了

## ページ索引

- [Architecture](Architecture.md) — デュアルコア分担、Core0/Core1 の責務、dispatch 純粋性、tick パイプライン
- [Protocol](Protocol.md) — フレーム構成、種別表（0x04/0x38-0x3B 含む）、STATUS/ERRCODE、IMU_SAMPLE、USB-CDC、PC 送信ガイド
- [Build-and-Test](Build-and-Test.md) — ファームウェアのビルド手順（フルパス cmake/ninja、派生構成）、17 本のホスト単体試験、UF2/log 運用
- [Flash-and-Persistence](Flash-and-Persistence.md) — TLV タグ表、全書込経路、重複排除、`flash_safe_execute`/Core1-lockout 規則、WDT 危険の要約
- [Bluetooth](Bluetooth.md) — EMULATE_MODE の identity 接触点、SNIFF 要件、ペアリング/非ボンディング動作、再接続設計
- [Troubleshooting](Troubleshooting.md) — WDT 署名と SUB 症状の見分け方、ログ読解ガイド、よくある落とし穴
- [Glossary](Glossary.md) — SUB/BCON/TLV/SNIFF/FX/ACT/死亡 A/B などの用語集
- [Roadmap](Roadmap.md) — 完了項目の記録（rumble、spec v4、ボーレート、personality、IMU）と残項目

## Reproduce path（読順）

初回はspec→Protocol→Architectureまで読めば最低限である。
人間は概要把握が目的であり、再構築手順をたどる必要はない。
等価ファームを再構築するAIは次の順で読む。まず `spec/protocol_v3.md` → [References](References.md)（R1–R8→G1–G7の順で公開解析リポジトリに当たる） → [Protocol](Protocol.md)（フレーム→写像→レポート配置→ハンドシェイク）の順に通読する。続いて [Architecture](Architecture.md)（tick順）・[Bluetooth](Bluetooth.md)（identity接触点）・[Flash-and-Persistence](Flash-and-Persistence.md)（TLV差分）で実装対応を確認する。最後に `docs/history/2026-09-15-wdt/README.md`・`trial-history.md`・`verification-status.md`（AB1/AB5・W0–W8の証拠対応）に当たる。値は必ずin-repoのfile:lineと公開資料の両方で裏取りし、欠値は捏造せずG1–G7に積む。

## 運用ルール（全ページ共通）

- 秘密鍵バイト（リンク鍵・LTK）はログ・文書に一切出さない。peer BD_ADDR 程度は可 (`AGENTS.md:38`, `src/main.c:1258-1272,1303-1334`)
- `C:\Users\moilo\pico-wakecon` は参照専用。改変禁止 (`AGENTS.md:39`)
- `build/`、`build-host/`、`firmware/`、`log/`、`*.uf2` は git 管理外。コミットしない (`AGENTS.md:7`)
- WDT 関連の生の試行錯誤は本 Wiki に複写しない。`docs/history/2026-09-15-wdt/README.md` が索引であり、UF2 と `log/COM3_*.txt` の対応表が正である (`AGENTS.md:40`)
- 未着手・検証待ちの項目は `> 🚧 In-progress:` コールアウト付きでのみ記載し、未実装の動作を事実として書かない
- コード位置はファイル名＋シンボル名を正とする。本文中の行番号は執筆時点の目安であり、編集時は再確認すること

## 関連 SSOT・索引への入口

- プロトコル SSOT: `spec/protocol_v3.md`（v4 文面。旧版乖離の注意は解消済み）
- 運用 SSOT: `AGENTS.md`（ビルド手順、アーキテクチャ、gotcha）
- BT/WDT 来歴索引: `docs/history/2026-09-15-wdt/README.md`、検証状態 `docs/history/2026-09-15-wdt/verification-status.md`、時系列 `docs/history/2026-09-15-wdt/trial-history.md`
- UART 機能拡張設計: `docs/superpowers/specs/2026-09-15-uart-features-design.md`、実装計画 `docs/superpowers/plans/2026-09-15-plan-a-telemetry-v4.md`
- 恒久修正設計: `docs/history/2026-09-15-wdt/plans/quiesced-worker-design.md`、HW バッチ手順 `docs/history/2026-09-15-wdt/hw-batch-2026-09-15.md`

最終更新日: 2026-10-30。
解決しない疑問は関連SSOTの指す文書の来歴に当たる。
