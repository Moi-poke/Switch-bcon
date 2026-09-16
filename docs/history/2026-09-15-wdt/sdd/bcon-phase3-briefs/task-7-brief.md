# P3 Task 7 brief: Race-free reconnect arming (remove-before-add)

Make every `reconnect_timer` arm idempotent so double-add is structurally impossible. No new state, no flag lifecycle. Change is PERMANENT. Tree contains Tasks 1–6 hunks — do not touch them. (Controller ruling in ledger: this remove-before-add design replaces the plan's armed-flag sketch, whose lifecycle could not be closed.)

## Steps

- [ ] **Step 1: Confirm the API and read all 4 arm sites**

Confirm `btstack_run_loop_remove_timer` is declared in the bundled BTstack `btstack_run_loop.h`. If it does NOT exist, STOP and report BLOCKED (do not invent an alternative).
Read all 4 arm sites and their exact text:
  (a) `src/main.c` `handle_bt_ready`: `set(&reconnect_timer,2000)` + `add(&reconnect_timer)` in the `probe_host_known` branch.
  (b–d) `src/bt/link_conn.c` `link_reconnect_handler`: the 3 `set + add` exits (wired-mode early return, beacon early return, end-of-function).

- [ ] **Step 2: Insert remove-before-add at all 4 sites**

At each site, the arm sequence becomes remove → set → add. Exact pattern (adapt variable/expression to each site, `ts` in the handler, `&reconnect_timer` in main.c):

```c
    btstack_run_loop_remove_timer(ts); /* idempotent: no-op when unscheduled */
    btstack_run_loop_set_timer(ts, RECONNECT_RETRY_MS);
    btstack_run_loop_add_timer(ts);
```

Rationale (for the report, do not paste into code beyond one short comment at the main.c site): remove on an unscheduled timer is a safe no-op, and the run loop already dequeued a firing timer before its callback, so re-arm from inside the handler stays the documented pattern. No behavior change in the race-free case; the double-add path becomes impossible.

Keep intervals/conditions identical (`2000` first arm, `RECONNECT_RETRY_MS`/`1000` re-arms, all surrounding `if` conditions untouched).

- [ ] **Step 3: Compile the wireless build**

```powershell
cmake -S . -B $env:TEMP/p3t7 -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0
cmake --build $env:TEMP/p3t7 --target pico-bcon
```

No `-DBD_ADDR_BUMP` flag. Never reuse `build/`. Allow >= 600000 ms. Expected: exit 0.

- [ ] **Step 4: Leave changes in place + record hunks** (main.c 1 site + link_conn.c 3 sites). Do NOT revert.

- [ ] **Step 5: Write the 5-line flash/test note + outgoing-verdict protocol** (owner executes hardware later)

Include: UF2 location; auto-reconnect test (pair → reboot Pico ONLY with Switch idle outside Grip → expect outgoing page within ~7s and reconnect, no Grip); stale-key test cross-ref to Task 3; verdict rule (if collisions/`0x66` storms/no-reconnect → passive-only fallback: note that the on-file AB3 diff predates Phase 3 and would need rebasing — do NOT apply it now).

- [ ] **Step 6: Report** — write `C:\Users\moilo\AppData\Local\Temp\opencode\bcon-phase3\task-7-report.md` (API-exists evidence with header:line, per-site change description, build command + exit code, hunk description, flash/test note + verdict protocol, self-review, concerns). DO NOT commit.
