# switch-bcon

Nintendo Switch 用 Pro Controller エミュレータ
Raspberry Pi Pico 2 W 上で動作確認済

USB 接続/Classic Bluetooth 接続に対応し、PC から UART 経由で入力状態を受信して Switch へ転送します。
無線起動時は USB-CDC 経由でも同じ v3 フレームを受けられます（シリアル変換器不要）。
PC 側で生成した IMU（ジャイロ・加速度）を `IMU_SAMPLE` として送ると、Pro Controller のジャイロ入力として Switch に渡ります。
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
- PC 由来 IMU（ジャイロ・加速度）入力（`IMU_SAMPLE`）
- 無線時の USB-CDC 輸送（単一 UF2・起動時 profile 選択）
- 機種選択（`EMULATE_MODE`: Pro Controller / Joy-Con (L) / Joy-Con (R)）
- UART 経由でのコントローラ状態入力
- Poke-Controller Modified 旧版との互換モード(ビルド時定数変更)
- ホスト環境での単体テスト対応

## 動作状況

| 機能 | 実装 | 実機検証 |
|------|------|----------|
| USB HID | ✅ | ✅ |
| USB-CDC 無線輸送 | ✅ | — |
| Classic BT HID | ✅ | ✅ |
| BLE Wake Beacon 記録・再生 | ✅ | ✅ |
| 振動出力の振幅転送 | ✅ | ✅ |
| プレイヤー表示・本体色設定 | ✅ | ✅ |
| 色読戻し (`COLOR_GET`) | ✅ | — |
| Poke-Controller ASCII 行モード | ✅ | ❌ |
| NFC | ❌ | — |
| ジャイロ入力（PC 由来 IMU） | ✅ | ✅ |
| Joy-Con L/R | ✅ | ✅ |

ジャイロは PC 側で生成した `IMU_SAMPLE` を送る方式です。有線では Pro Controller（role0）のみ、無線では全 role に反映されます。

有線Joy-Con L/R に対応。
Switch実機で認識および操作を確認済み。

機種は `EMULATE_MODE`（0x38）で Pro Controller / Joy-Con (L) / Joy-Con (R) から選択します。

## 構成

```text
PC ──UART──────────────┐
                       ├──▶ Raspberry Pi Pico 2 W ──┬── USB HID ──────────────┬──▶ Nintendo Switch
PC ──USB CDC（無線起動時）┘                          └── Classic Bluetooth HID ─┘
```

入力データは PC から UART または USB-CDC 経由で Pico へ送信され、Pico が Nintendo Switch のコントローラとして振る舞います。

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

### 補足

- 有線プレイヤーランプ: SUB `0x30` 受信値を保存し、`0x31` で読戻し（`B0+id`）、`PLAYER_INFO` へ反映します。
- `PLAYER_INFO` flags bit2=`cap_saved`: 取込済み wake の保存有無を示します。詳細は `spec/protocol_v3.md` §5.8 を参照。
- `PLAYER_INFO` flags は bit3-4=gyro レンジ、bit5-6=accel レンジ、bit7=0x41 受信済みも示します。
- 同一 drain 内の `STATUS_REQ` 等は `FX` 要求を消去しません。
- `SEQ_GAP` は次の正常 SEQ 受信で解除されます（ドロップ数は保持）。
- 開発用 `BOOTSEL`: TYPE `0x37` / magic `0x5A`、約 500ms 後に USB BOOTSEL 再起動。Flash 書込みなし。
- `IMU_SAMPLE`（0x04, LEN12）: PC 側で生成した IMU 生値（6xint16LE）。200Hz 想定。100ms 無受信でジャイロ 0、500ms で静止値。
- `EMULATE_MODE`（0x38）: 0=Pro Controller / 1=Joy-Con (L) / 2=Joy-Con (R)。Flash 保存・約500ms 後の再起動で適用。
- `COLOR_GET`/`COLOR_INFO`（0x39/0x3A）: RAM 上の色 12B を読み戻します（Flash 書込なし）。
- `RECONNECT`（0x3B）: 無線時、拒否待機に入った再接続を明示的に再開します。Switch の受入を保証するものではありません。
- 無線起動時は USB-CDC でも同じ v3 フレームを送受信できます。data（interface 0/1）がプロトコル用、log（interface 2/3）はログ ASCII 用です。

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
- 無線起動時の USB-CDC 輸送（ボーレートなし、DTR/RTS 無視。COM は data と log の 2 本で、プロトコルは data 側を使う）

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

派生構成: `-DPOC_DATA_BAUD=115200`（115200bps 上限アダプタ）、`-DWIRED_DEFAULT=0`（無線起動）、`-DPOKECON_INPUT=1`（PokeCon ASCII 行モード）。詳細は `AGENTS.md`。

出力先:

- `build/` — ファームウェアビルド
- `build-host/` — ホストテスト
- `firmware/` — UF2 保管
- `log/` — シリアルログ

いずれも Git 管理対象外です。

## ホストテスト

Visual Studio Build Tools 環境が必要です。`vcvars64.bat` を実行したコマンドプロンプト上で実行してください。

現在のホスト試験は 17 本です（`imu_dummy` は `-DIMU_DUMMY=ON` の任意）。

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
