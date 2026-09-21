# AGENTS.md

SSOT is `spec/protocol_v3.md` (`PROTO_VER=4`). If spec conflicts with docs/history, trust spec + `src/proto/*`.

## Build

Board is `pico2_w` fixed (Pico SDK 2.3.0). `build/`, `build-host/`, `log/` (serial transcripts), `firmware/` (UF2 binaries), `*.uf2` are git-ignored — never commit them.

Firmware (PowerShell; `cmake` is NOT on PATH, use full path):
```powershell
$cmake = Join-Path $env:USERPROFILE '.pico-sdk/cmake/v4.3.4/bin/cmake.exe'
$ninja = Join-Path $env:USERPROFILE '.pico-sdk/ninja/v1.13.2/ninja.exe'
& $cmake -S . -B build -G Ninja -DCMAKE_MAKE_PROGRAM:FILEPATH=$ninja
& $cmake --build build
```
Variant configs: `-DPOC_DATA_BAUD=115200` (derated for 115200-bps adapters; default 1Mbps per spec), `-DWIRED_DEFAULT=0` (wireless boot; default 1), `-DPOKECON_INPUT=1` (PokeCon ASCII-line mode; default 0 = v3 binary, no auto-detect). Build variants in a temp dir (don't dirty `build/`), copy the UF2 to `log/`.

Host unit tests — must run from a `vcvars64.bat`-initialized cmd (plain PowerShell has no compiler). `ctest.exe` must be called by full path from the same dir as `cmake.exe`:
```
cmake -S tests/host -B build-host
cmake --build build-host --config Debug
ctest --test-dir build-host -V
```
Single test: `ctest --test-dir build-host -R <protocol|usb|config|baud|pokecon|rumble|personality> -V`. MSVC needs `/utf-8` (Japanese comments, C4819) — already set in `tests/host/CMakeLists.txt`, don't remove.
Targets: `switch-bcon` is the integrated FW; `poc_dualcore` is the PoC-only predecessor (don't extend it).

## Architecture

- `src/main.c`: the integrated FW. Core1 = UART1 DMA ring (16KB) + v3 parser + mutex push only — no printf/BT/CYW43/Flash. Core0 = BTstack run loop + TinyUSB + CYW43, 1ms `poll_tick` (inbox drain → `dispatch` → FX exec → outbox flush → pack → USB). Wired boot runs `wired_loop()` instead and never brings up CYW43/BT.
- `src/proto/` (`protocol.c`, `pack.c`, `spi.c`, `dispatch.c`): Pico/BTstack-independent, host-testable. `dispatch.h` is pure: returns `FX_*` effects + `ACT_*` outbox; all HW side effects (Flash/TLV/BT/UART-TX) live in `main.c`. Keep it that way. Append-only to `FX_*`/`ACT_*` enums (renumbering hazard).
- Identity/personality: `EMULATE_MODE` (`0x38`, role 0=ProCon/1=JoyL/2=JoyR) is Flash-persisted, reboot-applied like `WIRED_MODE`. Line buttons stay transport-independent u32; role mapping lives in Pico-side pack (`src/bt/personality.c` `joy_pack_btn3`, mirrored in `src/usb/usb_hid.c`). Joy-Con L/R values are provisional (real-hardware values pending) — don't treat PID/strings/SPI blanks as measured truth.
- `src/usb/` (TinyUSB wired ProCon) vs `src/bt/` (Classic BT + BLE wake capture): never include `tusb.h` and `btstack.h` in the same TU — `hid_report_type_t` double-defines (see `docs/poc_dualcore_result.md`).
- UART wiring: log = UART0 GP0/1 @115200; data = UART1 GP4/5 @1Mbps 8N1, no flow control. FTDI recommended, latency timer 1ms.
- Protocol basics: binary-first (v3 frames default; PokeCon ASCII-line mode is build-flag selected), frame `[SYNC=0xAB][TYPE][LEN][PAYLOAD][SEQ][CRC8/SMBUS over TYPE..SEQ]`; STATE = `0x01` LEN8 = BTN u32-LE (VIIPER order, 22 bits, reserved bits send 0 / receive-ignore) + 4 stick bytes, no HAT; SEQ counters are per-direction mod256. PC sender (`src/poc_dualcore/poc_send.py`, PoC-only) does one `write()` per frame.

## Gotchas

- Wired/wireless: `WIRED_MODE` (`0x34`) is Flash-persisted and applied by reboot (~500ms delay). Wired boot keeps radio down — CYW43 powered is known to break Switch 2 dock USB enumeration. `CAPTURE_START`/`BEACON_START` are wireless-only; wired requests are rejected (`0x10`/`0x11`).
- Flash: all writes via `flash_safe_execute` (Core1 must call `flash_safe_execute_core_init()`; boot log should show `victim=1`); TLV reads only before Core1 launch. Flash writes during active BT have caused ~2s-delayed WDT resets (9/9 in `docs/wdt_phenomena_brief_20260915.md`) — don't add new write paths in BT callbacks; defer/quiesce and verify on HW.
- Secrets: never print LTK/link-key bytes to logs/docs (`hci_dump` is temp-diagnosis only). Peer BD_ADDR is OK.
- `C:\Users\moilo\pico-wakecon` is reference-only, do not modify.
- History/diagnostics: `docs/history/2026-09-15-wdt/README.md` is the index for BT/WDT trials (UF2 + `log/COM3_*.txt` correspondence); `docs/poc_dualcore_result.md` records the dual-core adoption evidence.
- HW talk: `opencode.json` provides `serial` MCP (`uvx --from pyserial-mcp serial-mcp`); `src/poc_dualcore/poc_send.py` has `--hello/--ping/--sweep/--hold12/--sweep12/--break_/--probe-ladder/--bootsel/--key-delete/--emulate` modes (`--emulate 0|1|2` = ProCon/JoyL/JoyR). Preferred flash flow is button-less: `poc_send.py <dataCOM> --bootsel` (T_BOOTSEL magic → USB BOOTSEL reboot, ~500ms+, needs working data UART) then copy UF2 to the mounted drive; fall back to the physical BOOTSEL button when UART is down.
