# W10 brief: timer-period A/B (resolves U6 1ms-feed vs 2.07ms-blackout deadline crossing)

Diagnostic UF2s only. WDT window fixed at 2s; vary ONLY the `usb_timer` processing period across 1/5/10/50ms, keeping WDT feed semantics identical (feed every handler run). Tests verification-status §3 U6: a 2.07ms flash blackout against a 1ms feed period crosses a feed deadline every time (candidate explanation for the 9/9 death rate); longer periods should cross less often or never.

## Constraints

- No commits, pushes, PRs, branch ops. `C:\Users\moilo\pico-wakecon` never touched. No secret key bytes in reports/logs/code (peer BD_ADDR OK).
- No SDK/BTstack source edits (edits in `src/main.c` only at the two sites below; `src/bt/store.c` only for the forced-write trigger reused from W9 if needed; `CMakeLists.txt` untouched — no new files).
- Isolated temp build dirs (never reuse `build/`; one dir per period: `$env:TEMP/wab-w10-1`, `-5`, `-10`, `-50`). Wireless recipe `-DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0`, no BUMP flag.
- Every added/changed line carries a `W10DIAG` marker (including the period-value lines). Work from `C:\pico-bcon`. BASE `c366bf4`; tree holds uncommitted Phase-3 work — preserve it.
- Restore proof required (hashes equal, marker grep zero). NEVER use git checkout/restore/clean.
- Report path: `C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\task-10-report.md`.

## Exact edit sites (ONLY these; everything else frozen)

- [ ] **Site 1 — re-arm in handler** (`src/main.c:602-606`, `usb_handler`): `btstack_run_loop_set_timer(ts, 1)` → period under test (5/10/50; 1ms variant is a `W10DIAG`-commented no-op restatement). The `poll_tick(...)` call and the `add_timer` line are untouched.
- [ ] **Site 2 — init arm** (`src/main.c:997-999`): `btstack_run_loop_set_timer(&usb_timer, 1)` → same period value as Site 1. Handler registration line untouched.
- [ ] **Frozen:** `empty_timer` (init `main.c:1001-1003`, re-arm `main.c:613` via `probe_send_interval_ms()`) and `stats_timer` (init `main.c:993-995`, re-arm `main.c:598`, 1000ms) stay at their current values. `watchdog_enable(2000, 1)` (`main.c:1012`) untouched. `watchdog_update()` stays inside `poll_tick` (`main.c:544`) — feed semantics identical: exactly one feed opportunity per handler run, no added feeds, no feed hoisting.
- [ ] **Death-trigger normalization:** each session performs forced physical writes using the W9 `store_host_force`-style bypass (generation-numbered, dedupe-proof) fired from a fixed, documented gate (recommend the W9-D just-after-open gate for all four periods, so period is the ONLY variable). If reusing W9 code, re-mark those lines `W10DIAG`. Minimum write proof per session: safe/tag begin-end + rc + dur_us + boot dump (W8 minimum).

## Decision rule (per period, N>=3 forced-write sessions each)

- [ ] Run at least 3 valid forced-write sessions per period (valid = write proof shows `rc=0` physical write; else discard and re-run). Record per session: period, gen, write proof, death (WDT reboot ≈2.0s after write, `wdt=1` boot line) vs survival (≥60s post-write, normal `0x13` or continued run), time-to-death ms.
- [ ] Per-variant death-rate rule: `kill_rate(period) = deaths / valid_sessions`. Predict (deadline-crossing hypothesis): `kill_rate(1ms) ≈ 1.0`, falling as the period exceeds the ~2.07ms blackout (5/10/50ms → markedly lower, 50ms near 0). State the observed 4-tuple and whether it is monotone decreasing.
- [ ] **Falsification condition:** if the 50ms period still dies at the same ~2.0s signature with a kill rate indistinguishable from 1ms (e.g. 3/3 dead, same write→WDT delay), the 1ms-deadline-crossing explanation is WEAKENED — report U6 as falsified in this form and redirect to the alarm-re-arm-loss path (U5), not to finer period sweeps.

## Steps

- [ ] **Step 1: Record baseline and back up**

```powershell
git status --porcelain | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\status-before-w10.txt
Copy-Item src/main.c C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\backups\w10\src_main.c.bak
Copy-Item src/bt/store.c C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\backups\w10\store.c.bak
Copy-Item src/bt/store.h C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\backups\w10\store.h.bak
```

Verify pre-state: marker grep `W7DIAG|W8DIAG|W9DIAG|W10DIAG|diag_rec|scratch` over `src/` returns zero hits. No new files in this task.

- [ ] **Steps 2–6: Implement ONE period value at a time** (Sites 1+2 same value + fixed-gate forced write; verify `git diff` shows ONLY the two period lines + force/gate lines, all `W10DIAG`-marked).
- [ ] **Step 7: Compile each period in its isolated dir**

```powershell
cmake -S . -B $env:TEMP/wab-w10-1 -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0
cmake --build $env:TEMP/wab-w10-1 --target pico-bcon
# repeat with wab-w10-5, wab-w10-10, wab-w10-50
```

Expected: exit 0 each. Never reuse `build/`.

- [ ] **Step 8: Save artifacts**

```powershell
Copy-Item $env:TEMP/wab-w10-1/pico-bcon.uf2 log/pico-bcon-w10-1ms.uf2
Copy-Item $env:TEMP/wab-w10-5/pico-bcon.uf2 log/pico-bcon-w10-5ms.uf2
Copy-Item $env:TEMP/wab-w10-10/pico-bcon.uf2 log/pico-bcon-w10-10ms.uf2
Copy-Item $env:TEMP/wab-w10-50/pico-bcon.uf2 log/pico-bcon-w10-50ms.uf2
git diff -- src/main.c src/bt/store.c src/bt/store.h | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\diffs\w10-<period>-tracked.diff
```

All UF2s non-empty. Record size + SHA256 per period.

- [ ] **Step 9: Restore and prove** (after ALL period builds): restore the 3 backed-up files via `Copy-Item` (no git checkout/restore/clean); `status-after-w10.txt` identical to `status-before-w10.txt` (`Compare-Object` empty); file hashes equal to Step-1 baseline; marker grep `W10DIAG` over `src/` zero hits (others zero too).
- [ ] **Step 10: HW run + report** — write `task-10-report.md`: per-period implementation (exact diff), build exit codes, UF2 paths+sizes+SHAs, per-session table (period, gen, write proof rc/dur, death/survival, write→WDT ms), kill-rate 4-tuple, falsification verdict on U6, restore evidence, self-review, concerns.

## NO-GO gate

STOP and report BLOCKED without building if: (a) the period cannot be changed at exactly the two sites without also retiming empty/stats/reconnect behavior (state which call site couples them); (b) feed-every-run semantics cannot be preserved (e.g. a longer period would require moving `watchdog_update` out of `poll_tick` to keep the link alive — that restructuring is OUT of scope); (c) the fixed-gate forced write cannot be held constant across periods. Report the blocker and the minimal restructure it would need.

DO NOT commit.
