# W8 brief: epoch + boot-baseline instrumentation (resolves U3/U5 accumulation ambiguity)

Diagnostic UF2 only. Extends the W7 design (recreate `src/diag_rec.c`/`.h`, deleted at W7 restore) with the external-review minimum: per-boot epoch, last-tag + last-epoch per flash op, boot-start baselines, current-minus-baseline boot deltas. Then revert fully. Do NOT rebuild W6/W7 UF2s (both persist in `log/`).

External review verdict (accepted by controller): W7's `trail=2,3,4,5,7,8,9,10` proves the last observed `usb_handler` completed `set_timer`+`add_timer` and reached return-imminent; fault boundary is ADD_TIMER_DONE -> next FIRE missing (lower scheduler path, unchanged by this task). `del=1/1` and `adds-fires=+2` on all timers are most likely cross-boot accumulation in the NOLOAD recorder, not new anomalies. This task definitionally separates per-boot values from cumulative ones.

## Constraints (same as W7)

- No commits, pushes, PRs, branch ops. `C:\Users\moilo\pico-wakecon` never touched. No secret key bytes in reports/logs/code.
- No SDK/BTstack source edits (instrumentation in our layers only: `src/main.c`, `src/bt/store.c`, new `src/diag_rec.*`, one `CMakeLists.txt` hunk only if sources are explicitly listed).
- Isolated temp build dir (never reuse `build/`). Wireless recipe `-DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0`, no BUMP flag.
- Every added line carries a `W8DIAG` marker. New files are 100% W8DIAG-marked per line.
- Work from `C:\pico-bcon`. BASE `c366bf4`, tree currently holds uncommitted Phase-3 work; W7 was hash-proven restored (zero `W7DIAG|diag_rec|scratch` hits expected at start — verify).

## Steps

- [ ] **Step 1: Record baseline and back up**

```powershell
git status --porcelain | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\status-before-w8.txt
Copy-Item src/main.c C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\backups\w8\src_main.c.bak
Copy-Item src/bt/store.c C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\backups\w8\store.c.bak
Copy-Item src/bt/store.h C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\backups\w8\store.h.bak
Copy-Item CMakeLists.txt C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\backups\w8\CMakeLists.txt.bak
```

Verify pre-state: `Get-ChildItem src/diag_rec.*` empty; marker grep `W7DIAG|W8DIAG|diag_rec|scratch` over `src/*.c,*.h` returns zero hits (W7 restored). New files listed here for deletion at restore; NEVER use git clean.

- [ ] **Step 2: Scratch ownership map (gate, reuse W7 proof)**

W7 proved: SDK owns `scratch[4..7]` (`watchdog_enable` magic / reboot pc/sp); our recorder used `[0]=magic/version, [1]=index+CRC, [2]=stage mirror`; `[3]` spare, never written. Grep-confirm zero `scratch` users in `src/` again, then assign **`[3]` = boot epoch counter** (u32, incremented in `diag_rec_init` on every boot, survives WDT reboot, resets on power-loss/reflash like the rest of the NOLOAD state). If `[3]` is taken, STOP and report BLOCKED.

- [ ] **Step 3: Recorder with epoch + baseline (new files, deleted at restore)**

Create `src/diag_rec.c` + `src/diag_rec.h` (check no name collision first), `.uninitialized_data` (NOLOAD) ring as in W7 (magic `0xD1A60008`, ver 8, FNV CRC, 96-entry ring, Core0-only writes, no printf/flash/mutex). State extends W7 with:

  - `boot_epoch`: current boot's epoch (from `scratch[3]`).
  - `post_epoch`: epoch of the preserved (previous-boot) content at dump time — i.e. capture the epoch value BEFORE incrementing, keep both.
  - Per flash op (safe/tag/del/lkput/lkdel): `begins/ends` (cumulative), `last_tag`, `last_rc`, `last_dur`, **`last_epoch`** (epoch in which the last op of that kind ran).
  - Per owned timer (usb/empty/stats): `tmr_add/tmr_fire/tmr_to` (cumulative) — unchanged from W7.
  - **Boot baseline block**: snapshot taken once per boot AFTER dumping previous-boot content and AFTER epoch increment, BEFORE any timer arm or flash op of the new boot: `base_begins/ends[6]`, `base_tmr_add/base_tmr_fire[4]`, `base_epoch`. Boot-delta = current minus base.
  - Init order (strict): 1) read WDT-reboot reason flag first; 2) dump previous-boot content (boot context, `probe_line`); 3) validate magic/CRC (reset + fresh epoch only if invalid); 4) increment epoch into `scratch[3]`; 5) store baseline snapshot; 6) proceed with normal init/arms.

- [ ] **Step 4: Flash-op counters (same choke points as W7, plus epoch/tag)**

  - `src/bt/store.c`: same three instrumentation points as W7 (`tag_store_safe` outer, `tag_store_fn` inner, `tag_delete_fn`), recording begin/end + rc + duration + **tag + current epoch** into `last_tag/last_epoch`. Error semantics unchanged. Includes: `pico/time.h` + `diag_rec.h` (W5/W7-proven).
  - Link-key shim in `src/main.c`: same 9-method forwarder as W7 (read W7's `w7-main-vs-backup.diff` in `..\bcon-wab\diffs\` for exact signatures), counting `put/delete` begin/end + epoch.
  - Boundary (carried): SDK-internal erase/program granularity is OUT of scope. `delete_found` (whether BTstack's internal `delete_tag_until_offset` matched an entry) is NOT observable without SDK edits — do NOT attempt; instead the reading guide correlates the DEL counter with the hci_dump `Erase tag '...'` log line (present => physical write; absent => target-missing scan or other-boot value, disambiguated by `last_epoch` + `last_tag`). State this boundary in the report.

- [ ] **Step 5: Fine stages + owned-timer accounting (verbatim W7)**

Same 10 stage points in `poll_tick`/`usb_handler` and same FIRE-at-entry / ADD-after-`add_timer` points for usb/empty/stats (init arms included) as W7's `w7-main-vs-backup.diff`. `reconnect_timer` untouched. No logic restructuring.

- [ ] **Step 6: hci_dump ON + extended boot dump**

Re-add the 6 hci_dump lines exactly as W6/W7. `diag_rec_init()` right after the `=== pico-bcon ===` banner; extended `w8_boot_dump()` after the `wdt=` banner, printing (via `probe_line`, boot context only):

```
w8epoch post=<prev> current=<new>
w8flash-total safe=a/b tag=a/b del=a/b lkput=a/b lkdel=a/b
w8flash-boot  safe=a/b tag=a/b del=a/b lkput=a/b lkdel=a/b
w8last store tag=<hex> epoch=<e> rc=<r> dur=<us>
w8last delete tag=<hex> epoch=<e> rc=<r> dur=<us>
w8tmr-total usb a=.. f=.. to=.. empty a=.. f=.. to=.. stats a=.. f=.. to=..
w8tmr-boot  usb a=.. f=.. empty a=.. f=.. stats a=.. f=..
w8stage last=.. n=.. trail=..,..,..,..,..,..,..,..
w8ev last=0x.. n=..
```

(`*-boot` = current-minus-baseline. `to=` only in total lines. Keep the `w7rec invalid (fresh boot; ring reset)` path with the W8 name.)

- [ ] **Step 7: Compile the wireless build**

```powershell
cmake -S . -B $env:TEMP/wab-w8 -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0
cmake --build $env:TEMP/wab-w8 --target pico-bcon
```

CMakeLists: add `src/diag_rec.c` (W8DIAG-commented) ONLY if sources are explicitly listed (W7 proved they are). Never reuse `build/`. Expected: exit 0.

- [ ] **Step 8: Save artifacts**

```powershell
Copy-Item $env:TEMP/wab-w8/pico-bcon.uf2 log/pico-bcon-w8-epoch.uf2
git diff -- src/main.c src/bt/store.c src/bt/store.h CMakeLists.txt | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\diffs\w8-tracked.diff
```

Copy new files to `diffs/w8-diag_rec.c.txt`, `diffs/w8-diag_rec.h.txt`. Extra: `w8-main-vs-backup.diff`, `w8-store-vs-backup.diff`. All artifacts non-empty. Record UF2 size + SHA256.

- [ ] **Step 9: Restore and prove**

Restore the 4 backed-up files via `Copy-Item` (no git checkout/restore/clean); DELETE exactly the 2 created files. `status-after-w8.txt` identical to `status-before-w8.txt` (`Compare-Object` empty); file hashes equal to Step-1 baseline; `Get-ChildItem src/diag_rec.*` empty; marker grep `W8DIAG|diag_rec|scratch` over `src/` zero hits (W7DIAG must also be zero — it was zero at start).

- [ ] **Step 10: Report** — write `C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\task-8-report.md` (scratch map, per-site implementation, build exit code, UF2 path+size+SHA, artifact inventory, restore evidence incl. hashes, self-review, concerns) PLUS the reading guide with worked expectations:

  - U3: if `w8flash-boot del=0/0` while total `del=1/1` with `delete epoch=<post>` => other-boot value, case closed. If boot `del=1/1` with `delete tag=4243484f/42435731` + epoch=current => live path identified by tag (host-forget vs cap-forget; cap-forget has no callers — flag as new finding).
  - U5: if every timer's boot delta satisfies `a-f=1` while totals show +2 => accumulation closed. State the exact arithmetic.
  - Duration note: record `dur` alongside tag+epoch as usual; do NOT add program/erase/lockout-split instrumentation (deferred per review — state why).

DO NOT commit.
