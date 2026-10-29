# Build-and-Test — ビルドと試験

## 前提

ボードは `pico2_w` 固定、Pico SDK 2.3.0 である (`AGENTS.md:7`)。`build/`、`build-host/`、`firmware/`、`log/`、`*.uf2` は git 管理外であり、コミットしない (`AGENTS.md:7`)。

`cmake` は PATH にないためフルパスで呼ぶ。MSVC が要るホスト試験は `vcvars64.bat` 初期化済み cmd から行う（素の PowerShell にはコンパイラがない）(`AGENTS.md:9,18`)。

## ファームウェアのビルド（PowerShell）

```powershell
$cmake = Join-Path $env:USERPROFILE '.pico-sdk/cmake/v4.3.4/bin/cmake.exe'
$ninja = Join-Path $env:USERPROFILE '.pico-sdk/ninja/v1.13.2/ninja.exe'
& $cmake -S . -B build -G Ninja -DCMAKE_MAKE_PROGRAM:FILEPATH=$ninja
& $cmake --build build
```

上記は `AGENTS.md:9-15` の正準手順である。統合 FW のソース一覧は `CMakeLists.txt` の `add_executable(switch-bcon ...)` が正である。`src/proto/baud.c`、`src/usb/usb_cdc.c`、`src/bt/hid_pack.c`、`src/bt/personality.c`、`src/bt/reconnect_policy.c` を含む。BTstack Classic/BLE/CYW43 と TinyUSB をリンクする。`POC_DATA_BAUD`、`WIRED_DEFAULT`、`POKECON_INPUT`、`IMU_DUMMY`、`JOY_SPI_CAL_ROT`、`JOY_USB_PID_2007` は定義で渡す。

## 派生構成 (baudと無線既定)

- `-DPOC_DATA_BAUD=115200` は 115200bps 上限アダプタ向けの derated 構成である。既定は仕様通り 1Mbps (`AGENTS.md:16`, `src/main.c:55-57`)。データ UART の既定ピン・速度の定義は `src/main.c:54-59` にある
- `-DWIRED_DEFAULT=0` は無線起動の試験用である。既定は 1（有線起動）(`AGENTS.md:16`)。未保存時の起動モードとして `store_wired_load_def(WIRED_DEFAULT != 0)` で使う (`src/main.c:1421`)
- 派生構成は `build/` を汚さないよう temp ディレクトリに別構成し、UF2 を `log/` へコピーする (`AGENTS.md:16`)。無線版の建て方の具体例（temp `bcon-wireless`、derated＋無線既定＋BUMP 等）は引継ぎに記録されている (`docs/handoff_bt_20260914.md:23`)
- `-DPOKECON_INPUT=1` は PokeCon Modified ASCII 行モード。`-DPOC_DATA_BAUD=115200` と組で使う。
- `-DJOY_SPI_CAL_ROT=0|1|4` は role2 の SPI 校正回転（既定1=Y,Z反転）。
- `-DJOY_USB_PID_2007=1` は診断用で role2 の USB PID を 0x2007 にする。
- `-DIMU_DUMMY=1` は Phase 0 のダミー IMU 実験ビルド。

## ホスト単体試験（vcvars64 済み cmd）

```text
cmake -S tests/host -B build-host
cmake --build build-host --config Debug
ctest --test-dir build-host -C Debug -V
```

上記は `AGENTS.md:18-23` の正準手順である。`ctest.exe` は cmake と同じディレクトリのものをフルパスで呼ぶ。単体実行は `ctest --test-dir build-host -C Debug -R <protocol|usb|config> -V` である。MSVC には `/utf-8` が要る（日本語コメント、C4819 対策）。`tests/host/CMakeLists.txt` に設定済みであり、外さないこと (`AGENTS.md:24`)。

試験は次の17本立てである（`imu_dummy` は `-DIMU_DUMMY=ON` の任意）。

- `protocol`（CRC・parser・`frame_build`）
- `usb`（pack・SPI・0x30 IMU ゲート等）
- `joy_imu_axis`（Joy role の IMU 軸変換）
- `config`（dispatch の HELLO/PING/CONFIG→FX 写像・outbox・STATUS 組立・PLAYER_INFO 遷移・RUMBLE 遷移）
- `baud`（レート表・sweep・lock）
- `pokecon`（PokeCon 行写像・quirk・不正行棄却）
- `rumble`（復号・dispatch）
- `personality` / `joy_scopeA` / `joy_scopeB`（Joy-Con 擬態・identity・kick81）
- `wired_player` / `player_capsaved`（有線プレイヤー・ランプ）
- `color_getinfo`（COLOR_GET→COLOR_INFO）
- `reconnect_policy`（能動再接続の page 予算・discoverable 判定）
- `usb_cdc`（CDC 輸送の parser/応答）
- `bt_input`（BT 0x30 の 14B/50B 組み立て）
- `virt_tick`（仮想 tick によるポーリング）

> 注: 正は `tests/host/CMakeLists.txt` の `add_test` 一覧である。増減するときは CMake を直したうえで本表を同期すること。現行の登録は 17 件であり、合否は `ctest` の実行結果で確認する。

## UF2 / log の運用規約

- UF2 と生ログ（`log/COM3_2026_09_*.txt`）を使い分ける。UF2 は `firmware/`、生ログは `log/` に置く。いずれも git 管理外である (`docs/history/2026-09-15-wdt/README.md:16-20`)。
- 対象 UF2 は `firmware/switch-bcon-wireless-test.uf2`、`firmware/switch-bcon-ab{1..5}-*.uf2`、`firmware/switch-bcon-w{0,1,3,4,5,6,7}-*.uf2`、`firmware/switch-bcon-w8-epoch.uf2`、`firmware/switch-bcon-phase3-test.uf2` 等である。
- 注: 過去の記録文書に `log/*.uf2` とあるのは `firmware/` の誤記である。実ファイルは `firmware/` にある。
- UF2 とログの対応表は `docs/history/2026-09-15-wdt/trial-history.md` が正である。本文書の日付別表と対応付けて読む (`docs/history/2026-09-15-wdt/README.md:18-20`)
- ログ取得時は起動 banner（`=== switch-bcon ===`）から取り逃がさないこと。W7 で先頭 banner 欠落の前例がある (`docs/history/2026-09-15-wdt/hw-batch-2026-09-15.md:7`)
- 無線版 UF2 の具体例として、4:24 版は送信復帰＋TLV 停止＋起動時ワイプ＋hci_dump＋reporter＋MSPLIM＋SCR＋heartbeat 入りで SNIFF 無効継続の診断用一時措置であった (`docs/handoff_bt_20260914.md:22`)

## HW 通信の道具

`opencode.json` が `serial` MCP（`uvx --from pyserial-mcp serial-mcp`）を提供する。排他資源であり、`serial_open()` したら作業終了前に `serial_close()` で閉じること。`serial_execute()` は自前で開閉するため別途 close 不要である。`src/poc_dualcore/poc_send.py` の `--hello`/`--ping`/`--sweep` は HELLO/PING/ボタン確認用である (`AGENTS.md:41`)。

## ビルドしてはいけないもの・触ってはいけないもの

- `C:\Users\moilo\pico-wakecon` は参照専用であり、改変もビルド組込もしない (`docs/superpowers/plans/2026-09-15-plan-a-telemetry-v4.md:16`)。破ると参照専用ツリーを汚す。
- SDK・BTstack ソースは改変対象外である。計装は自前層のみに行う (`docs/history/2026-09-15-wdt/verification-status.md:115-117`)。破ると外部依存の更新追従が壊れる。
- commit・PR・ブランチ操作は明示指示があるまで行わない。本 Wiki 執筆もコミットしない (`docs/superpowers/plans/2026-09-15-plan-a-telemetry-v4.md:15`)。破ると未指示の履歴改変になる。
