# Task 1 brief: SNIFF-revert variant (AB1)

Single-variable trial: restore SNIFF in the default link policy (wakecon value), nothing else.

## Steps

- [ ] **Step 1: Record baseline and back up**

```powershell
git status --porcelain | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\status-before-ab1.txt
Copy-Item src/main.c C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\backups\ab1\src_main.c.bak
git diff -- src/main.c | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\diffs\ab1-baseline-check.diff
```

Work from: C:\pico-bcon. Do NOT touch any file except `src/main.c` (one line, see Step 2).

- [ ] **Step 2: Apply the single change**

In `src/main.c` (around line 1036), replace exactly:

```c
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH);
```

with:

```c
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH | LM_LINK_POLICY_ENABLE_SNIFF_MODE);
```

Do not touch the diagnostic comment above it or any other line.

- [ ] **Step 3: Compile the wireless build**

```powershell
cmake -S . -B $env:TEMP/bcon-ab1 -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0 -DBD_ADDR_BUMP=1
cmake --build $env:TEMP/bcon-ab1 --target pico-bcon
```

Locate `ninja.exe` first (`Get-Command ninja.exe`; if absent, search under `C:\Users\moilo\.pico-sdk` and pass `-DCMAKE_MAKE_PROGRAM=<path>`). Never reuse the existing `build/` dir. Allow >= 600000 ms. Expected: exit 0 with a UF2 produced.

- [ ] **Step 4: Save artifacts**

```powershell
Copy-Item $env:TEMP/bcon-ab1/pico-bcon.uf2 log/pico-bcon-ab1-sniff.uf2
git diff -- src/main.c | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\diffs\ab1-sniff.diff
```

Expected: the diff shows exactly the one-line change from Step 2.

- [ ] **Step 5: Restore and prove**

```powershell
Copy-Item C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\backups\ab1\src_main.c.bak src/main.c
git status --porcelain | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\status-after-ab1.txt
```

Expected: `status-after-ab1.txt` identical to `status-before-ab1.txt`; `git diff -- src/main.c` identical to the Step 1 baseline file.

- [ ] **Step 6: Report** — write `C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\task-1-report.md` (see report contract in dispatch prompt). DO NOT commit (plan constraint overrides any commit instruction).
