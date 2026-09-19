# switch-bcon 開発者Wiki — Home

switch-bcon は Raspberry Pi Pico 2 W 上で動くファームウェアである。PC からの UART 入力を Nintendo Switch 1 向け Pro Controller 入力に変換し、有線 USB-HID または Classic Bluetooth で Switch に届ける。BLE は wake 取込・再生専用であり、Switch 2 の BLE 入力エミュレーションは対象外である (`spec/protocol_v3.md:5`)。

この Wiki の初版は HEAD `c366bf4` + 未コミット作業時点のツリーを記録する。プロトコル仕様書の SSOT は `spec/protocol_v3.md` であり、仕様と他文書が衝突した場合は仕様 + `src/proto/*` を正とする (`AGENTS.md:3`)。

## 状態バッジ（文字版、2026-09-19 時点）

- プロトコル: v4 完了（`PROTO_VER=0x04`、仕様書 v4 文面、Commit C）(`src/proto/protocol.h:11`, `spec/protocol_v3.md:1`)
- SUB 到達問題: 解決済み（SNIFF 受容が必須条件。AB1 勝利 2/2、AB5 敗北 4/4）([Bluetooth](Bluetooth.md))
- WDT 問題: 解決済み（Death-B＝入れ子 flash 機序を外側除去＋guard で修正、HW 検証 PASS。quiesced worker は別案のまま未採用）([Flash-and-Persistence](Flash-and-Persistence.md), [Troubleshooting](Troubleshooting.md))
- RUMBLE 振幅転送: 完了（HW 実測接地、Commit C `2a3057c`）([Roadmap](Roadmap.md))
- 仕様書 v4 改訂: 完了（Commit C）([Roadmap](Roadmap.md))

## ページ索引

- [Architecture](Architecture.md) — デュアルコア分担、Core0/Core1 の責務、dispatch 純粋性、tick パイプライン
- [Protocol](Protocol.md) — フレーム構成、種別表（v4 差分含む）、STATUS/ERRCODE、PC 送信ガイド
- [Build-and-Test](Build-and-Test.md) — ファームウェアのビルド手順（フルパス cmake/ninja、派生構成）、ホスト単体試験、UF2/log 運用
- [Flash-and-Persistence](Flash-and-Persistence.md) — TLV タグ表、全書込経路、重複排除、`flash_safe_execute`/Core1-lockout 規則、WDT 危険の要約
- [Bluetooth](Bluetooth.md) — 将来の personality 作業に向けた identity 接触点、SNIFF 要件、ペアリング/非ボンディング動作、再接続設計
- [Troubleshooting](Troubleshooting.md) — WDT 署名と SUB 症状の見分け方、ログ読解ガイド、よくある落とし穴
- [Glossary](Glossary.md) — SUB/BCON/TLV/SNIFF/FX/ACT/死亡 A/B などの用語集
- [Roadmap](Roadmap.md) — 完了項目の記録（rumble 連鎖、spec v4 改訂、ボーレート Plan B）と将来項目（personality 基盤）、および非採用の quiesced worker（Death-B は外側除去で修正済み）

## Reproduce path（AI向け読順）

等価ファームを再構築するAIは次の順で読む。`spec/protocol_v3.md` → [References](References.md)（R1–R8→G1–G6の順で公開解析リポジトリに当たる） → [Protocol](Protocol.md)（フレーム→写像→レポート配置→ハンドシェイク） → [Architecture](Architecture.md)（tick順）・[Bluetooth](Bluetooth.md)（identity接触点）・[Flash-and-Persistence](Flash-and-Persistence.md)（TLV差分） → `docs/history/2026-09-15-wdt/README.md`・`trial-history.md`・`verification-status.md`（AB1/AB5・W0–W8の証拠対応）。値は必ずin-repoのfile:lineと公開資料の両方で裏取りし、欠値は捏造せずG1–G6に積む。

## 運用ルール（全ページ共通）

- 秘密鍵バイト（リンク鍵・LTK）はログ・文書に一切出さない。peer BD_ADDR 程度は可 (`AGENTS.md:38`, `src/main.c:1258-1272,1303-1334`)
- `C:\Users\moilo\pico-wakecon` は参照専用。改変禁止 (`AGENTS.md:39`)
- `build/`、`build-host/`、`log/`、`*.uf2` は git 管理外。コミットしない (`AGENTS.md:7`)
- WDT 関連の生の試行錯誤は本 Wiki に複写しない。`docs/history/2026-09-15-wdt/README.md` が索引であり、UF2 と `log/COM3_*.txt` の対応表が正である (`AGENTS.md:40`)
- 未着手・検証待ちの項目は `> 🚧 In-progress:` コールアウト付きでのみ記載し、未実装の動作を事実として書かない

## 関連 SSOT・索引への入口

- プロトコル SSOT: `spec/protocol_v3.md`（v4 文面。旧版乖離の注意は解消済み）
- 運用 SSOT: `AGENTS.md`（ビルド手順、アーキテクチャ、gotcha）
- BT/WDT 来歴索引: `docs/history/2026-09-15-wdt/README.md`、検証状態 `docs/history/2026-09-15-wdt/verification-status.md`、時系列 `docs/history/2026-09-15-wdt/trial-history.md`
- UART 機能拡張設計: `docs/superpowers/specs/2026-09-15-uart-features-design.md`、実装計画 `docs/superpowers/plans/2026-09-15-plan-a-telemetry-v4.md`
- 恒久修正設計: `docs/history/2026-09-15-wdt/plans/quiesced-worker-design.md`、HW バッチ手順 `docs/history/2026-09-15-wdt/hw-batch-2026-09-15.md`
