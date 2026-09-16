# Task 2 brief: Slow-start variant, 1Hz until first SUB (AB2)

Single-variable trial (NXBT-style slow start): pace empty reports at 1 Hz until the first SUB (Output report) arrives; afterwards keep the current interval logic. Ruling applied: NO extern declaration is needed — `probe_out_report_count` is already declared `extern uint32_t` in `src/bt/hid.h:31`, which `src/main.c` includes. Single-spot change only.

## Steps

- [ ] **Step 1: Record baseline and back up**

```powershell
git status --porcelain | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\status-before-ab2.txt
Copy-Item src/main.c C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\backups\ab2\src_main.c.bak
git diff -- src/main.c | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\diffs\ab2-baseline-check.diff
```

Work from: C:\pico-bcon. Do NOT touch any file except `src/main.c` (Step 2 spot only).

- [ ] **Step 2: Apply the change (one spot)**

In `empty_handler` in `src/main.c`, replace exactly:

```c
    btstack_run_loop_set_timer(ts, probe_send_interval_ms());
```

with:

```c
    /* AB2: NXBT-style slow start — 1 Hz until the first SUB arrives. */
    uint32_t interval_ms = (probe_out_report_count == 0u) ? 1000u : probe_send_interval_ms();
    btstack_run_loop_set_timer(ts, interval_ms);
```

No other lines change. No new declarations (uses existing `probe_out_report_count` from `bt/hid.h` and existing `probe_send_interval_ms()`).

- [ ] **Step 3: Compile the wireless build**

```powershell
cmake -S . -B $env:TEMP/bcon-ab2 -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0 -DBD_ADDR_BUMP=1
cmake --build $env:TEMP/bcon-ab2 --target pico-bcon
```

Locate `ninja.exe` first (`Get-Command ninja.exe`; if absent, search under `C:\Users\moilo\.pico-sdk` and pass `-DCMAKE_MAKE_PROGRAM=<path>`). The prior task (T1) successfully used the SDK-bundled cmake by full path — reuse whatever toolchain discovery worked there if needed. Never reuse the existing `build/` dir. Allow >= 600000 ms. Expected: exit 0 with a UF2 produced.

- [ ] **Step 4: Save artifacts**

```powershell
Copy-Item $env:TEMP/bcon-ab2/pico-bcon.uf2 log/pico-bcon-ab2-slowstart.uf2
git diff -- src/main.c | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\diffs\ab2-slowstart.diff
```

Expected: the diff shows exactly the Step 2 change.

- [ ] **Step 5: Restore and prove**

```powershell
Copy-Item C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\backups\ab2\src_main.c.bak src/main.c
git status --porcelain | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\status-after-ab2.txt
(Get-FileHash src/main.c).Hash -eq (Get-FileHash C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\backups\ab2\src_main.c.bak).Hash
```

Expected: status files identical; SHA256 hashes equal.

- [ ] **Step 6: Report** — write `C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\task-2-report.md` (implementation, build command + exit code, UF2 path + size, diff path + content summary, restore evidence with hashes, files changed, self-review, concerns). DO NOT commit.
