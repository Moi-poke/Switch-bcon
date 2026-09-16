# Task 5 brief: Stable-identity build (AB5, build flags only)

No source change. Produce a wireless UF2 with `-DBD_ADDR_BUMP=0` (stable MAC identity) and prove the tree is untouched.

## Steps

- [ ] **Step 1: Record baseline**

```powershell
git status --porcelain | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\status-before-ab5.txt
```

Work from: C:\pico-bcon. Do NOT edit any source file.

- [ ] **Step 2: Compile the wireless build with `-DBD_ADDR_BUMP=0`**

```powershell
cmake -S . -B $env:TEMP/bcon-ab5 -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0 -DBD_ADDR_BUMP=0
cmake --build $env:TEMP/bcon-ab5 --target pico-bcon
```

Locate `ninja.exe` first (`Get-Command ninja.exe`; if absent use `C:\Users\moilo\.pico-sdk\ninja\v1.13.2\ninja.exe` via `-DCMAKE_MAKE_PROGRAM`; SDK-bundled cmake at `C:\Users\moilo\.pico-sdk\cmake\v4.3.4\bin\cmake.exe` if `cmake` is not on PATH, as prior tasks did). Never reuse the existing `build/` dir. Allow >= 600000 ms. Expected: exit 0 with a UF2 produced.

- [ ] **Step 3: Save artifact**

```powershell
Copy-Item $env:TEMP/bcon-ab5/pico-bcon.uf2 log/pico-bcon-ab5-bump0.uf2
```

- [ ] **Step 4: Verify tree untouched**

```powershell
git status --porcelain | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\status-after-ab5.txt
```

Expected: identical to Step 1 (no source file may appear modified beyond the pre-existing handoff state; `log/` is gitignored so the UF2 does not appear).

- [ ] **Step 5: Report** — write `C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\task-5-report.md` (build command + exit code, UF2 path + size, status-equality evidence, files changed: none, self-review, concerns) plus the EXPLICIT note that the diagnostic startup key+host wipe is still active in this UF2 (its removal belongs to Phase 3 permanent fixes, not this build). DO NOT commit.
