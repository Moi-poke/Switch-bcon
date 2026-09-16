# W10c report: Death-B 2s-window crumbs on top of W10a (HAL 4 points + D-gate) — task-13

Status: DONE_WITH_CONCERNS (concerns are scope/limitations, none blocking).
Scope: W10c = W10a VERBATIM + the Death-B 2s-window instrument below. No commits/pushes/PRs/
branch ops. `pico-wakecon` untouched. No secret key bytes. **No SDK/BTstack source edits**
(same --wrap mechanism as W10a). The rec#1 HAL crumbs, the D-gate, and the `W10_TIMEOUT_MS`
hook all behave exactly as in W10a; only the instrument is extended.

## 1. Why W10c (context)

The W10a HW run (owner, 2026-09-16 ~02:38) did NOT reproduce the op-in death. Instead it gave
**Death-B deterministically (~6/6 epochs)**: the forced BCHO store (value/header/erase-zero:
3 × 256 B page programs, ~485–491 µs each) completes, all 27/27 lockouts acked (fail=0),
all 21/21 programs finished, outer `FSE_OK` (3837–3859 µs) as the last HAL event, then
~2.0 s of silence and a WDT reset (`wdt=1 host=1`). So the HAL points — P1/P2/P3/P4 —
are ALL CLOSED in this state and say nothing about the death. The death window (complete →
silence) is unmeasured. W10c fills exactly that window, using the W9-proven breadcrumb set.
(W10b's 100 ms cap is ON HOLD: it can only fire on a lockout that never acks, which does not
occur in this state. rec#2 therefore waits for the op-in precondition / young-bank run.)

## 2. Design: the 2s-window crumbs (all marked W10C)

New NOLOAD snapshot block in `w10_diag.c` (VER bumped to 2 → cross-flashing W10c resets the
recorder instead of misparsing a W10a/b record). Fast-path mutators update counters only
(no ring push — 1 ms cadence would flood the 32-entry HAL ring; CRC refreshed on every
mutation so a death at any instant leaves a valid record):

- `w10_poll_stage` (5 stages at fixed points in `poll_tick`/`usb_handler`): ENTER →
  BODY_DONE (inbox drained) → WDT_DONE (watchdog fed) → RETURN_IMMINENT, + RETURNED (after
  `poll_tick` returns). 8-deep trail; a wedge inside `poll_tick` reads as ENTER without RETURN.
- `w10_tmr_svc` / `w10_tmr_add` on the three owned timers (usb/empty/stats): per-timer last
  FIRE timestamp + FIRE/ADD counts (+ last re-arm interval).
- `w10_loop_sample` post-poll in `usb_handler` (1 ms): `loop_us` + PUBLIC timer-list walk
  (capped at 16, read-only) → `tmr_n`/`next_to`/`first_to`/`trunc` + PRIMASK/BASEPRI (`pm`/`bp`;
  0/0 = IRQs open). Own `usb_timer` is excluded by construction (already dequeued).
- `w10_hci_ev` (packet_handler entry), `w10_hci_tx_req` / `w10_hci_tx_grant` (can-send-now
  request/grant): RX/TX liveness counters.

hci_dump stays OFF (counters carry the decision-table inputs; less noise, smaller delta).
The blink (reconnect) timer is deliberately NOT instrumented.

## 3. Artifact (owner-facing)

- `log/pico-bcon-w10c-deathb.uf2` 824832 B
  SHA256 `61F303A4247348CE5255DAC8C68FFDE32DAB9D9BF2F6722AFDF6EA769C8120D5`
  (wireless, 115200 baud, D-gate + HAL crumbs + 2s-window crumbs; `W10_TIMEOUT_MS=0`).
  Recipe (temp dir, never `build/`): `cmake -S . -B <dir> -G Ninja
  -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0 -DW10_TIMEOUT_MS=0` → BUILD EXIT=0;
  wrap symbols verified kept in the .map (`__wrap_flash_safe_execute`,
  `__wrap_multicore_lockout_start_timeout_us`, `__wrap_flash_range_program`, `w10_loop_sample`).

## 4. Owner runbook — reading the W10c dump

The W10a lines (`w10epoch`/`w10hal`/`w10rom`/`w10ev`/`w10e…`) are unchanged. New block:

```
w10poll last= n= trail=…
w10tmrsvc usb= empty= stats= fire=…/…/… add=…/…/…
w10loop us= tmr_n= next_to= first_to= trunc= pm= bp=
w10hci rx_n= rx_last=0x… tx_req= tx_grant=
```

Run = W10a recipe (flash, connect from the Switch — a plain reconnect suffices since the
D-gate fires per HID-open; let it die ~2 s; KEEP CAPTURING past the reboot; 2–3 boots).
Expected death shape (if it repeats the W10a run): outer `FSE_OK` as the last `w10e`,
then `w10loop us` frozen at ~the store time. Read the last lines as:

| Observation | Meaning |
|---|---|
| `w10poll last=5` (RETURNED) but `w10loop us` ≈ store time, then silence | poll completed the last time; the loop died with/just after the store return |
| `w10poll trail` = 1,2,3 (ENTER/BODY/WDT, no RETURN) | wedge INSIDE `poll_tick` (FX/flush/USB task/reboot-check), not in the BT callback |
| `w10loop pm=1` (or bp≠0) at the last sample | IRQs masked at the death instant (IRQ-killed — compare against W9's `w9s12` reading) |
| `tmr_n`→0, `next_to`=-1 | timer list emptied/vanished after the store |
| `trunc=1` or absurd `first_to` | timer-list corruption (cap hit) |
| `w10hci rx_n` advancing with `w10poll` frozen | pump wedged but HCI RX alive (BTstack stuck above HCI) |
| one `svc_us` stuck while the others advance | that timer's path wedged |
| `tx_req` advancing with no `tx_grant` | can-send never granted (controller-side stall) |
| `tmr_n`>0 + sane `next_to` + `pm`/`bp`=0/0 | loop didn't die by timer-list/IRQ — look past the crumbs (needs Phase-2 design) |

One number to quote from the run: `w10loop us` − (last `FSE_OK` ts). ≈0 means the loop died
with the store; >>0 means the loop survived the return and died later in a plain poll.

## 5. Restore evidence

Restored 4 files from `backups/w10c/*` (`Copy-Item`); deleted `src/w10_diag.c`,
`src/w10_diag.h`, `w10diag.cmake` (Test-Path False ×3). Hashes restored == Step-1 baselines:
main.c `F40AFE1A…04918`, store.c `40456D82…C87726`, store.h `FF4B5284…FE340`,
CMakeLists `6E3628C3…B42D254`. `git status --porcelain` = 30 lines, identical to pre-work.
Marker grep `W10DIAG|W10C|w10_diag|w10diag` over `src/` + `CMakeLists.txt` → zero hits.
Diffs retained: `diffs/w10c-{main,store,store-h,cmake}-vs-backup.diff` (added 30/13/1/7,
unmarked 0; markers are W10DIAG for the HAL/D-gate set and W10C for the loop set),
`w10c-tracked.diff`, `w10c_diag.{c,h}.txt`, `w10c_diag.cmake.txt`
(new-file nonblank 307/34/9, unmarked 0).

## 6. Concerns / limitations

- C1: per-1 ms `w10_loop_sample` does an FNV pass over the ~1.3 KB NOLOAD state + a capped
  list walk — same cost class as W9's accepted C1 (diagnostic only, fully reverted).
- C2: hci_dump is OFF by design (see §2); byte-level RX/TX comparison vs the AB1 victory
  log is therefore not available from this run.
- C3: only the three owned timers are instrumented; the (reconnect) blink timer is not.
- C4: W10c carries the W10a recorder layout only at VER 2 — do NOT compare W10a/b `w10e`
  ring offsets numerically; the `k` codes are unchanged.
- C5: The D-gate remains (doubles BCHO wear per HID-open, forces the erase path —
  that is the Death-B trigger under test).
- C6: one build → two verdict tracks (HAL closure + 2s-window). If the run instead shows
  the op-in signature (SDK write+Erase logs, then silence, `w10hal req>ack`), the HAL 4
  points answer (a)/(b) as designed.

(End of report)
