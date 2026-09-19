# switch-bcon → PokeCon Modified セッション引継ぎ（2026-09-18）

PokeController Modified 側の作業を別セッションで続けるための全量整理。
前提の真実源：`AGENTS.md`（ビルド手順・制約）、`spec/protocol_v3.md`（プロトコルSSOT、現行v4文面）。

## 1. リポジトリ状態（HEAD `9915acf`、tree clean）

直近5 commits（すべて本日・いずれも明示指示あり）：

| SHA | 内容 |
|---|---|
| `df00641` | serial BOOTSEL（T_BOOTSEL `0x37`/magic `0x5A`＋LOG `bootsel`行、開発用） |
| `2311c8d` | PokeCon入力モジュール＋host test（Commit A、本書§3） |
| `2a3057c` | rumble連鎖unpark＋T9 spec v4（Commit C） |
| `7cc4cc9` | baud B5/BREAK/ladder HW verdict（Commit B、docsのみ） |
| `9915acf` | 色/PLAYER_INFO HW確認＋0x23デコーダ（Commit D） |

- host testは6/6（protocol/usb/config/baud/pokecon/rumble）。実行は`vcvars64.bat`初期化済みcmd＋フルパス`ctest.exe -C Debug`（AGENTS.md）。
- FWビルドはフルパスcmake（`~/.pico-sdk/cmake/v4.3.4/bin/cmake.exe`）＋ninja。派生構成はtemp dirでbuildし`log/`へUF2保管。`build/`・`build-host/`・`log/`・`*.uf2`はgit管理外。
- `log/`直下の本セッション成果物：`COM3_2026_09_18.rumble_vib.txt`（採取本体）、`rumble_A2_extract.txt`（抽出表）、`COM3_2026_09_18.baud_rerun.txt`、`baud_ladder.txt`/`baud_ladder2.txt`、`COM3_2026_09_18.color_pi.txt`、`hci_dump_revert_plan.md`、`poc_send_023.patch`（適用済み）、`ulw-notepad.md`（作業台帳）。
- 台帳：`docs/history/2026-09-15-wdt/sdd/plan-a-progress.md`末尾にRumble-unpark／baud verdict／color verdictを追記済み。
- `.omo/`はuntrackedのまま残置（本セッションの産物ではない。触らないこと）。

## 2. デバイス現在状態（2026-09-18最終時点）

- 焼き済みFW：baudhunt無線版（`log/switch-bcon-baudhunt-wireless-1M-2a3057c.uf2`、SHA `58a1efa3…`）。`baud=0L`（115200 lock）、セッション生存を確認済み。
- Flash永続の注意：**本体色がミク・ターコイズ（`#39C5CF`）に書換え済み**（ユーザー要求。既定グレー系ではない）。色ベースライン確認時はCOLOR_SETで書き戻すこと。
- TLV（鍵・host・baud・色）は電源断・UF2焼替えを跨いで保持される。現セッションでSwitch自動再接続を反復確認済み。
- 配線：単一FTDIアダプタ（COM3）。最終形態は**UART1フル**（GP4/GP5両方）。分割形（RX←GP0＋TX→GP5）も検証済みで有効：両115200時のみ成立、DATA応答（STATUS/PONG/PLAYER_INFO）は見えなくなる。
- シリアル操作の定石（本セッションで全手順HOST側実行済み）：opencode `serial` MCP（open→write/wait_for/read→close。長時間待機は`inactivity_timeout`を時間単位に）。RPI-RP2へのUF2コピーはエクスプローラ相当操作（PowerShell `Copy-Item`）。BOOTSEL突入はDATAフレーム`AB 37 01 5A <SEQ> <CRC8>`またはLOG行`bootsel`（500ms後に再起動、RPI-RP2列挙）。

## 3. PokeCon task-17 の現状（本書の主題）

### 3.1 仕様（freeze済み、実装の唯一の根拠）

`docs/superpowers/specs/2026-09-16-pokecon-compat-design.md`

- wire形式：115200 8N1想定のASCII行 `<btn16> <hat> [<lx> <ly> [<rx> <ry>]]\r\n`＋単語行（v1は`end`のみ）。16進は大小・ゼロパディング不問。
- ボタン対応（wire bit LSB順→VIIPER）：0=RS flag、1=LS flag、2=Y、3=B、4=A、5=X、6=L、7=R、8=ZL、9=ZR、10=MINUS、11=PLUS、12=LCLICK、13=RCLICK、14=HOME、15=CAPTURE。
- HAT 0-8→十字ボタン（8=none）。wiki例`92 8 80 ff`のスティック部はtypoとして捨てる（表とparser実装を正とする）。
- LS/RS quirk（yqYo1互換）：LSのみ→LX/LY＝lx/ly、RSのみ→RX/RY＝lx/ly、両方→LX/LY＝lx/lyかつRX/RY＝rx/ry、なし→スティック保持。
- スティックはu8・中央`0x80`・Y下正で1:1コピー（`<<4`は既存ingestが処理）。`end`→全解放。200ms timeout-neutralはv1維持（削除しない）。
- 対象外：MCU語・キーボード系・Date/Year。v3バイナリ既定は壊さない。
- **訂正反映済み**：PokeConアプリ側baudは可変（spec §1.1の115200固定前提は改訂済み扱い）。baud hunt＋BREAK儀式（`--break_`約100ms→baud設定→stream）で追従する。

### 3.2 実装済み（desk検証のみ、HW未検証）

- `src/pokecon.c`／`src/pokecon.h`：Pico/BTstack非依存の純粋写像（`pack.c`パターン）。`pokecon_parse_line()`（行→`ctrl_state_t`）＋`pokecon_linebuf_t`（64B cap、過長破棄、`\\r`非終端、部分行保持）。
- `tests/host/test_pokecon.c`（CTest `pokecon`）：POKE-HAPPY-01／EDGE-02（hat 0-8＋`end`）／EDGE-03（RS配送＋不正・過長・部分行）。host 6/6 GREEN確認済み。
- Core1配線（`src/main.c`の`#if POKECON_INPUT`群）：行バッファ→parse→`g_s.state`へ供給し、BT/USB/pack/timeout-neutralを流用。huntはpokecon modeで確定扱い（固定レート・TX開放）。
- ビルドflag `POKECON_INPUT=0/1`（既定0・自動判別なし）。両値でFW exit 0確認済み（UF2 SHAはCommit A報告参照）。
- TLV mode flagはv1.1送り（必要なら`TAG_POKECON`新設＋`FX_WIRED_MODE`型の再起動適用）。

### 3.3 PokeConセッションでの残作業（HW）

1. `POKECON_INPUT=1`＋115200（derated相当）でbuild→BOOTSELで焼替え。
2. PokeCon Modifiedのポートをdata-UART COM（115200）に向け、仮想コントローラ→Switch入力テスト画面で押下確認→A連打スクリプト完走。
3. 問題が出たら：(a) timeout-neutral延長（pokecon mode限定・削除禁止）、(b) hunt有効/無効の扱い見直し（未決のまま）。
4. PokeCon接続儀式：`--break_`→baud設定→stream（本セッションでBREAK再確定をHW実証済み）。

## 4. 他領域の確定事項（PokeCon作業が壊さないための境界）

- **rumble送出路は開通済み**（`src/proto/rumble.h`復号＋`ACT_SEND_RUMBLE`＋spec v4）。実測ベクタ：neutral 3形、非ゼロ L-mid／R-mid／両mid（`00 45 40 52`系）／両strong（`80 00 60 92`系）。LF正規化は観測最大`0x92`基準（より大音量の採取で再正規化要）。USB側は計数のみ。
- **baud hunt＋BAUD_SET（B §3）はHW完証**（B5 sweep捕捉・BREAK再確定・ladder全4 rung ADOPTED）。`locked until reboot OR BREAK`の意味論を変えないこと。
- **BOOTSEL（0x37/0x5A）**は開発用常設機能。PokeCon用UF2の焼替えにも使うこと。
- **色**：現デバイス赤→ミク青ではなくターコイズ（§2）。`COLOR_SET`（0x32）＋登録解除→再接続の再読込はHW実証済み（C-1/C-3/C-4/C-6 PASS）。`PLAYER_INFO`（0x23 lamp=0x01/flags=0x03、P-1/P-2/P-5 PASS）。未検証残：P-3/P-4 flag遷移、P-6再接続クリア（ledgerに記録済み）。
- **秘密鍵バイトの記録禁止**（`hci_dump`はTemp診断のみ）。peer BD_ADDRは可。

## 5. 参考ポインタ

- 台帳時系列：`docs/history/2026-09-15-wdt/sdd/plan-a-progress.md`（末尾3件が本セッション分）
- 前回引継ぎ：`docs/handoff_20260916.md`（WDT調査の全量。Phase-3・Plan Aの来歴はこちら）
- 設計3点：`docs/superpowers/specs/2026-09-16-{pokecon-compat-design,uart-baud-hunt-design,rumble-capture-design}.md`＋`2026-09-16-joycon-personality-design.md`（Joy-Conは実機値待ちのまま）
- PC送信器：`src/poc_dualcore/poc_send.py`（`--hello/--ping/--sweep12/--hold12/--break_/--probe-ladder/--bootsel`＋0x23デコーダ内蔵）
- 使い捨てHWスクリプト（TEMP外・repo外）：`$env:TEMP\\opencode\\uart1_verify.py`、`pi_check.py`、`color_blue.py`、`color_miku.py`、`baud_ladder.py`、`baud_ladder2.py`（必要なら再利用・削除可）
