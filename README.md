# switch-bcon

Nintendo Switch 用 Pro Controller エミュレータ
Raspberry Pi Pico 2 W 上で動作確認済

USB 接続/Classic Bluetooth 接続に対応し、PC から UART 経由で入力状態を受信して Switch へ転送します。
また、BLE Wake Beacon の取得・再生機能を備えています。

本プロジェクトは Poke-Controller 系ツールとの連携を主な利用目的として開発しています。

## 導入方法

1. `../../releases` から利用する構成の UF2 をダウンロードする
2. BOOTSEL ボタンを押しながら Pico 2 W を PC に接続する
3. マウントされたドライブに UF2 をドラッグ＆ドロップする
4. UART を GP4 / GP5 に接続する

## 主な機能

- Nintendo Switch Pro Controller エミュレーション
- USB HID（有線接続）
- Classic Bluetooth HID（無線接続）
- BLE Wake Beacon の記録・再生 (Switch2のスリープ解除)
- UART 経由でのコントローラ状態入力
- Poke-Controller Modified 旧版との互換モード(ビルド時定数変更)
- ホスト環境での単体テスト対応

## 動作状況

| 機能 | 実装 | 実機検証 |
|------|------|----------|
| USB HID | ✅ | ✅ |
| Classic BT HID | ✅ | ✅ |
| BLE Wake Beacon 記録・再生 | ✅ | ✅ |
| 振動出力の振幅転送 | ✅ | ✅ |
| プレイヤー表示・本体色設定 | ✅ | ✅ |
| Poke-Controller ASCII 行モード | ✅ | ❌ |
| NFC | ❌ | — |
| ジャイロ入力 | ❌ | — |
| Joy-Con L/R | ✅ | 有線対応 |

有線Joy-Con L/R に対応。
Switch実機で認識および操作を確認済み。

## 構成

```text
PC ──UART──▶ Raspberry Pi Pico 2 W ──┬── USB HID ──────────────┬──▶ Nintendo Switch
                                     └── Classic Bluetooth HID ─┘
```

入力データは PC から UART 経由で Pico へ送信され、Pico が Switch Pro Controller として振る舞います。

### ハードウェア要件

- Raspberry Pi Pico 2 W

他の Pico 系ボードは現在サポート対象外です。

### アーキテクチャ

デュアルコア構成です。Core1 が UART 受信・パース専用、Core0 が BT・USB 処理と 1ms 周期の tick 駆動を担当します。詳細は Wiki を参照。

- `docs/wiki/Architecture.md`

## プロトコル仕様

通信プロトコルの詳細は以下を参照してください。

- `spec/protocol_v3.md`

主な仕様は次の通りです。

### ボタン状態

- u32 Little Endian
- VIIPER 順
- 22bit 使用

### スティック状態

- LEN8: u8 x4
- LEN12: u16LE x4（12bit 拡張）

### UART

Log 系と data 系の 2 系統です。

Log 系:

- UART0
- GP0 / GP1
- 115200bps

Data 系:

- UART1
- GP4 / GP5
- デフォルト 1Mbps、8N1、フロー制御なし

対応機能:

- Baud Hunt 自動追従
- `BAUD_SET` による速度切替
- BREAK 検出による再探索
- Poke-Controller Modified ASCII 行モード（ビルド時選択）

## ディレクトリ構成

```text
spec/                     プロトコル仕様
src/proto/                フレーム処理・CRC・パーサ
src/usb/                  USB HID 実装
src/bt/                   Bluetooth HID・BLE Wake 処理
src/pokecon.*             Poke-Controller 互換入力
src/main.c                統合ファームウェア
tests/host/               ホスト単体テスト
docs/wiki/                開発者向け Wiki
docs/superpowers/plans/   実装計画
```

## ビルド

Pico SDK 2.3.0 導入済み環境を前提とします。

```powershell
$cmake = Join-Path $env:USERPROFILE '.pico-sdk/cmake/v4.3.4/bin/cmake.exe'
$ninja = Join-Path $env:USERPROFILE '.pico-sdk/ninja/v1.13.2/ninja.exe'
& $cmake -S . -B build -G Ninja -DCMAKE_MAKE_PROGRAM:FILEPATH=$ninja
& $cmake --build build
```

ビルド対象は `pico2_w` 固定です。

出力先:

- `build/` — ファームウェアビルド
- `build-host/` — ホストテスト
- `firmware/` — UF2 保管
- `log/` — シリアルログ

いずれも Git 管理対象外です。

## ホストテスト

Visual Studio Build Tools 環境が必要です。`vcvars64.bat` を実行したコマンドプロンプト上で実行してください。

詳細は以下を参照してください。

- `tests/host/CMakeLists.txt`

## 開発者向けドキュメント

- プロトコル仕様: `spec/protocol_v3.md`
- Wiki: `docs/wiki/Home.md`

## ライセンス

本リポジトリで独自に作成されたコードには、特記のない限り以下を適用します。

- PolyForm Noncommercial License 1.0.0（`LICENSE`）

非商用利用に限定します。本リポジトリの内容を用いた営利目的の利用（販売、有償サポート、書込済みハードウェアの販売、受託開発等を含む）は認めません。商用利用の個別許諾も行っていません。

第三者コンポーネントには各ライセンスが適用されます。

詳細は以下を参照してください。

- `LICENSE`
- `NOTICE.md`
- `LICENSES/`

## 免責事項

本プロジェクトは個人による非公式プロジェクトです。任天堂株式会社および関連会社との提携・後援・承認関係はありません。Nintendo Switch および Pro Controller などの名称は、それぞれの権利者に帰属します。
本プロジェクトの利用によって生じた一切の損害について、作者は責任を負いません。
