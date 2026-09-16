# W4 brief: Deferred flash + breadcrumb combined (wedge-signature capture)

Apply W1's deferred-write change AND W3's breadcrumb instrumentation together, build one UF2, then revert both fully. Purpose: guarantee a flash-write death WITH post-mortem numbers identifying where the loop wedges.

## Steps

- [ ] **Step 1: Record baseline and back up**

```powershell
git status --porcelain | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\status-before-w4.txt
Copy-Item src/main.c C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\backups\w4\src_main.c.bak
Copy-Item src/bt/store.c C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\backups\w4\store.c.bak
Copy-Item src/bt/store.h C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\backups\w4\store.h.bak
```

Reference designs (read them; they applied cleanly before, tree is unchanged since):
- W1: brief `..\bcon-wab\briefs\task-1-brief.md` Steps 2–3 + exact diffs `..\bcon-wab\diffs\w1-main-vs-backup.diff`, `w1-store-h-vs-backup.diff`.
- W3: brief `..\bcon-wab\briefs\task-3-brief.md` Step 2 (incl. scratch zero-user gate — re-run the gate; T4 removal still holds but VERIFY again).

Work from: C:\pico-bcon. Touch ONLY src/main.c, src/bt/store.c, src/bt/store.h.

- [ ] **Step 2: Apply W1 change verbatim** (force-refactor + 3s one-shot deferral + registration + open-branch replacement, W1DIAG markers NOT needed — reuse the exact W1 hunks so the combined diff stays reviewable; the W3 markers distinguish the new part).

- [ ] **Step 3: Apply W3 change verbatim** (all W3DIAG-marked lines per the W3 brief, after re-passing the scratch gate).

- [ ] **Step 4: Compile the wireless build**

```powershell
cmake -S . -B $env:TEMP/wab-w4 -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0
cmake --build $env:TEMP/wab-w4 --target pico-bcon
```

SDK full paths if needed. Never reuse `build/`. Allow >= 600000 ms. Expected: exit 0, no BUMP flag.

- [ ] **Step 5: Save artifacts**

```powershell
Copy-Item $env:TEMP/wab-w4/pico-bcon.uf2 log/pico-bcon-w4-defer-crumb.uf2
git diff -- src/main.c | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\diffs\w4-main.diff
git diff --no-index -- C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\backups\w4\store.c.bak src/bt/store.c | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\diffs\w4-store.diff
```

Both diff files must be non-empty. (store.h change, if any, must appear in neither — store.h is untracked; if touched, persist a third vs-backup diff for it.)

- [ ] **Step 6: Restore and prove**

Restore all three files from backups; status-after identical to status-before; all three hashes True; grep `W1DIAG|W3DIAG|defer_host|store_host_force|scratch` in src/ → zero hits.

- [ ] **Step 7: Report** — write `C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\task-4-report.md` (both changes summarized, build + exit code, UF2 path + size, diff inventory, restore evidence, self-review, concerns) PLUS the post-mortem reading guide (copy from the W3 report, noting the ev-field caveat). DO NOT commit.
