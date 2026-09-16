# W1 brief: Deferred flash write (Death-B test)

Move the `store_host` flash write out of the HID-open BTstack callback into a 3s one-shot run-loop timer. Single variable vs W0. Full backup/restore discipline (as AB1–5): UF2 + diff + byte-restore + proof.

## Steps

- [ ] **Step 1: Record baseline and back up**

```powershell
git status --porcelain | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\status-before-w1.txt
Copy-Item src/main.c C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\backups\w1\src_main.c.bak
Copy-Item src/bt/store.c C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\backups\w1\store.c.bak
git diff -- src/main.c | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\diffs\w1-baseline-check.diff
```

(store.c is untracked: its baseline is the backup copy itself. main.c baseline-check documents the tracked side.)
Work from: C:\pico-bcon. Touch ONLY src/main.c and src/bt/store.c as specified below.

- [ ] **Step 2: Add `store_host_force` in store.c**

Refactor minimally: extract the write body of `store_host` into `static void store_host_inner(const bd_addr_t addr, bool force)`, where the `memcmp` early-return is skipped when `force` is true. Keep `void store_host(bd_addr_t addr)` calling inner with false (byte-identical behavior for existing callers). Add:

```c
void store_host_force(const bd_addr_t addr)
{
    store_host_inner(addr, true);
}
```

Declare it in `src/bt/store.h`. The save counter + `host saved (n=)` log stay shared (count = flash writes total). No other changes in store.c/store.h.

- [ ] **Step 3: Defer the open-time write in main.c**

(a) Add near the other `btstack_timer_source_t` declarations:

```c
static btstack_timer_source_t defer_host_timer; /* W1DIAG: deferred host-flash write (Death-B test) */
```

(b) Add the one-shot handler (place near empty_handler; fires once, never re-arms):

```c
/* W1DIAG: runs in run-loop timer context, NOT inside a BTstack event callback. */
static void defer_host_handler(btstack_timer_source_t *ts)
{
    (void)ts;
    store_host_force(probe_host_addr);
}
```

(c) Register its handler once where the other `set_timer_handler` calls live:

```c
    btstack_run_loop_set_timer_handler(&defer_host_timer, &defer_host_handler);
```

(d) In `handle_hid_meta` OPEN success branch, replace:

```c
                store_host(a);
                snprintf(msg, sizeof(msg), "hid open. host %s saved",
                         bd_addr_to_str(a));
                probe_line(msg);
```

with:

```c
                memcpy(probe_host_addr, a, 6);
                probe_host_known = true;
                btstack_run_loop_remove_timer(&defer_host_timer);
                btstack_run_loop_set_timer(&defer_host_timer, 3000);
                btstack_run_loop_add_timer(&defer_host_timer);
                snprintf(msg, sizeof(msg), "hid open. host %s (flash in 3s)",
                         bd_addr_to_str(a));
                probe_line(msg);
```

(RAM update stays immediate so reconnect logic sees the current host; only the flash op moves. Repeat opens re-arm the one-shot. If disconnect precedes firing, the save still happens — intended and harmless. Includes needed: `string.h` for memcpy in main.c — verify, add only if compiler requires.)

- [ ] **Step 4: Compile the wireless build**

```powershell
cmake -S . -B $env:TEMP/wab-w1 -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0
cmake --build $env:TEMP/wab-w1 --target pico-bcon
```

SDK full-path cmake/ninja if needed. Never reuse `build/`. Allow >= 600000 ms. Expected: exit 0, no BUMP flag anywhere.

- [ ] **Step 5: Save artifacts**

```powershell
Copy-Item $env:TEMP/wab-w1/pico-bcon.uf2 log/pico-bcon-w1-defer.uf2
git diff -- src/main.c | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\diffs\w1-main.diff
git diff --no-index -- C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\backups\w1\store.c.bak src/bt/store.c | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\diffs\w1-store.diff
```

(store.c is untracked: persist the change via --no-index against the backup. Both diff files must be non-empty and show ONLY the Step 2–3 changes.)

- [ ] **Step 6: Restore and prove**

```powershell
Copy-Item C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\backups\w1\src_main.c.bak src/main.c
Copy-Item C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\backups\w1\store.c.bak src/bt/store.c
git status --porcelain | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\status-after-w1.txt
(Get-FileHash src/main.c).Hash -eq (Get-FileHash C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\backups\w1\src_main.c.bak).Hash
(Get-FileHash src/bt/store.c).Hash -eq (Get-FileHash C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\backups\w1\store.c.bak).Hash
```

Expected: status files identical; both hashes True. (store.h: if you touched it, back it up in Step 1 too and restore/prove the same way — better: back up store.h in Step 1 regardless.)

- [ ] **Step 7: Report** — write `C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\task-1-report.md` (implementation, build + exit code, UF2 path + size, diff paths + content summary, restore evidence with hashes, files changed, self-review, concerns). DO NOT commit.
