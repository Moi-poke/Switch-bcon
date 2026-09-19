# Build-and-Test — ビルドと試験

## 前提

ボードは `pico2_w` 固定、Pico SDK 2.3.0 である (`AGENTS.md:7`)。`build/`、`build-host/`、`log/`、`*.uf2` は git 管理外であり、コミットしない (`AGENTS.md:7`)。`cmake` は PATH にないためフルパスで呼ぶ。MSVC が要るホスト試験は `vcvars64.bat` 初期化済み cmd から行う（素の PowerShell にはコンパイラがない）(`AGENTS.md:9,18`)。

## ファームウェアのビルド（PowerShell）

```powershell
$cmake = Join-Path $env:USERPROFILE '.pico-sdk/cmake/v4.3.4/bin/cmake.exe'
$ninja = Join-Path $env:USERPROFILE '.pico-sdk/ninja/v1.13.2/ninja.exe'
& $cmake -S . -B build -G Ninja -DCMAKE_MAKE_PROGRAM:FILEPATH=$ninja
& $cmake --build build
```

上記は `AGENTS.md:9-15` の正準手順である。統合 FW のターゲット `switch-bcon` は `src/main.c`、`src/proto/`（`protocol.c`、`pack.c`、`spi.c`、`dispatch.c`）、`src/usb/` 3 件、`src/bt/` 7 件（`hid.c`、`link.c`、`link_conn.c`、`link_cap.c`、`link_beacon.c`、`cap.c`、`store.c`）から構成される。BTstack Classic/BLE/CYW43 と TinyUSB をリンクし、`POC_DATA_BAUD` と `WIRED_DEFAULT` を定義で渡す。

## 派生構成（`-DPOC_DATA_BAUD` / `-DWIRED_DEFAULT`）

- `-DPOC_DATA_BAUD=115200` は 115200bps 上限アダプタ向けの derated 構成である。既定は仕様通り 1Mbps (`AGENTS.md:16`, `src/main.c:55-57`)。データ UART の既定ピン・速度の定義は `src/main.c:54-59` にある
- `-DWIRED_DEFAULT=0` は無線起動の試験用である。既定は 1（有線起動）(`AGENTS.md:16`)。未保存時の起動モードとして `store_wired_load_def(WIRED_DEFAULT != 0)` で使う (`src/main.c:1421`)
- 派生構成は `build/` を汚さないよう temp ディレクトリに別構成し、UF2 を `log/` へコピーする (`AGENTS.md:16`)。無線版の建て方の具体例（temp `bcon-wireless`、derated＋無線既定＋BUMP 等）は引継ぎに記録されている (`docs/handoff_bt_20260914.md:23`)

## ホスト単体試験（vcvars64 済み cmd）

```text
cmake -S tests/host -B build-host
cmake --build build-host --config Debug
ctest --test-dir build-host -V
```

上記は `AGENTS.md:18-23` の正準手順である。`ctest.exe` は cmake と同じディレクトリのものをフルパスで呼ぶ。単体実行は `ctest --test-dir build-host -R <protocol|usb|config> -V` である。MSVC には `/utf-8` が要る（日本語コメント、C4819 対策）。`tests/host/CMakeLists.txt` に設定済みであり、外さないこと (`AGENTS.md:24`)。

試験の構成は 6 本立てである。`protocol`（CRC・parser・`frame_build`）、`usb`（pack・SPI 等）、`config`（dispatch の HELLO/PING/CONFIG→FX 写像・outbox・STATUS 組立・PLAYER_INFO 遷移・RUMBLE 遷移）、`baud`（レート表・sweep・lock）、`pokecon`（PokeCon 行写像・quirk・不正行棄却）、`rumble`（復号・dispatch）である。現行 6/6 ALL PASS が全 sub-project の gate である (`docs/superpowers/specs/2026-09-15-uart-features-design.md:31,58-59`)。

## UF2 / log の運用規約

- UF2（`switch-bcon-wireless-test.uf2`、`switch-bcon-ab{1..5}-*.uf2`、`switch-bcon-w{0,1,3,4,5,6,7}-*.uf2`、`switch-bcon-w8-epoch.uf2`、`switch-bcon-phase3-test.uf2` 等）と生ログ（`COM3_2026_09_*.txt`）は `log/` に置く。git 管理外である (`docs/history/2026-09-15-wdt/README.md:16-20`)
- UF2 とログの対応表は `docs/history/2026-09-15-wdt/trial-history.md` が正である。本文書の日付別表と対応付けて読む (`docs/history/2026-09-15-wdt/README.md:18-20`)
- ログ取得時は起動 banner（`=== switch-bcon ===`）から取り逃がさないこと。W7 で先頭 banner 欠落の前例がある (`docs/history/2026-09-15-wdt/hw-batch-2026-09-15.md:7`)
- 無線版 UF2 の具体例として、4:24 版は送信復帰＋TLV 停止＋起動時ワイプ＋hci_dump＋reporter＋MSPLIM＋SCR＋heartbeat 入りで SNIFF 無効継続の診断用一時措置であった (`docs/handoff_bt_20260914.md:22`)

## HW 通信の道具

`opencode.json` が `serial` MCP（`uvx --from pyserial-mcp serial-mcp`）を提供する。排他資源であり、`serial_open()` したら作業終了前に `serial_close()` で閉じること。`serial_execute()` は自前で開閉するため別途 close 不要である。`src/poc_dualcore/poc_send.py` の `--hello`/`--ping`/`--sweep` は HELLO/PING/ボタン確認用である (`AGENTS.md:41`)。

## ビルドしてはいけないもの・触ってはいけないもの

- `C:\Users\moilo\pico-wakecon` は参照専用であり、改変もビルド組込もしない (`docs/superpowers/plans/2026-09-15-plan-a-telemetry-v4.md:16`)
- SDK・BTstack ソースは改変対象外である。計装は自前層のみに行う (`docs/history/2026-09-15-wdt/verification-status.md:115-117`)
- commit・PR・ブランチ操作は明示指示があるまで行わない。本 Wiki 執筆もコミットしない (`docs/superpowers/plans/2026-09-15-plan-a-telemetry-v4.md:15`)
