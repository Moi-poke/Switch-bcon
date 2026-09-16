# Task 3 brief: Passive-only variant, outgoing disabled (AB3)

Single-variable trial: never page out (`hid_device_connect` never fires); only accept incoming connections.

## Steps

- [ ] **Step 1: Record baseline and back up**

```powershell
git status --porcelain | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\status-before-ab3.txt
Copy-Item src/bt/link_conn.c C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\backups\ab3\link_conn.c.bak
git diff -- src/bt/link_conn.c | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\diffs\ab3-baseline-check.diff
```

Work from: C:\pico-bcon. Do NOT touch any file except `src/bt/link_conn.c` (Step 2 spots only).

- [ ] **Step 2: Apply the change (two spots, one variable: no outgoing page)**

Spot A — next to `#define RECONNECT_GIVEUP_MS 15000u` (around lines 43-45), add:

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

The `src/main.c` WORKING-time initial arm stays as-is (it only schedules the now-inert handler); state this explicitly in the report. No other lines change. Note: `stdbool.h` availability — `link_conn.c` already uses `bool` (e.g. `link_wired`), so no new include is needed; if the compiler disagrees, report NEEDS_CONTEXT instead of adding includes on your own.

- [ ] **Step 3: Compile the wireless build**

```powershell
cmake -S . -B $env:TEMP/bcon-ab3 -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0 -DBD_ADDR_BUMP=1
cmake --build $env:TEMP/bcon-ab3 --target pico-bcon
```

Locate `ninja.exe` first (`Get-Command ninja.exe`; if absent, search under `C:\Users\moilo\.pico-sdk` and pass `-DCMAKE_MAKE_PROGRAM=<path>`; prior tasks reused the SDK-bundled cmake by full path — do the same if needed). Never reuse the existing `build/` dir. Allow >= 600000 ms. Expected: exit 0 with a UF2 produced.

- [ ] **Step 4: Save artifacts**

```powershell
Copy-Item $env:TEMP/bcon-ab3/pico-bcon.uf2 log/pico-bcon-ab3-passive.uf2
git diff -- src/bt/link_conn.c | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\diffs\ab3-passive.diff
```

Expected: the diff shows exactly the Step 2 change.

- [ ] **Step 5: Restore and prove**

```powershell
Copy-Item C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\backups\ab3\link_conn.c.bak src/bt/link_conn.c
git status --porcelain | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\status-after-ab3.txt
(Get-FileHash src/bt/link_conn.c).Hash -eq (Get-FileHash C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\backups\ab3\link_conn.c.bak).Hash
```

Expected: status files identical; SHA256 hashes equal.

- [ ] **Step 6: Report** — write `C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\task-3-report.md` (implementation, build command + exit code, UF2 path + size, diff path + content summary, restore evidence with hashes, files changed, self-review, concerns, plus the explicit main.c-arm-inert note). DO NOT commit.
