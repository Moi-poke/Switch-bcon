# P3 Task 6R report: Revert grip colors to original defaults (TDD: RED → GREEN)

Exact reverse of Task 6. No commits, no reverts of my own changes (left in place).

## Step 1 — RED: test expectation restored, failing run FIRST

Test edit only (`tests/host/test_usb.c`): replaced the Task 6 stock-black
expectation with the original:

```c
CHECK(n == 64 && out[13] == 0x90 && out[14] == 0x10 && out[19] == 12 &&
      out[20] == 0x82 && out[21] == 0x82, "10 SPI color 6050");
```

No source files touched at this point (`src/proto/spi.c` still held the
Task 6 table `32 32 32 FF FF FF 46 46 46 ...`).

Commands (VS 18 BuildTools vcvars64 shell, VS-bundled CMake — `build-host/`
was already configured with generator `Visual Studio 18 2026`, so no
reconfigure was needed or done; workdir `C:\pico-bcon`):

```powershell
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && "<...>\CMake\CMake\bin\cmake.exe" --build build-host --config Debug'
cmd /c '"..." && "<...>\CMake\CMake\bin\ctest.exe" --test-dir build-host -C Debug -V'
```

Relevant failing output (RED evidence):

```text
2:   PASS 02 device info (fw 03 48, tail 02)
2:   FAIL 10 SPI color 6050
...
2: RESULT: HAS FAILURES (1 failures)
2/3 Test #2: usb ..............................***Failed    0.03 sec
...
67% tests passed, 1 tests failed out of 3
The following tests FAILED:
          2 - usb (Failed)
```

Why expected: only `10 SPI color 6050` failed; protocol 1/1, config 1/1,
and every other usb check passed — exactly the single-failure RED the
brief predicts.

Process note (recovered, no impact on result): my first edit went through
the VSCode bridge (`edit_replace` returned `saved:false`), and the
immediately following build+test run passed 3/3 on the STALE binary —
disk still held the old test text (verified via disk-level
`Select-String`). I re-applied the identical hunk with a disk-level edit,
verified disk content plus bridge buffer/disk hash sync (`dbef03dc...`),
rebuilt (`test_usb.c` recompiled), and re-ran: the RED above. Same
buffer-vs-disk trap as Task 3's report; bridge saves must be
disk-verified before building.

## Step 2 — GREEN: original defaults restored

`src/proto/spi.c` only: replaced the Task 6 table + 4-line
jc_toolkit conformance comment with the original (no comment):

```c
uint8_t spi_color_6050[13] = {
    0x82, 0x82, 0x82, 0x0F, 0x0F, 0x0F,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00
};
```

Untouched per brief: byte 12 (`0x00`), store/load, COLOR_SET path,
`0x02` tail, `0x601B`, every other file.

## Step 3 — GREEN proof: full host suite 3/3 ALL PASS

Same build/test commands as RED (rebuild recompiled `spi.c` +
`test_usb.vcxproj`). Relevant passing output:

```text
2:   PASS 10 SPI color 6050
...
2: RESULT: ALL PASS (0 failures)
2/3 Test #2: usb ..............................   Passed    0.03 sec
...
100% tests passed, 0 tests failed out of 3
```

## Step 4 — Wireless build (no BUMP flag), exit 0

```powershell
& "C:\Users\moilo\.pico-sdk\cmake\v4.3.4\bin\cmake.exe" -S . -B "$env:TEMP\p3t6r" -G Ninja "-DCMAKE_MAKE_PROGRAM=C:/Users/moilo/.pico-sdk/ninja/v1.13.2/ninja.exe" -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0
& "C:\Users\moilo\.pico-sdk\cmake\v4.3.4\bin\cmake.exe" --build "$env:TEMP\p3t6r" --target pico-bcon
```

Configure: PICO_SDK_PATH `C:/Users/moilo/.pico-sdk/sdk/2.3.0`, board
`pico2_w`, "Build files have been written to:
C:/Users/moilo/AppData/Local/Temp/p3t6r". Build: `[228/228] Linking CXX
executable pico-bcon.elf`, final `$LASTEXITCODE` = 0. Fresh temp dir
`p3t6r`; existing `build/` untouched. No `-DBD_ADDR_BUMP` passed.

## Step 5 — Hunks (left in tree, NOT reverted)

1. `tests/host/test_usb.c` — `10 SPI color 6050 (stock black)` CHECK
   (5-line, full `out[20..28]` = `32 32 32 FF FF FF 46 46 46`) →
   original 2-line CHECK (`out[20] == 0x82 && out[21] == 0x82`,
   label `"10 SPI color 6050"`).
2. `src/proto/spi.c` — Task 6 table (`32 32 32 FF FF FF 46 46 46 46 46
   46 00` + 4-line conformance comment) → original table
   (`82 82 82 0F 0F 0F FF FF FF FF FF FF 00`, no comment; byte 12
   still `0x00`).

## Files changed

- `tests/host/test_usb.c` (test expectation only)
- `src/proto/spi.c` (color defaults + comment removal only)

## Self-review

- `git diff --exit-code -- tests/host/test_usb.c src/proto/spi.c` →
  exit 0: both files now byte-match `HEAD` (`c366bf4`), which already
  carries the original values (`git show HEAD:...` confirms `0x82`
  table + original CHECK). I.e. Task 6 lived only as uncommitted
  working-tree state, and this revert restores exactly HEAD content —
  so "exactly the two reverse hunks, nothing else" holds relative to
  the pre-task tree, and relative to HEAD the two files are clean.
- `git status --short`: the remaining modifications (`CMakeLists.txt`,
  `spec/protocol_v3.md`, `src/main.c`, `poc_send.py`, `protocol.h`,
  `spi.h`, `usb_wired.c`, `tests/host/CMakeLists.txt`, untracked
  `docs/`, `src/bt/`, `dispatch.*`, `test_config.c`) are the
  pre-existing Tasks 1–5/7 hunks — I touched none of them. In
  particular `src/proto/spi.h` (listed modified) was NOT touched by me;
  verified I edited only the two brief-named files.
- `C:\Users\moilo\pico-wakecon` untouched. No commits, no pushes, no PRs.
- Disk-verified both edited regions (`Select-String` + `read`); host
  suite 3/3; wireless build exit 0.

## Concerns

- None about the change itself. One process concern (recovered, see
  Step 1 note): VSCode bridge edits can report success without flushing
  to disk — always disk-verify before building.
