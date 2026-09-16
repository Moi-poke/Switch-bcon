# W6 brief: hci_dump-only visibility rebuild (death-session capture)

Re-add ONLY the hci_dump init/enable (no other diagnostics) to capture one death session with full HCI bytes. Purpose: byte-compare the encrypt→open window against the victorious AB1 log (which retains full HCI), to see exactly which exchange wedges. Full backup/restore discipline.

## Steps

- [ ] **Step 1: Record baseline and back up**

```powershell
git status --porcelain | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\status-before-w6.txt
Copy-Item src/main.c C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\backups\w6\src_main.c.bak
```

Work from: C:\pico-bcon. Touch ONLY src/main.c as specified below. First read the exact anchor regions (includes block ~line 25-35, init block where `hci_events`/`hci_add_event_handler` + `hci_set_bd_addr` live).

- [ ] **Step 2: Re-add hci_dump (4 lines + 2 include lines, W6DIAG-marked)**

(a) Add with the other includes:

```c
/* W6DIAG: death-session HCI capture only. REMOVE after comparison with AB1 victory dump. */
#include "hci_dump.h"
#include "hci_dump_embedded_stdout.h"
```

(If the exact BTstack include paths differ in this tree/SDK, resolve by reading how 4:24-era code included them — git history is unavailable for these untracked-era lines; instead verify by successful compile. If different paths are needed, report them.)

(b) Add immediately before `hci_set_bd_addr(probe_addr);` (or at the equivalent init point found in Step 1):

```c
    /* W6DIAG */ hci_dump_init(hci_dump_embedded_stdout_get_instance());
    /* W6DIAG */ hci_dump_enable_packet_log(true);
```

Nothing else changes. No SCR, no heartbeat, no markers, no behavior change.

- [ ] **Step 3: Compile the wireless build**

```powershell
cmake -S . -B $env:TEMP/wab-w6 -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0
cmake --build $env:TEMP/wab-w6 --target pico-bcon
```

SDK full paths if needed. Never reuse `build/`. Allow >= 600000 ms. Expected: exit 0, no BUMP flag.

- [ ] **Step 4: Save artifacts**

```powershell
Copy-Item $env:TEMP/wab-w6/pico-bcon.uf2 log/pico-bcon-w6-hcidump.uf2
git diff -- src/main.c | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\diffs\w6-main.diff
```

Diff must show ONLY the Step 2 additions (plus baseline noise — additionally persist a vs-backup isolation diff so reviewers see exactly the W6 lines).

- [ ] **Step 5: Restore and prove**

Restore src/main.c from backup; status-after identical to status-before; hash True; grep `W6DIAG|hci_dump` in src/ → zero hits.

- [ ] **Step 6: Report** — write `C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\task-6-report.md` (implementation, build + exit code, UF2 path + size, diff inventory, restore evidence, self-review, concerns) PLUS the comparison protocol: flash → pair to a WDT death → the owner byte-diffs the encrypt→HID-open window against `log/COM3_2026_09_14.22.32.18.050_ab1.txt` (victory, full HCI) watching PSM/MTU/FCS/config-option bytes and last-packet-before-silence. DO NOT commit.
