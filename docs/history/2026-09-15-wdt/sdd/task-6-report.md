# W6 report: hci_dump-only visibility rebuild (death-session capture)

## Implementation (exact lines + include resolution)

Touched ONLY `src/main.c`. 5 added lines, all `W6DIAG`-marked, exactly per brief Step 2:

(a) Includes, placed after `#include "pico/btstack_flash_bank.h"` (line 31), before the blank line + `#include "protocol.h"`:
```c
/* W6DIAG: death-session HCI capture only. REMOVE after comparison with AB1 victory dump. */
#include "hci_dump.h"
#include "hci_dump_embedded_stdout.h"
```

(b) Init, placed immediately before `hci_set_bd_addr(probe_addr);` (was line 985), after `hci_add_event_handler(&hci_events);`:
```c
    /* W6DIAG */ hci_dump_init(hci_dump_embedded_stdout_get_instance());
    /* W6DIAG */ hci_dump_enable_packet_log(true);
```

Nothing else changed. No SCR, no heartbeat, no markers, no behavior change.
The second `hci_set_bd_addr` in `src/bt/link_conn.c:130` was NOT touched.

Include resolution (verified before editing, no guessing):
- `hci_dump.h` → `<SDK>/lib/btstack/src/hci_dump.h` (via `pico_btstack_base_headers` include dir `${PICO_BTSTACK_PATH}/src`).
- `hci_dump_embedded_stdout.h` → `<SDK>/lib/btstack/platform/embedded/hci_dump_embedded_stdout.h` (via include dir `${PICO_BTSTACK_PATH}/platform/embedded`).
- Both sources (`hci_dump.c`, `hci_dump_embedded_stdout.c`) are already compiled into `pico_btstack_base`, so no CMake change was needed.
- Proven by successful compile+link (exit 0); brief's exact paths used verbatim, no alternates needed.
- SDK pinned to 2.3.0 (`C:/Users/moilo/.pico-sdk/sdk/2.3.0`) to match the W5 build cache flags.

## Build + exit code

- Configure: `cmake -S . -B $env:TEMP/wab-w6 -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0` (+ `-DPICO_SDK_PATH=.../sdk/2.3.0`, full cmake/ninja paths since neither is on PATH) → success, Release, board pico2_w.
- Build: `cmake --build $env:TEMP/wab-w6 --target pico-bcon` → **228/228 steps, exit code 0**. `src/main.c.obj` compiled with no new warnings; `hci_dump*.c` objects built from SDK. No BUMP flag.
- Note: `cmake`/`ninja` are not on PATH in this shell; used `C:/Users/moilo/.pico-sdk/cmake/v4.3.4/bin/cmake.exe` and `CMAKE_MAKE_PROGRAM=C:/Users/moilo/.pico-sdk/ninja/v1.13.2/ninja.exe`. Never touched `build/`.

## UF2 path + size

- `log/pico-bcon-w6-hcidump.uf2` — **818176 bytes**, SHA256 `3DD8E1BC81FCF7AC2C83A18AE5B2F0D6E8202CD19E3FC91D0CF90DE73A6FBDF5`.
- Companion ELF (`$env:TEMP/wab-w6/pico-bcon.elf`) SHA256 `05C5F49E7BCF7C56AC4493132D4AC67E4DB0E94D2E7799B877684EAC81F06532`.
- `log/` + `*.uf2` are gitignored → artifact does not pollute `git status`.

## Diff inventory

- `C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\diffs\w6-main.diff` (83376 bytes): `git diff -- src/main.c` vs HEAD — contains W6 lines plus pre-existing baseline noise (dirty tree from W0/W1/W3/W4/W5-era work, present before W6 per `status-before-w6.txt`).
- `C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\diffs\w6-vs-backup.diff` (1870 bytes): `git diff --no-index` backup-vs-working isolation diff — shows EXACTLY the 5 added W6 lines in 2 hunks (include hunk +3, init hunk +2), zero removals, zero other changes.
- `status-before-w6.txt` / `status-after-w6.txt` + `backups/w6/src_main.c.bak` retained alongside.

## Restore evidence with hashes

- Restore method: `Copy-Item backups/w6/src_main.c.bak → src/main.c` (backup only; never `git checkout/restore/clean`).
- `restored = 690F3A7937571F7240BDC2922E5F4BCB7DAEB1A83963EECCC3A9C7034E10C27A`
- `backup   = 690F3A7937571F7240BDC2922E5F4BCB7DAEB1A83963EECCC3A9C7034E10C27A`
- hashMatch=True, baselineMatch=True (identical to pre-edit baseline hash recorded in Step 1).
- `status-after == status-before`: Compare-Object → identical (True). The `M src/main.c` entry in both is pre-existing baseline dirt vs HEAD, not W6 residue (hash proof is byte-identical).
- Post-restore `grep "W6DIAG|hci_dump" in src/` → **zero hits**.

## Files changed

- In-repo, post-restore: **none** (net). During build window only `src/main.c` (+5 lines); fully reverted with hash proof.
- New artifacts (all outside tracked tree or ignored): `log/pico-bcon-w6-hcidump.uf2` (ignored), workspace `backups/w6/src_main.c.bak`, `diffs/w6-main.diff`, `diffs/w6-vs-backup.diff`, `status-before/after-w6.txt`, build dir `$env:TEMP/wab-w6` (fresh, `build/` untouched).
- `C:\Users\moilo\pico-wakecon`: never touched.

## Self-review

- Diff shows exactly the specified additions (1 marker comment + 2 includes + 2 init calls, all W6DIAG-marked), nothing else — verified via vs-backup isolation diff (2 hunks, +5/-0).
- Restores hash-proven (True); post-restore grep zero; status-after identical to status-before.
- No commits, no pushes, no PRs. No subagents dispatched (all work done directly).
- Backup/restore discipline followed exactly (Steps 1, 4, 5).

## Concerns

- None blocking. Minor notes: (1) `cmake`/`ninja` not on PATH — used full SDK paths, recorded above for reproducibility. (2) `hci_dump_embedded_stdout` logs over stdout at 115200 baud on LOG_UART alongside existing `printf` traffic; capture host must log raw serial bytes continuously or the death window's tail may be cut. (3) UF2 is for ONE death-session capture only — tree is already restored; any re-flash later must rebuild from the saved UF2, not re-edit.

## Comparison protocol (Step 6 — for the owner)

1. **Flash** `log/pico-bcon-w6-hcidump.uf2` to the Pico 2 W (bootsel drag-and-drop).
2. **Capture**: open the log UART at 115200 baud with raw byte logging to a file (e.g. `log/COM3_<timestamp>_w6death.txt`). Log from before pairing through the death.
3. **Pair** to the Switch and drive it into a WDT death (same repro as prior death sessions). Do NOT reset/power-cycle before stopping the capture — the tail matters most.
4. **Byte-diff the encrypt→HID-open window** of the new death log against the victory reference `log/COM3_2026_09_14.22.32.18.050_ab1.txt` (680859 bytes, AB1 victory with full HCI):
   - Align both logs at encryption-enable / link-key / `HCI Event: Encryption Change`, then walk forward packet-by-packet through L2CAP `Connect Req/Rsp`, `Config Req/Rsp`, and the HID `OPEN`.
   - Watch specifically: **PSM values** (HID-Control 0x11 vs HID-Interrupt 0x13), **MTU** fields, **FCS/config-option bytes** in Config Req/Rsp, channel IDs/DCIDs, and result/status codes.
   - Identify the **last packet before silence** in the death log and the first missing/divergent byte vs the victory log — that exchange is the wedge candidate.
   - Note any retransmit/duplicate bursts present in death but absent in victory.
5. After comparison, **delete the W6 UF2 from the device** (reflash a clean-tree build) — the tree already has zero W6 residue; this UF2 must never ship.
