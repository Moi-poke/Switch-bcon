# W0 brief: Control build from current tree (no source change)

Produce the baseline UF2 for the WDT A/B series. No source edits at all.

## Steps

- [ ] **Step 1: Record baseline**

```powershell
git status --porcelain | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\status-before-w0.txt
```

Work from: C:\pico-bcon. Do NOT edit any file.

- [ ] **Step 2: Compile the wireless build**

```powershell
cmake -S . -B $env:TEMP/wab-w0 -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0
cmake --build $env:TEMP/wab-w0 --target pico-bcon
```

Locate ninja/cmake via SDK-bundled full paths if not on PATH (`C:\Users\moilo\.pico-sdk\cmake\v4.3.4\bin\cmake.exe`, `C:\Users\moilo\.pico-sdk\ninja\v1.13.2\ninja.exe` worked previously). Never reuse `build/`. Allow >= 600000 ms. Expected: exit 0. No `-DBD_ADDR_BUMP` flag (mechanism deleted).

- [ ] **Step 3: Save artifact + verify untouched**

```powershell
Copy-Item $env:TEMP/wab-w0/pico-bcon.uf2 log/pico-bcon-w0-control.uf2
git status --porcelain | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\status-after-w0.txt
```

Expected: status files identical; UF2 present.

- [ ] **Step 4: Report** — write `C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\task-0-report.md` (build command + exit code, UF2 path + size, status-equality evidence, files changed: none, self-review, concerns). DO NOT commit.
