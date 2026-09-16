# P3 Task 1 brief: Normalize SNIFF (AB1 value made permanent)

One-line functional change + comment normalization in `src/main.c`. This change is PERMANENT (stays in tree for later tasks). Do NOT revert it.

## Steps

- [ ] **Step 1: Read the current block**

Read `src/main.c` around the `gap_set_default_link_policy_settings` call. Current diagnostic text:

```c
    // Task 4診断用の一時措置: SNIFF受容を外す (open後SNIFF突入とWDT死の因果切り分け)。
    // Switch主導のSNIFF突入直後に共有バス転送が止まる疑い。ROLE_SWITCHは維持。
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH);
    gap_set_allow_role_switch(true);
```

- [ ] **Step 2: Replace with the permanent form (exact text)**

```c
    // Switch 2 requires SNIFF acceptance: without it the console never sends
    // SUB after HID open and drops the link with 0x13 after ~1s (AB1 proven).
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH |
                                         LM_LINK_POLICY_ENABLE_SNIFF_MODE);
    gap_set_allow_role_switch(true);
```

No other lines change. No other files change.

- [ ] **Step 3: Compile the wireless build**

```powershell
cmake -S . -B $env:TEMP/p3t1 -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0 -DBD_ADDR_BUMP=1
cmake --build $env:TEMP/p3t1 --target pico-bcon
```

Locate `ninja.exe` first (`Get-Command ninja.exe`; if absent use `C:\Users\moilo\.pico-sdk\ninja\v1.13.2\ninja.exe` via `-DCMAKE_MAKE_PROGRAM`; SDK cmake at `C:\Users\moilo\.pico-sdk\cmake\v4.3.4\bin\cmake.exe` if needed). Never reuse the existing `build/` dir. Allow >= 600000 ms. Expected: exit 0.

- [ ] **Step 4: Leave the change in place + record the diff**

Do NOT revert. Record: `git diff --stat` and `git diff -- src/main.c` summary (which hunks are yours vs pre-existing handoff hunks). Save nothing to the workspace except your report (diffs stay in the tree; reviewer reads them via git).

- [ ] **Step 5: Write the 5-line flash/test note** (owner executes hardware later)

Include: UF2 location in temp build dir, flash procedure reference (BOOTSEL drag-drop), Switch prep (unpair + power cycle + Change-Grip), expected log lines (`SUB=0x02`, full handshake, no `0x13`), and what failure looks like.

- [ ] **Step 6: Report** — write `C:\Users\moilo\AppData\Local\Temp\opencode\bcon-phase3\task-1-report.md` (implementation, build command + exit code, diff hunk description, flash/test note, self-review, concerns). DO NOT commit.
