# P3 Task 3 brief: Auth-fail stale-key drop (replaces startup wipe)

Delete the boot-time key+host wipe; drop keys automatically when authentication fails. Change is PERMANENT. The tree already contains Tasks 1–2 hunks — do not touch them.

## Steps

- [ ] **Step 1: Read the two spots**

Spot A — `handle_bt_ready` in `src/main.c`: the Task 4 wipe block (delete-all-link-keys + store_host_forget + `keys+host wiped (temp)` inside a `static bool wiped` guard), followed by the `link keys=%d` display and the `probe_host_known` branch.

Spot B — `packet_handler`'s `HCI_EVENT_AUTHENTICATION_COMPLETE` case in `src/main.c` (currently status display only). Read its exact text, variable names for status/address.

- [ ] **Step 2: Delete the wipe block, keep the display**

Delete Spot A entirely (the comment + the `{ static bool wiped; ... }` block). Keep the `link keys=%d` display line and everything after it untouched.

- [ ] **Step 3: Drop keys on authentication failure**

In the AUTH_COMPLETE case, after the existing status logging, add (adapting variable names to what Step 1 found):

```c
    if (auth_status != 0) {
        /* Stale key: forget it so the next attempt re-pairs cleanly. */
        gap_delete_all_link_keys();
        probe_line("auth fail: keys dropped, re-pair");
    }
```

Preferred over delete-all: a targeted per-peer drop IF BTstack exposes it — check `gap.h` (bundled BTstack headers) for `gap_drop_link_key_for_bd_addr`. If present, call it with the event's peer address instead of `gap_delete_all_link_keys()` and document the choice in the report. If absent, use delete-all as above. Either way, no other lines change in that case block.

- [ ] **Step 4: Compile the wireless build**

```powershell
cmake -S . -B $env:TEMP/p3t3 -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0 -DBD_ADDR_BUMP=1
cmake --build $env:TEMP/p3t3 --target pico-bcon
```

Toolchain discovery as in prior tasks. Never reuse `build/`. Allow >= 600000 ms. Expected: exit 0.

- [ ] **Step 5: Leave the change in place + record hunks**

Do NOT revert. Record which `git diff` hunks are Task 3's (wipe deletion + auth-case addition) vs earlier state.

- [ ] **Step 6: Write the 5-line flash/test note** (owner executes hardware later)

Include: UF2 location; stale-key scenario (pair → deregister Switch-side only → connect → expect one `auth fail` cycle with automatic drop then clean re-pair with SUB, no manual key delete, no reboot); pass/fail signals.

- [ ] **Step 7: Report** — write `C:\Users\moilo\AppData\Local\Temp\opencode\bcon-phase3\task-3-report.md` (implementation incl. targeted-vs-all decision, build command + exit code, hunk description, flash/test note, self-review, concerns). DO NOT commit.
