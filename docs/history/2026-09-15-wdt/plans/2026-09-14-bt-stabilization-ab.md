# BT Stabilization A/B Variants Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build compile-verified UF2 + source diff for each single-variable Bluetooth A/B variant, without committing anything, so the variants can be flashed and tested on hardware later.

**Architecture:** Each task applies exactly one minimal source change (or none for T5), compiles the wireless Pico build in an isolated temp build dir, copies the UF2 to `log/`, saves the source diff to the plan workspace, then restores the sources byte-identical and proves the restore. Tasks run sequentially; the tree must be identical before and after each task.

**Tech Stack:** Raspberry Pi Pico SDK 2.3.0, BTstack (bundled), CMake + Ninja, PowerShell 5.1, RP2350 / pico2_w.

**Spec:** `docs/handoff_bt_20260914.md` (behavioral ground truth, test procedure, constraints) — the plan argues from the handoff; executors read both. This plan file carries the exact values to use verbatim.

## Global Constraints

- No commits, no pushes, no PRs, no branch operations (handoff §7: commit only on explicit instruction).
- NEVER run `git checkout --`, `git restore`, or `git clean` on tracked files: the tree contains pre-existing uncommitted work that must survive. Restore edited files ONLY by copying back the task's own backup copies, then prove equality.
- `C:\Users\moilo\pico-wakecon` is reference-only: never modify, never build into.
- Never paste secret key bytes (link keys, LTK) into reports or logs. HCI dumps stay in local files only.
- `log/*.uf2`, `build/`, and `.superpowers/` are git-ignored scratch.
- Wireless build recipe (handoff §2): `cmake -S . -B <tempdir> -G Ninja [-DCMAKE_MAKE_PROGRAM=<ninja.exe>] -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0 [-DBD_ADDR_BUMP=<n>]`, then build `--target pico-bcon`, then copy the produced UF2 to `log/`. Never reuse the existing `build/` dir (it holds the wired build). Locate `ninja.exe` via `Get-Command ninja.exe`; if absent, search under `C:\Users\moilo\.pico-sdk`. Use per-task timeouts >= 600000 ms for builds.
- Each task records `git status --porcelain` before the first edit and after the restore; the two must be identical (modulo git-ignored scratch).

---

### Task 1: SNIFF-revert variant (AB1)

**Files:**
- Modify: `src/main.c` (1 line)
- Artifacts: `log/pico-bcon-ab1-sniff.uf2`, workspace `diffs/ab1-sniff.diff`, workspace `backups/ab1/src_main.c.bak`

**Interfaces:**
- Consumes: nothing from other tasks (tree must be at handoff state).
- Produces: `log/pico-bcon-ab1-sniff.uf2`, `diffs/ab1-sniff.diff`.

- [ ] **Step 1: Record baseline and back up**

```powershell
git status --porcelain | Out-File workspace/status-before.txt
Copy-Item src/main.c workspace/backups/ab1/src_main.c.bak
git diff -- src/main.c | Out-File workspace/diffs/ab1-baseline-check.diff
```

- [ ] **Step 2: Apply the single change**

In `src/main.c` line 1036, replace exactly:

```c
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH);
```

with:

```c
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH | LM_LINK_POLICY_ENABLE_SNIFF_MODE);
```

Do not touch the diagnostic comment above it or any other line.

- [ ] **Step 3: Compile the wireless build**

Run: `cmake -S . -B $env:TEMP/bcon-ab1 -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0 -DBD_ADDR_BUMP=1` then build `--target pico-bcon`
Expected: exit 0, UF2 produced.

- [ ] **Step 4: Save artifacts**

Run:
```powershell
Copy-Item $env:TEMP/bcon-ab1/pico-bcon.uf2 log/pico-bcon-ab1-sniff.uf2
git diff -- src/main.c | Out-File workspace/diffs/ab1-sniff.diff
```
Expected: diff shows exactly the one-line change.

- [ ] **Step 5: Restore and prove**

Run:
```powershell
Copy-Item workspace/backups/ab1/src_main.c.bak src/main.c
git status --porcelain | Out-File workspace/status-after.txt
```
Expected: `status-after.txt` identical to `status-before.txt`; `git diff -- src/main.c` identical to the Step 1 baseline file.

- [ ] **Step 6: Report** — write workspace `task-1-report.md` with: status (DONE/BLOCKED), UF2 path, diff path, build command + exit code, status-equality evidence, concerns. No commit.

### Task 2: Slow-start variant, 1Hz until first SUB (AB2)

**Files:**
- Modify: `src/main.c` (empty_handler interval + one extern declaration)
- Artifacts: `log/pico-bcon-ab2-slowstart.uf2`, workspace `diffs/ab2-slowstart.diff`, workspace `backups/ab2/src_main.c.bak`

**Interfaces:**
- Consumes: nothing (tree at handoff state; `probe_out_report_count` is a `uint32_t` defined in `src/bt/hid.c`).
- Produces: `log/pico-bcon-ab2-slowstart.uf2`, `diffs/ab2-slowstart.diff`.

- [ ] **Step 1: Record baseline and back up** (same commands as Task 1 Step 1, with `ab2` paths)

- [ ] **Step 2: Apply the change (two spots, one variable: pre-first-SUB pacing)**

Spot A — near the top of `src/main.c` where other `probe_*` externs live (or directly above `empty_handler` if no such block exists), add exactly one line:

```c
extern uint32_t probe_out_report_count;
```

Spot B — in `empty_handler`, replace exactly:

```c
    btstack_run_loop_set_timer(ts, probe_send_interval_ms());
```

with:

```c
    /* AB2: NXBT-style slow start — 1 Hz until the first SUB arrives. */
    uint32_t interval_ms = (probe_out_report_count == 0u) ? 1000u : probe_send_interval_ms();
    btstack_run_loop_set_timer(ts, interval_ms);
```

No other lines change.

- [ ] **Step 3: Compile the wireless build** (same recipe as Task 1, build dir `$env:TEMP/bcon-ab2`, `-DBD_ADDR_BUMP=1`)
- [ ] **Step 4: Save artifacts** (`log/pico-bcon-ab2-slowstart.uf2`, `diffs/ab2-slowstart.diff`)
- [ ] **Step 5: Restore and prove** (same as Task 1 Step 5, with `ab2` paths)
- [ ] **Step 6: Report** (`task-2-report.md`, same contract, no commit)

### Task 3: Passive-only variant, outgoing disabled (AB3)

**Files:**
- Modify: `src/bt/link_conn.c` (fire-condition + one constant)
- Artifacts: `log/pico-bcon-ab3-passive.uf2`, workspace `diffs/ab3-passive.diff`, workspace `backups/ab3/link_conn.c.bak`

**Interfaces:**
- Consumes: nothing (tree at handoff state).
- Produces: `log/pico-bcon-ab3-passive.uf2`, `diffs/ab3-passive.diff`.

- [ ] **Step 1: Record baseline and back up** (same pattern, `ab3` paths, file `src/bt/link_conn.c`)

- [ ] **Step 2: Apply the change**

Spot A — next to `#define RECONNECT_GIVEUP_MS 15000u` (file lines ~43-45), add:

```c
/* AB3: passive-only trial — never page out, only accept incoming. */
static const bool kPassiveOnly = true;
```

Spot B — in `link_reconnect_handler`, change exactly:

```c
    if (probe_hid_cid == 0u && !probe_outgoing_tried && probe_host_known) {
```

to:

```c
    if (probe_hid_cid == 0u && !probe_outgoing_tried && probe_host_known && !kPassiveOnly) {
```

The `src/main.c` WORKING-time initial arm stays as-is (it only schedules the now-inert handler). Note this explicitly in the report. No other lines change.

- [ ] **Step 3: Compile the wireless build** (build dir `$env:TEMP/bcon-ab3`, `-DBD_ADDR_BUMP=1`)
- [ ] **Step 4: Save artifacts** (`log/pico-bcon-ab3-passive.uf2`, `diffs/ab3-passive.diff`)
- [ ] **Step 5: Restore and prove** (same pattern, `ab3` paths)
- [ ] **Step 6: Report** (`task-3-report.md`, same contract, no commit)

### Task 4: Reconnect double-add guard patch (AB4, patch only)

**Files:**
- Modify: `src/main.c` (guard flag + guarded arm, `handle_bt_ready`)
- Artifacts: workspace `diffs/ab4-reguard.diff` (patch for later decision), workspace `backups/ab4/src_main.c.bak`. A UF2 MAY be built for compile proof (`log/pico-bcon-ab4-reguard.uf2`) but it must NOT be mixed into the A/B series.

**Interfaces:**
- Consumes: nothing (tree at handoff state).
- Produces: `diffs/ab4-reguard.diff` (+ optional UF2 clearly labeled as patch proof).

- [ ] **Step 1: Record baseline and back up** (same pattern, `ab4` paths)

- [ ] **Step 2: Apply the change**

Spot A — directly above `handle_bt_ready`, add:

```c
/* AB4: guard against double-adding reconnect_timer on repeated BT WORKING events. */
static bool s_reconnect_arm_done;
```

Spot B — inside `handle_bt_ready`, replace exactly:

```c
    if (probe_host_known) {
        btstack_run_loop_set_timer(&reconnect_timer, 2000);
        btstack_run_loop_add_timer(&reconnect_timer);
    } else {
```

with:

```c
    if (probe_host_known) {
        if (!s_reconnect_arm_done) {
            s_reconnect_arm_done = true;
            btstack_run_loop_set_timer(&reconnect_timer, 2000);
            btstack_run_loop_add_timer(&reconnect_timer);
        }
    } else {
```

Known limitation (state in report): if BT restarts cleanly the flag persists; this is a trial patch, not the final design. No other lines change.

- [ ] **Step 3: Compile the wireless build** (build dir `$env:TEMP/bcon-ab4`, `-DBD_ADDR_BUMP=1`)
- [ ] **Step 4: Save artifacts** (`diffs/ab4-reguard.diff`; optional UF2 labeled patch-proof)
- [ ] **Step 5: Restore and prove** (same pattern, `ab4` paths)
- [ ] **Step 6: Report** (`task-4-report.md`, same contract, no commit)

### Task 5: Stable-identity build (AB5, build flags only)

**Files:**
- Modify: none (source untouched).

**Interfaces:**
- Consumes: nothing.
- Produces: `log/pico-bcon-ab5-bump0.uf2`.

- [ ] **Step 1: Record baseline** (`git status --porcelain` to workspace file; no backup needed)

- [ ] **Step 2: Compile the wireless build with `-DBD_ADDR_BUMP=0`**

Run: `cmake -S . -B $env:TEMP/bcon-ab5 -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0 -DBD_ADDR_BUMP=0` then build `--target pico-bcon`
Expected: exit 0.

- [ ] **Step 3: Save artifact** (`log/pico-bcon-ab5-bump0.uf2`)

- [ ] **Step 4: Verify tree untouched** (`git status --porcelain` identical to Step 1)

- [ ] **Step 5: Report** (`task-5-report.md`): status, UF2 path, build evidence, plus the explicit note that the diagnostic startup key+host wipe is still active in this UF2 (its removal belongs to Phase 3). No commit.

## Deferred (hardware-gated, NOT part of this execution)

- Phase 1: 4:24 SUB-arrival test with hci_dump verdict (needs Switch + Pico).
- Phase 2 flashing/tests of AB1–AB5 UF2s, one variable at a time.
- Phase 3 permanent fixes (host-save dedupe, auth-fail key drop, H1 final design, SNIFF/BUMP finalization, hci_dump + diagnostics removal, Step 4 commit, Task 5).
