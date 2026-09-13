# pico-bcon

Raspberry Pi Pico 2 W 用・バイナリ専用・最速志向の Switch 1 Pro Controller エミュレータ
（有線USB公式プロコン＋Classic BT＋BLE wakeビーコン取込再生）。新規作成リポジトリ。

* 線路：PC →(UART)→ Pico →(USB-HID / Classic BT)→ Switch 1。Switch 2 BLE入力は対象外。
* プロトコル：`spec/protocol_v3.md` が単一の真実源（SSOT）。`PROTO_VER=3`。
* 状態送信：ボタンu32-LE（VIIPER順・22bit使用）＋スティック4B＝LEN8。HATフィールドなし（十字キーはボタン）。
* UART：バイナリ専用、既定 1Mbps 8N1、既定ピン GP4/5（UART1、ビルド時GP0/1選択可）。フロー制御なし既定。
* USB：任天堂 `057E:2009`＋純正写し記述子（`bInterval 8`実機写し）。HORIPAD名乗りはしない。
* 秘密（LTK/IRK/AES鍵）をログに出さない。
* ビルド成果物は `build/`（増分）と `build-verify/`（クリーン確認）。どちらもgit管理外。

## 配置

```
spec/                 プロトコル仕様（SSOT）
src/proto/            CRC8・パーサ・フレーム生成（Pico/BTstack非依存・host test可）
src/usb/              公式ProCon USB（移植予定：wakecon usb_hid/usb_descriptors相当）
src/bt/               Classic BT＋BLE wake（移植予定：wakecon link/hid/cap/spi/store相当）
src/main.c            統合ファーム（移植予定）
tests/host/           host単体テスト（CTest）
docs/superpowers/plans/ 実装計画
```

## ビルド（Picoファーム）

```powershell
$cmake = Join-Path $env:USERPROFILE '.pico-sdk/cmake/v4.3.4/bin/cmake.exe'
$ninja = Join-Path $env:USERPROFILE '.pico-sdk/ninja/v1.13.2/ninja.exe'
& $cmake -S . -B build -G Ninja -DCMAKE_MAKE_PROGRAM:FILEPATH=$ninja
& $cmake --build build
```

board は `pico2_w` 固定。

## hostテスト

`vcvars64.bat` 経由の cmd で実行（素の PowerShell にコンパイラなし）。
`ctest.exe` は cmake と同ディレクトリのフルパスで呼ぶ。詳細は `tests/host/CMakeLists.txt`。

## ライセンス

* 本リポジトリのコード：MIT License（予定）。
* ビルド時に Pico SDK 経由でリンクされる BTstack は BlueKitchen の独自許諾
  （非商用に限り無償）。バイナリ配布・商用利用は別途確認のこと。
* USB/HID記述子は実機写し・2wiCC等の実働値参照を含む。各ファイルに帰属を記載する。
