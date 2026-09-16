# P3 Task 4 brief: Remove hci_dump and Task 4 diagnostics (keep safety infra)

Delete every temporary diagnostic; retain WDT, HardFault reporter, MSPLIM, and behavior-neutral one-line observation logs. Change is PERMANENT. Tree contains Tasks 1–3 hunks — do not touch them.

## Steps

- [ ] **Step 1: Read and inventory**

Read the candidate regions in `src/main.c` and `src/bt/hid.c` and confirm exact text before deleting anything: hci_dump includes + init/enable calls; `log_hci_packet` definition + its (commented-out) call; SCR flight-recorder block (enum/defines, `scr_dump_and_clear` + call, all `watchdog_hw->scratch[...]` uses); `dbg_hb_alarm` + `add_repeating_timer_ms` registration; `empty tick`/`cansend` markers; `runloop EXIT` probe_line; any remaining `hid open done` line; `Task 4` comment wording on retained safety code; hid.c's `hardware/structs/watchdog.h` include + `cansend`/`SCR_SEND`/`cansent` blocks in `probe_can_send_now`.

- [ ] **Step 2: Delete in `src/main.c`**

(a) `#include hci_dump.h` + `#include hci_dump_embedded_stdout.h` and their comment lines.
(b) `hci_dump_init(...)` + `hci_dump_enable_packet_log(true)` and their comment lines.
(c) The `log_hci_packet` static function definition AND its commented-out call site.
(d) The entire SCR flight-recorder apparatus: enum/defines, `scr_dump_and_clear` definition + its boot call, every `watchdog_hw->scratch[...]++` and `watchdog_hw->scratch[...] = ...` increment/assignment in handlers (SCR_TICK, SCR_EMPTY, SCR_EV, SCR_EVMS, SCR_SEND if present in main.c).
(e) `dbg_hb_alarm` definition + its `add_repeating_timer_ms(100, ...)` registration (+ any `s_dbg_hb_timer` declaration made only for it).
(f) In `empty_handler`: the `empty tick` 5-shot block (`static uint8_t dbg_n` + `probe_line("empty tick")` + `SCR_EMPTY++`). KEEP the `hid_device_request_can_send_now_event(probe_hid_cid);` call and the re-arm lines.
(g) The `probe_line("runloop EXIT")` line. KEEP the `while (1) tight_loop_contents();` guard loop.
(h) If a standalone `probe_line("hid open done")` line still exists (Task 2 may have replaced its neighborhood): delete it as redundant with the `hid open. host %s saved` line. If already gone, note so.
(i) Reword (do NOT delete code): any remaining `Task 4診断用の一時措置` comments on RETAINED safety code (HardFault reporter, MSPLIM/stack-guard install, `watchdog_enable`) into permanent descriptions. The safety code itself stays byte-identical.

- [ ] **Step 3: Delete in `src/bt/hid.c`**

(a) `#include "hardware/structs/watchdog.h"` — ONLY if no other use of `watchdog_hw` remains in the file after (b). Verify first.
(b) In `probe_can_send_now`: the `cansend` 5-shot block, the `watchdog_hw->scratch[5]++` line, and the `cansent` block. KEEP all send logic (`reply`/`0x30`/empty branches + `probe_send_now_wanted = false;`).

- [ ] **Step 4: Keep list (do NOT touch)**

HardFault reporter + MSPLIM/stack-guard + `watchdog_enable(2000,1)`; connection-request peer log; LINK_KEY_REQUEST / AUTH_COMPLETE (incl. Task 3 addition) / ENCRYPTION_CHANGE / SSP-counter logs; `hid open. host %s saved`; `hid closed`; `disc reason` + reconnect-armed; BCON/stats prints; all timers and handlers' functional lines.

- [ ] **Step 5: Compile + host tests**

Wireless: `cmake -S . -B $env:TEMP/p3t4 -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0 -DBD_ADDR_BUMP=1` then `--target pico-bcon`. Expected exit 0. Host suite (vcvars64 shell, tests/host): expected 3/3 ALL PASS.

- [ ] **Step 6: Prove the removal**

```powershell
Get-ChildItem -Recurse -File -Include *.c,*.h src | Select-String -Pattern "hci_dump|Task 4|scratch\[|cansend|cansent|empty tick|runloop EXIT|hid open done|dbg_hb_alarm|scr_dump|SCR_" | Select-Object Path, LineNumber, Line
```

Expected: ZERO hits, except `Task 4` inside `docs/`/history if the pattern scope leaks (scope is `src/` only — must be zero there). If any hit remains, either remove it (if in the Step 2/3 delete set and missed) or justify in the report (if in the Step 4 keep set — quote it).

- [ ] **Step 7: Write the 5-line flash/test note** (owner executes hardware later)

Include: UF2 location; pair + handshake expectation (SUB arrival, no `0x13`); log-cleanliness expectation (no HCI packet bytes, no key material — statuses/counters only); pass/fail signals.

- [ ] **Step 8: Report** — write `C:\Users\moilo\AppData\Local\Temp\opencode\bcon-phase3\task-4-report.md` (per-block removal list with file:line, keep-list confirmation, build + host-test evidence, Step 6 output summary, flash/test note, self-review, concerns). DO NOT commit.
