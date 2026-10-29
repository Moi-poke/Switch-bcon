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
**Non-ASCII worktree path breaks the firmware build** (`arm-none-eabi-objdump`/`cd /D` on the mojibake path fails at step 1). Workaround: map an ASCII drive and build from there — `subst X: <worktree>`, then `cd /X` and run the same cmake commands; `subst X: /D` when done. Host tests (`tests/host`) are unaffected.

Variant configs: `-DPOC_DATA_BAUD=115200` (derated for 115200-bps adapters; default 1Mbps per spec), `-DWIRED_DEFAULT=0` (wireless boot; default 1), `-DPOKECON_INPUT=1` (PokeCon ASCII-line mode; default 0 = v3 binary, no auto-detect), `-DJOY_SPI_CAL_ROT=0|1|4` (role2 SPI cal rotation; default 1=YZ), `-DJOY_USB_PID_2007=1` (diagnostic role2 USB PID 0x2007), `-DIMU_DUMMY=1` (Phase 0 dummy IMU). Build variants in a temp dir (don't dirty `build/`), copy the UF2 to `log/`.

Host unit tests — must run from a `vcvars64.bat`-initialized cmd (plain PowerShell has no compiler). `ctest.exe` must be called by full path from the same dir as `cmake.exe`:
```
cmake -S tests/host -B build-host
cmake --build build-host --config Debug
ctest --test-dir build-host -C Debug -V
```
Single test: `ctest --test-dir build-host -C Debug -R <protocol|usb|joy_imu_axis|usb_cdc|bt_input|virt_tick|config|baud|pokecon|rumble|personality|joy_scopeA|joy_scopeB|wired_player|player_capsaved|color_getinfo|reconnect_policy> -V`. The authoritative list is the `add_test` set in `tests/host/CMakeLists.txt` (17 active; `imu_dummy` is opt-in via `-DIMU_DUMMY=ON`) — keep this line in sync when adding one. MSVC needs `/utf-8` (Japanese comments, C4819) — already set in `tests/host/CMakeLists.txt`, don't remove.
Targets: `switch-bcon` is the integrated FW; `poc_dualcore` is the PoC-only predecessor (don't extend it).

## Architecture

- `src/main.c`: the integrated FW. Core1 = UART1 DMA ring (16KB) + v3 parser + live STATE/IMU apply + short mutex push only — no printf/BT/CYW43/Flash. Core0 = BTstack run loop + TinyUSB + CYW43, 1ms `poll_tick` (CDC pump → inbox drain → `dispatch` → FX exec → outbox flush → pack → IMU timeout → USB/BT poll). Wired boot runs `wired_loop()` instead and never brings up CYW43/BT; wireless boot selects USB CDC-only (`usb_cdc_set_profile(false)`) — two CDC functions, itf0/1=data (v3 frames), itf2/3=log (`probe_line` ASCII), USB ID `2E8A:000A` — while wired selects HID-only (`057E:2009`).
- `src/proto/` (`protocol.c`, `baud.c`, `pack.c`, `spi.c`, `dispatch.c`): Pico/BTstack-independent, host-testable. `dispatch.h` is pure: returns `FX_*` effects + `ACT_*` outbox; all HW side effects (Flash/TLV/BT/UART-TX) live in `main.c`. Keep it that way. Append-only to `FX_*`/`ACT_*` enums (renumbering hazard).
- Identity/personality: `EMULATE_MODE` (`0x38`, role 0=ProCon/1=JoyL/2=JoyR) is Flash-persisted (`BCEM`), reboot-applied like `WIRED_MODE`. Role is resolved once at boot; USB is Plan A fixed `057E:2009`/`Pro Controller` for all roles (`JOY_USB_PID_2007=1` makes role2 0x2007 for diagnostics), BT uses role GAP name/CoD/dev_type/MAC (`PERSONALITY_TABLE`). Line buttons stay transport-independent u32; role mapping lives in Pico-side pack (`src/bt/personality.c` `joy_pack_btn3`, mirrored in `src/usb/usb_hid.c`). Joy-Con L/R values are provisional (real-hardware values pending) — don't treat PID/strings/SPI blanks as measured truth.
- `src/usb/` (TinyUSB wired ProCon) vs `src/bt/` (Classic BT + BLE wake capture): never include `tusb.h` and `btstack.h` in the same TU — `hid_report_type_t` double-defines (see `docs/poc_dualcore_result.md`).
- UART wiring: log = UART0 GP0/1 @115200; data = UART1 GP4/5 @1Mbps 8N1, no flow control. Wireless boot also accepts v3 frames over USB-CDC (data port = itf0/1, no baud, DTR/RTS ignored; log port = itf2/3); wired boot is HID-only. FTDI recommended, latency timer 1ms.
- Protocol basics: binary-first (v3 frames default; PokeCon ASCII-line mode is build-flag selected), frame `[SYNC=0xAB][TYPE][LEN][PAYLOAD][SEQ][CRC8/SMBUS over TYPE..SEQ]`; STATE = `0x01` LEN8 = BTN u32-LE (VIIPER order, 22 bits, reserved bits send 0 / receive-ignore) + 4 stick bytes, no HAT; SEQ counters are per-direction mod256. Additional frames: `IMU_SAMPLE` (0x04 LEN12), `EMULATE_MODE` (0x38), `COLOR_GET`/`COLOR_INFO` (0x39/0x3A), `RECONNECT` (0x3B); PLAYER_INFO flags carry 0x41 sensitivity bits 3-7. PC sender (`src/poc_dualcore/poc_send.py`, PoC-only) does one `write()` per frame.

## Gotchas

- Wired/wireless: `WIRED_MODE` (`0x34`) is Flash-persisted and applied by reboot (~500ms delay). USB profile is exclusive: wired=HID-only (`057E:2009`), wireless=CDC-only (`2E8A:000A`, itf0/1=data frames, itf2/3=log ASCII); select at boot, reboot to change, and raise the wireless USB after BTstack init so enumeration completes. `EMULATE_MODE` is also Flash-persisted and reboot-applied (~500ms). Wired boot keeps radio down — CYW43 powered is known to break Switch 2 dock USB enumeration. `CAPTURE_START`/`BEACON_START` are wireless-only; wired requests are rejected (`0x10`/`0x11`).
- Flash: all writes via `flash_safe_execute` (Core1 must call `flash_safe_execute_core_init()`; boot log should show `victim=1`); TLV reads only before Core1 launch. Flash writes during active BT have caused ~2s-delayed WDT resets (9/9 in `docs/wdt_phenomena_brief_20260915.md`) — don't add new write paths in BT callbacks; defer/quiesce and verify on HW.
- Secrets: never print LTK/link-key bytes to logs/docs (`hci_dump` is temp-diagnosis only). Peer BD_ADDR is OK.
- `C:\Users\moilo\pico-wakecon` is reference-only, do not modify.
- History/diagnostics: `docs/history/2026-09-15-wdt/README.md` is the index for BT/WDT trials (UF2 + `log/COM3_*.txt` correspondence); `docs/poc_dualcore_result.md` records the dual-core adoption evidence.
- HW talk: `opencode.json` provides `serial` MCP (`uvx --from pyserial-mcp serial-mcp`); `src/poc_dualcore/poc_send.py` has `--hello/--ping/--sweep/--hold12/--sweep12/--break_/--probe-ladder/--bootsel/--key-delete/--emulate` modes (`--emulate 0|1|2` = ProCon/JoyL/JoyR). Preferred flash flow is button-less: `poc_send.py <dataCOM> --bootsel` (T_BOOTSEL magic → USB BOOTSEL reboot, ~500ms+, needs working data UART) then copy UF2 to the mounted drive; fall back to the physical BOOTSEL button when UART is down.
