# W5 brief: Time the flash store (stall-vs-corruption discriminator)

Add microsecond timing around the TLV store call inside the W1-style deferred write. If only the "begin" line prints before a death → stall inside the write (e.g. bank erase). If both lines print and death follows ~2s later → post-write corruption. Full backup/restore discipline.

## Steps

- [ ] **Step 1: Record baseline and back up**

```powershell
git status --porcelain | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\status-before-w5.txt
Copy-Item src/main.c C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\backups\w5\src_main.c.bak
Copy-Item src/bt/store.c C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\backups\w5\store.c.bak
Copy-Item src/bt/store.h C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\backups\w5\store.h.bak
```

Prior W1/W3/W4 designs (all applied cleanly before, tree unchanged since — verify by reading, do not assume):
- W1: brief `..\bcon-wab\briefs\task-1-brief.md` Steps 2–3 + diffs `..\bcon-wab\diffs\w1-main-vs-backup.diff`, `w1-store-h-vs-backup.diff` (deferred 3s one-shot + force path).
- No W3 content in this variant (timing replaces breadcrumb; keeps the diff small).

Work from: C:\pico-bcon. Touch ONLY src/main.c, src/bt/store.c, src/bt/store.h.

- [ ] **Step 2: Apply the W1 deferral verbatim** (force refactor + 3s one-shot + open-branch replacement + registration, exactly as in the W1 brief/diffs).

- [ ] **Step 3: Add timing around the store call (W5DIAG-marked, minimal)**

Inside the shared store path (where `tag_store_safe` is invoked for TAG_HOST — whether in the inner function or the force path, choose the single choke point covering the deferred write), wrap it:

```c
    /* W5DIAG: stall-vs-corruption discriminator. REMOVE after measurement. */
    probe_line("store begin");
    uint32_t w5_t0 = time_us_32();
    int w5_rc = tag_store_safe(tlv, ctx, TAG_HOST, addr, 6);
    uint32_t w5_dt = time_us_32() - w5_t0;
    {
        char w5m[48];
        snprintf(w5m, sizeof(w5m), "store dt_us=%lu rc=%d", (unsigned long)w5_dt, w5_rc);
        probe_line(w5m);
    }
```

Adapt variable names to the actual function under edit; keep the existing counter/log lines intact; use the existing `op.rc`-style result handling (do not change error semantics — preserve the original `!= 0 → return` behavior with the measured rc). `time_us_32` needs `pico/time.h` — verify include chain, add ONLY if the compiler requires, and report it. Every added line carries `/* W5DIAG */`.

- [ ] **Step 4: Compile the wireless build**

```powershell
cmake -S . -B $env:TEMP/wab-w5 -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0
cmake --build $env:TEMP/wab-w5 --target pico-bcon
```

SDK full paths if needed. Never reuse `build/`. Allow >= 600000 ms. Expected: exit 0, no BUMP flag.

- [ ] **Step 5: Save artifacts**

```powershell
Copy-Item $env:TEMP/wab-w5/pico-bcon.uf2 log/pico-bcon-w5-timed-store.uf2
git diff -- src/main.c | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\diffs\w5-main.diff
git diff --no-index -- C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\backups\w5\store.c.bak src/bt/store.c | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\diffs\w5-store.diff
```

(If store.h touched: third vs-backup diff for it.) All diff files non-empty, showing ONLY W1 hunks + timing lines.

- [ ] **Step 6: Restore and prove**

Restore all backed-up files; status-after identical to status-before; all hashes True; grep `W1DIAG|W3DIAG|W5DIAG|defer_host|store_host_force|store begin|scratch` in src/ → zero hits.

- [ ] **Step 7: Report** — write `C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\task-5-report.md` (both changes, build + exit code, UF2 path + size, diff inventory, restore evidence, self-review, concerns) PLUS the reading guide: "begin"-only → stall-in-write; both lines + ~2s death → post-write corruption (report the dt_us magnitude classes: <1ms program-like, tens-of-ms erase-like, seconds = pathological). DO NOT commit.
