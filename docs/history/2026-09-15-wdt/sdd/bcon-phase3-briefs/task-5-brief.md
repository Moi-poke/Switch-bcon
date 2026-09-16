# P3 Task 5 brief: Finalize the Bluetooth identity (remove BUMP knob)

Delete the BD_ADDR_BUMP mechanism so the MAC is purely board-ID-derived and stable. Change is PERMANENT. Tree contains Tasks 1–4 hunks — do not touch them. Controller ruling (in ledger): proceed directly to deletion; AB5's failure is explained by SNIFF-off, and the HW batch will acceptance-test SUB arrival on the final address (fallback: bake `...:cb` if it fails — a separate scoped fix, not this task).

## Steps

- [ ] **Step 1: Read the mechanism**

Read `CMakeLists.txt` (`set(BD_ADDR_BUMP ...)` cache option + its compile definition) and `src/bt/link_conn.c` (`#ifndef BD_ADDR_BUMP` block + `probe_addr[5] += ...` line + comments). Confirm exact text.

- [ ] **Step 2: Save a verification UF2 first (for the HW batch)**

```powershell
cmake -S . -B $env:TEMP/p3t5verify -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0 -DBD_ADDR_BUMP=0
cmake --build $env:TEMP/p3t5verify --target pico-bcon
Copy-Item $env:TEMP/p3t5verify/pico-bcon.uf2 log/pico-bcon-p3t5-bump0-verify.uf2
```

Toolchain discovery as in prior tasks. Never reuse `build/`. Allow >= 600000 ms. Expected: exit 0. (This UF2 carries the current tree + BUMP=0 for the owner's HW acceptance run.)

- [ ] **Step 3: Delete the mechanism**

In `CMakeLists.txt`: remove the `set(BD_ADDR_BUMP ...)` cache-option block and the compile definition that forwards it (leave all other definitions intact).
In `src/bt/link_conn.c`: remove the `#ifndef BD_ADDR_BUMP` / `#define BD_ADDR_BUMP 0` / `#endif` block, the `probe_addr[5] = (uint8_t)(probe_addr[5] + (uint8_t)BD_ADDR_BUMP);` line, and the diagnostic comment attached to it. Keep the OUI + board-ID derivation lines byte-identical. Add one permanent comment line above `link_init`'s address derivation, e.g.:

```c
/* Device identity is fixed: OUI + unique board ID, never bumped (SNIFF-on + stable MAC is the proven combination). */
```

- [ ] **Step 4: Compile with NO `-DBD_ADDR_BUMP` flag at all**

```powershell
cmake -S . -B $env:TEMP/p3t5 -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0
cmake --build $env:TEMP/p3t5 --target pico-bcon
```

Expected: exit 0. Confirm the boot MAC in build output/logs equals the pure board-ID-derived address (record it in the report; do NOT paste any peer/secret material — own MAC is fine).

- [ ] **Step 5: Leave changes in place + record hunks**

Do NOT revert. Record the CMakeLists + link_conn.c hunks.

- [ ] **Step 6: Write the 5-line flash/test note** (owner executes hardware later)

Include: verification UF2 path (Step 2 artifact); final-address acceptance (clean Switch state → SUB arrival on `...:ca`-family address; reboot → same address, host remembered, no re-pair); what triggers the bake-`...:cb` fallback.

- [ ] **Step 7: Report** — write `C:\Users\moilo\AppData\Local\Temp\opencode\bcon-phase3\task-5-report.md` (mechanism deletion description, both builds + exit codes, hunk description, final MAC rule, flash/test note, self-review, concerns). DO NOT commit.
