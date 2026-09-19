# switch-bcon

BCON = Bridge Controller。Raspberry Pi Pico 2 W 用・バイナリ主体・最速志向の Switch 1 Pro Controller エミュレータ
（PC-UART→Switchブリッジ）。
（有線USB公式プロコン＋Classic BT＋BLE wakeビーコン取込再生）。

* 線路：PC →(UART)→ Pico →(USB-HID / Classic BT)→ Switch 1。Switch 2 BLE入力は対象外。
* プロトコル：`spec/protocol_v3.md` が単一の真実源（SSOT）。`PROTO_VER=4`。
* 状態送信：ボタンu32-LE（VIIPER順・22bit使用）＋スティック（LEN8=u8x4／LEN12=u16LEx4の12bit拡張）。HATフィールドなし（十字キーはボタン）。
* UART：log=UART0 GP0/1 @115200・data=UART1 GP4/5（既定1Mbps 8N1、フロー制御なし）。data側はbaud hunt自動追従（`BAUD_SET`合意切替・BREAK再探索）＋PokeCon Modified互換のASCII行モード（ビルド時選択・HW未検証）に対応。開発用にシリアルBOOTSEL突入あり。
* USB：任天堂 `057E:2009`＋純正写し記述子（`bInterval 8`実機写し）。HORIPAD名乗りはしない。
* 振動・表示系：Switch振動出力の振幅転送（RUMBLE送出）、プレイヤー表示（PLAYER_INFO）、本体色設定（COLOR_SET）に対応。いずれもHW実証済み。
* Joy-Con：L/Rパーソナリティ対応を予定（設計書 `docs/superpowers/specs/2026-09-16-joycon-personality-design.md` あり・実機値待ち）。
* 秘密（LTK/IRK/AES鍵）をログに出さない。
* ビルド成果物は `build/`（増分）・`build-host/`（hostテスト）・派生構成のtemp dir。焼き用UF2は `firmware/` に保管。いずれもgit管理外（`log/`はシリアル採取文のみ）。

## 配置

```
spec/                 プロトコル仕様（SSOT）
src/proto/            CRC8・パーサ・フレーム生成＋dispatch（Pico/BTstack非依存・host test可）
src/pokecon.*         PokeCon Modified互換のASCII行入力（同上・ビルド時選択）
src/usb/              公式ProCon USB（有線HID＋記述子）
src/bt/               Classic BT＋BLE wake（link/hid/cap/spi/store）＋振動出力の振幅転送
src/main.c            統合ファーム（dual-core：Core1=UART取込／Core0=BT・USB・1ms poll）
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

* 本リポジトリで独自に作成したソースコードには、特記のない限り MIT License（`LICENSE`）を適用します。
* 本プロジェクトは Raspberry Pi Pico SDK（2.3.0、外部依存）を通じて提供される BTstack などの第三者コンポーネントに依存します。これらは MIT の対象外であり、それぞれの条件（`LICENSES/`）が適用されます。
* Bluetoothファームウェアの対象ハードウェアは Raspberry Pi Pico 2 W です。BTstack は Raspberry Pi 向け補足ライセンスの対象範囲内で使用することを意図しています。
* 本プロジェクトは個人による非商用のオープンソースプロジェクトです。現時点では販売・有償サポート・書込済みハードの販売・受託開発は行っていません。
* 詳細は `NOTICE.md` および `LICENSES/` を確認してください。

## 免責・非公式プロジェクトについて

本プロジェクトは個人が開発する非公式のオープンソースプロジェクトであり、Nintendo Switch との互換性を検証するコントローラーエミュレーションです。任天堂株式会社またはその関連会社との提携、後援、承認その他の関係はありません。Nintendo Switch および Pro Controller などの名称は、それぞれの権利者に帰属します。
