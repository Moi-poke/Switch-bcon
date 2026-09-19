# hci_dump TEMP diagnostic — revert plan (rumble A2-10 capture)

HEAD baseline: `df00641` (df00641b2a9c9594bff22823712059eaae8e721e)
Artifact: `firmware/pico-bcon-hcidump-a2-10-2311c8d.uf2`
  - size 825856 bytes
  - SHA256 `53012C2498E0D6A0739CB3899B484725C87CF635E0FFD3FA4FB6261D51870345`
Build config: wireless (`-DWIRED_DEFAULT=0`) + derated data UART (`-DPOC_DATA_BAUD=115200`), `PICO_BOARD=pico2_w`, Release, GCC 15.2.1, Pico SDK 2.3.0.

NOTE (deviation from the brief): the brief said exactly 2 lines. Two *calls* are not
enough — HEAD `src/main.c` no longer includes any hci_dump header (removed in Phase-3
Task 4), and GCC 15 rejects the implicit declaration of
`hci_dump_embedded_stdout_get_instance()`. So **3 lines** were added: 1 include + the 2
calls. `btstack.h` already pulls in `hci_dump.h` (for `hci_dump_init` /
`hci_dump_enable_packet_log`), but the embedded-stdout instance getter lives in its own
header. Drop the include and the build fails.

## The lines added (source anchors are on HEAD `df00641`)

File: `src/main.c`

1) Include — after HEAD line 32 `#include "pico/btstack_flash_bank.h"` (becomes new line 33):
```c
#include "hci_dump_embedded_stdout.h"
```

2) Init/enable — immediately after HEAD line 1527 `hci_add_event_handler(&hci_events);`
   (become new lines 1528-1529), before `hci_set_bd_addr(probe_addr);`:
```c
    hci_dump_init(hci_dump_embedded_stdout_get_instance());
    hci_dump_enable_packet_log(true);
```
That is the whole instrumentation: exactly 1 include + 2 calls, no other behavior change,
no new Flash-write path, no SDK edits.

## How the artifact was built (clean baseline, isolated from the live tree)

The working tree had uncommitted third-party edits when this task ran (a concurrent
"PokeCon" change in `src/main.c`, `CMakeLists.txt`, `src/poc_dualcore/poc_send.py`,
`tests/host/`, plus new files). To avoid baking that WIP into the diagnostic, the source
was exported from a pristine committed baseline instead of copying the dirty tree:

```powershell
# 1. pristine HEAD export (no working-tree dirt)
git archive --format=zip -o "$env:TEMP\bcon-hci-src.zip" HEAD
Expand-Archive -Path "$env:TEMP\bcon-hci-src.zip" -DestinationPath "$env:TEMP\bcon-hci-src" -Force
# 2. apply the 3 lines above to $env:TEMP\bcon-hci-src\src\main.c
```

## Rebuild command (wireless, from any tree that has the 3 lines)

```powershell
$cmake = Join-Path $env:USERPROFILE '.pico-sdk/cmake/v4.3.4/bin/cmake.exe'
$ninja = Join-Path $env:USERPROFILE '.pico-sdk/ninja/v1.13.2/ninja.exe'
$bd    = Join-Path $env:TEMP 'bcon-hcidump'
& $cmake -S . -B $bd -G Ninja -DCMAKE_MAKE_PROGRAM:FILEPATH=$ninja `
    -DPICO_SDK_PATH=C:/Users/moilo/.pico-sdk/sdk/2.3.0 `
    -DWIRED_DEFAULT=0 -DPOC_DATA_BAUD=115200
& $cmake --build $bd --target pico-bcon
# UF2: "$bd\pico-bcon.uf2"
# (for the isolated build above, -S was $env:TEMP\bcon-hci-src instead of .)
```

## REVERT the instrumentation (remove the 3 lines)

Because the artifact was built from the isolated pristine copy, the main `C:\pico-bcon`
tree carries **no** hci_dump changes from this task. To revert the 3 lines from any tree
that did receive them:

1. Delete `#include "hci_dump_embedded_stdout.h"` (the line right after
   `#include "pico/btstack_flash_bank.h"`).
2. Delete the two lines:
   `hci_dump_init(hci_dump_embedded_stdout_get_instance());` and
   `hci_dump_enable_packet_log(true);` (right after `hci_add_event_handler(&hci_events);`).
3. Or, if the lines were applied on top of a clean committed baseline:
   `git checkout -- src/main.c`
4. Prove removal:
   `Select-String -Path src\main.c -Pattern 'hci_dump'` -> zero hits;
   `git status --short` -> no `src/main.c` entry.

## Reflash steps (NO HW performed by this task)

Preferred (serial, from the running FW):
```powershell
python src/poc_dualcore/poc_send.py --bootsel --port COM<N>
```
This sends frame `[AB][0x37][01][0x5A][SEQ][CRC8]` (`T_BOOTSEL = 0x37`, magic `0x5A`).
The FW prints `BOOTSEL req -> usb_boot in 500ms` then `rebooting to BOOTSEL...` and
reboots to USB mass storage in ~500 ms. (Alternative trigger with only the LOG UART
connected: send the literal line `bootsel`, case-insensitive, on UART0 @115200;
`poll_log_bootsel()` arms the same path.)

Drag-drop fallback (no FW cooperation needed):
1. Unplug USB. Hold BOOTSEL, plug USB in, release BOOTSEL -> `RPI-RP2`/`RP2350` mass-storage volume appears.
2. Copy `firmware/pico-bcon-hcidump-a2-10-2311c8d.uf2` onto that volume; the board reboots.

## Capture, then restore to prior FW

Capture (rumble A2-10):
- Open UART0 @115200 with raw byte logging before pairing; keep logging through the session.
- Look for ACL data rows carrying the HID output report `0x10`; the target signature is
  an `A2 10` line with a non-zero rumble payload (need neutral + non-zero kinds).
- hci_dump emits raw HCI bytes and may include link-key/LTK material: **treat captured
  logs as secret, never paste key bytes anywhere, delete the raw log after extracting the
  A2 10 payloads.**

Post-capture restore verification (reflash prior production FW concept,
`firmware/pico-bcon-prod-fix1.uf2`):
1. Reflash the prior production UF2 (same `--bootsel` / drag-drop flow).
2. Open UART0 @115200 and confirm the prior-FW boot line is back:
   `=== pico-bcon ===` followed by
   `ready. feed UART1 GP4/5 <baud> baud v3 frames. log=UART0 115200`
   (wireless also prints `BT READY` and `link keys=<n>`).
3. Confirm the hci_dump artifacts are gone: no HCI hex packet rows (no `A2 10` / HCI
   command/event dumps) precede the banner.

## Addendum (2026-09-18, trusted-provenance rebuild)

Rebuilt from HEAD `2311c8d` + same 3 lines (temp dir, wireless/115200):
`firmware/pico-bcon-hcidump-a2-10-2311c8d.uf2`, SHA256
`53012c2498e0d6a0739cb3899b484725c87cf635e0ffd3fa4fb6261d51870345` —
byte-identical to the df00641 artifact above (POKECON code is GC'd out when
`POKECON_INPUT=0`). Either file is usable for capture; prefer the 2311c8d one.
`src/main.c` reverted after build (`hci_dump` 0 hits, `git status` clean).
