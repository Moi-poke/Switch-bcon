# Task 0 (W0 control build) — report

## Build command + exit code
Work dir: `C:\pico-bcon` (no source edits; isolated build dir, never reused `build/`).

Configure (exit 0):
```powershell
& "C:\Users\moilo\.pico-sdk\cmake\v4.3.4\bin\cmake.exe" -S . -B "$env:TEMP/wab-w0" -G Ninja "-DCMAKE_MAKE_PROGRAM=C:\Users\moilo\.pico-sdk\ninja\v1.13.2\ninja.exe" -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0
```
Build (exit 0, 228/228 steps, `BUILD_EXIT:0`):
```powershell
& "C:\Users\moilo\.pico-sdk\cmake\v4.3.4\bin\cmake.exe" --build "$env:TEMP/wab-w0" --target pico-bcon
```
Notes: `cmake` not on PATH so SDK-bundled full paths used per brief; `-DCMAKE_MAKE_PROGRAM` added to locate Ninja. No `-DBD_ADDR_BUMP` flag (mechanism deleted). PICO_SDK_PATH auto-detected as `C:/Users/moilo/.pico-sdk/sdk/2.3.0`, board `pico2_w`, Release. HEAD `c366bf4` unchanged.

## UF2 path + size
- Build output: `C:\Users\moilo\AppData\Local\Temp\wab-w0\pico-bcon.uf2` — 816640 bytes, SHA256 `2629D6DB6F7D685CC6778AD9A02B4C192A24B74CAD2F3A011854E810C69DE59E`
- Saved artifact: `C:\pico-bcon\log\pico-bcon-w0-control.uf2` — 816640 bytes, SHA256 `2629D6DB6F7D685CC6778AD9A02B4C192A24B74CAD2F3A011854E810C69DE59E` (hashes match; `Copy-Item` per brief Step 3)
- `log/` is gitignored (`.gitignore:13:log/`), so the artifact does not dirty git status.

## Status-equality evidence
- `status-before-w0.txt` and `status-after-w0.txt` in `C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\` are byte-identical: both SHA256 `250472FDB4119BB815ED09BACD211D2ABB67F8B6E1BDF9AD6BB5C1B9CBCEB4CD`.
- `Compare-Object` before vs after: no differences (empty output).
- Both contain the same pre-existing tree state (9 modified tracked files + 6 untracked entries); `git status --porcelain` re-run after the copy matches exactly.
- No commits made (`git log` still at `c366bf4`).

## Files changed
None. Zero source modifications; no files created/modified in `C:\pico-bcon` except the gitignored artifact `log/pico-bcon-w0-control.uf2`. Build outputs isolated to `C:\Users\moilo\AppData\Local\Temp\wab-w0\`. `C:\Users\moilo\pico-wakecon` untouched.

## Self-review
- Confirmed `git status --porcelain` before == after (hash equality + empty diff).
- Confirmed no `git commit`/`push`/`PR` performed.
- Confirmed no editor/file writes to source; only `Copy-Item` of UF2 into ignored `log/` plus status/report files under the `bcon-wab` workspace.
- Confirmed build flags exactly per brief (`-DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0`, Ninja, isolated `$env:TEMP/wab-w0`).
- Confirmed artifact present with matching size/hash.

## Concerns
- None blocking. Pre-existing dirty tree (listed above) was left untouched; W1/W3 must build from this same tree state for a valid comparison — flag if the tree changes before those builds.
- Minor deviation: added `-DCMAKE_MAKE_PROGRAM=<sdk ninja>` since `cmake`/`ninja` are not on PATH; toolchain and flags otherwise exactly per brief.
