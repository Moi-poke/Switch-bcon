# Task 4 brief: Reconnect double-add guard patch (AB4, patch only)

Trial patch (NOT part of the A/B flash series): guard `reconnect_timer` against double-add when BT WORKING events re-fire `handle_bt_ready`. Deliverable is the persisted diff; an optional UF2 proves compilation only and must be labeled patch-proof, never mixed into the AB series.

## Steps

- [ ] **Step 1: Record baseline and back up**

```powershell
git status --porcelain | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\status-before-ab4.txt
Copy-Item src/main.c C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\backups\ab4\src_main.c.bak
git diff -- src/main.c | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\diffs\ab4-baseline-check.diff
```

Work from: C:\pico-bcon. Do NOT touch any file except `src/main.c` (Step 2 spots only). `src/main.c` is TRACKED, so plain `git diff -- src/main.c` captures the change (unlike untracked paths).

- [ ] **Step 2: Apply the change (two spots, one variable: arm-once guard)**

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

- [ ] **Step 3: Compile the wireless build**

```powershell
cmake -S . -B $env:TEMP/bcon-ab4 -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0 -DBD_ADDR_BUMP=1
cmake --build $env:TEMP/bcon-ab4 --target pico-bcon
```

Locate `ninja.exe` first (`Get-Command ninja.exe`; if absent use `C:\Users\moilo\.pico-sdk\ninja\v1.13.2\ninja.exe` via `-DCMAKE_MAKE_PROGRAM` as prior tasks did; SDK-bundled cmake at `C:\Users\moilo\.pico-sdk\cmake\v4.3.4\bin\cmake.exe` if `cmake` is not on PATH). Never reuse the existing `build/` dir. Allow >= 600000 ms. Expected: exit 0.

- [ ] **Step 4: Save artifacts**

```powershell
git diff -- src/main.c | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\diffs\ab4-reguard.diff
```

Optional: copy the UF2 to `log/pico-bcon-ab4-reguard-PATCH-PROOF.uf2` (label makes clear it is not an A/B candidate). Expected: the diff shows exactly the Step 2 change.

- [ ] **Step 5: Restore and prove**

```powershell
Copy-Item C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\backups\ab4\src_main.c.bak src/main.c
git status --porcelain | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\status-after-ab4.txt
(Get-FileHash src/main.c).Hash -eq (Get-FileHash C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\backups\ab4\src_main.c.bak).Hash
```

Expected: status files identical; SHA256 hashes equal (handoff-state hash for src/main.c is 20CED94463A86AE116FE63C6AC7809E42E6F2C23E8DFD9AD8155BAAC38DBB8A5 — confirm match and report it).

- [ ] **Step 6: Report** — write `C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\task-4-report.md` (implementation, build command + exit code, diff path + content summary, UF2 path if built, restore evidence with hashes, files changed, self-review, concerns incl. the known limitation). DO NOT commit.
