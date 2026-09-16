# W3 brief: IRQ breadcrumb locator (Death-A test)

Add a loop-independent post-mortem recorder that identifies WHERE the run loop wedges. Diagnostic-only, fully marked for later removal. Full backup/restore discipline: UF2 + diff + byte-restore + proof.

## Steps

- [ ] **Step 1: Record baseline, back up, and gate scratch usage**

```powershell
git status --porcelain | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\status-before-w3.txt
Copy-Item src/main.c C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\backups\w3\src_main.c.bak
git diff -- src/main.c | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\diffs\w3-baseline-check.diff
```

Then grep the whole repo for existing `watchdog_hw->scratch` users:

```powershell
Get-ChildItem -Recurse -File -Include *.c,*.h src | Select-String -Pattern "scratch"
```

GATE: proceed only if ZERO hits in `src/` (T4 removed them all). If any hit exists, STOP and report BLOCKED with the hits (do not pick colliding indices).

Work from: C:\pico-bcon. Touch ONLY src/main.c as specified below.

- [ ] **Step 2: Add the breadcrumb apparatus (exact code)**

All added lines carry a `/* W3DIAG */` marker. Placement: near the other static declarations / next to `usb_timer` setup / inside `poll_tick` / at `packet_handler` entry / in the boot banner area (read exact anchors first; adapt placement minimally, never reorder existing logic).

```c
/* W3DIAG: post-mortem wedge locator. IRQ-driven, run-loop independent.
 * REMOVE after the WDT root cause is found. Uses watchdog scratch regs. */
#define W3_MAGIC 0xC0DECAFEu
static void w3_irq_tick(void) /* called from 100ms repeating timer (IRQ context) */
{
    watchdog_hw->scratch[1]++;
    watchdog_hw->scratch[0] = W3_MAGIC;
}
```

In `poll_tick`, first line of body:

```c
    watchdog_hw->scratch[2]++; /* W3DIAG: poll entry count */
```

After the inbox-drain loop (before `exec_fx`):

```c
    watchdog_hw->scratch[3] = 1u; /* W3DIAG: stage — inbox drained */
```

Just before `watchdog_update()`:

```c
    watchdog_hw->scratch[3] = 2u; /* W3DIAG: stage — tasks done, about to feed */
```

In `packet_handler`, first line of body:

```c
    watchdog_hw->scratch[4] = (uint32_t)ev + 1u; /* W3DIAG: last HCI event (+1; 0 = none yet) */
```

(`ev` is the existing local holding `hci_event_packet_get_type(packet)` — place the line AFTER its assignment; if the handler's structure differs, read it first and place equivalently, reporting the exact lines.)

Registration next to the other timer setups in main (one-shot code, runs once):

```c
    /* W3DIAG */ add_repeating_timer_ms(100, w3_irq_cb, NULL, &s_w3_timer);
```

with file-static `static repeating_timer_t s_w3_timer;` and a thin `static bool w3_irq_cb(repeating_timer_t *rt)` wrapper returning true that calls `w3_irq_tick()`. (If `add_repeating_timer_ms` needs a different callback signature in this SDK, match it and report.)

Boot dump: in the boot banner area after the `wdt=` line is printed, add:

```c
    /* W3DIAG: post-mortem from previous boot (survives WDT reboot). */
    if (watchdog_hw->scratch[0] == W3_MAGIC) {
        char w3m[128];
        snprintf(w3m, sizeof(w3m), "w3pm irq=%lu poll=%lu stage=%lu ev=0x%02lx",
                 (unsigned long)watchdog_hw->scratch[1],
                 (unsigned long)watchdog_hw->scratch[2],
                 (unsigned long)watchdog_hw->scratch[3],
                 (unsigned long)(watchdog_hw->scratch[4] == 0u ? 0u : watchdog_hw->scratch[4] - 1u));
        probe_line(w3m);
    }
```

Requires: `watchdog_hw` visible (structs/watchdog.h — T4 may have removed that include; re-add it WITH a W3DIAG comment if the compiler needs it), `stdio.h` for snprintf (verify), `pico/time.h` rep-timer API (verify include chain; add only if required).

- [ ] **Step 3: Compile the wireless build**

```powershell
cmake -S . -B $env:TEMP/wab-w3 -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0
cmake --build $env:TEMP/wab-w3 --target pico-bcon
```

SDK full paths if needed. Never reuse `build/`. Allow >= 600000 ms. Expected: exit 0.

- [ ] **Step 4: Save artifacts**

```powershell
Copy-Item $env:TEMP/wab-w3/pico-bcon.uf2 log/pico-bcon-w3-crumb.uf2
git diff -- src/main.c | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\diffs\w3-main.diff
```

The diff must show ONLY the Step 2 additions. If the diff is polluted by baseline noise, additionally persist `git diff -U10` hunks... no — instead ALSO save a vs-backup isolation: since main.c is TRACKED, `git diff` mixes handoff baseline; additionally write a focused evidence file listing the added line numbers (from the report's self-review read-back). The reviewer will scope to W3DIAG-marked lines.

- [ ] **Step 5: Restore and prove**

```powershell
Copy-Item C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\backups\w3\src_main.c.bak src/main.c
git status --porcelain | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\status-after-w3.txt
(Get-FileHash src/main.c).Hash -eq (Get-FileHash C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\backups\w3\src_main.c.bak).Hash
```

Expected: status files identical; hash True. Also grep `W3DIAG` in src/ → zero hits after restore.

- [ ] **Step 6: Report** — write `C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\task-3-report.md` (scratch-gate result, implementation per site, build + exit code, UF2 path + size, diff description, restore evidence, W3DIAG-absence proof, files changed, self-review, concerns) PLUS a short "how to read the post-mortem" section: poll frozen + stage value + last ev + irq advancing = wedge signature guide for the owner. DO NOT commit.
