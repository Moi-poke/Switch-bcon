# W10 report: HAL 4-point breadcrumbs (rec#1) + 100ms timeout diagnostics (rec#2) 窶・task-12

Status: DONE_WITH_CONCERNS (concerns are scope/limitations, none blocking).
Scope: W10 diagnostics ONLY (rec#1/W10a crumbs-only, rec#2/W10b crumbs+100ms cap, plus a
non-diagnostic production UF2 for rec#3). No commits/pushes/PRs/branch ops. `pico-wakecon`
untouched. No secret key bytes. **No SDK/BTstack source edits** 窶・the HAL points are taken
via linker `--wrap`, so nothing outside the repo needs reverting.

## 1. Design: 4 points, no SDK edits

| Point | Where | Mechanism |
|---|---|---|
| P1 lockout request 逶ｴ蜑・| requester side, before `multicore_fifo_pop` handshake | `--wrap=multicore_lockout_start_timeout_us` |
| P2 lockout ack 逶ｴ蠕・| requester side, after the matching ack pop | same wrapper (return value/duration) |
| P3 ROM 蜻ｼ蜃ｺ逶ｴ蜑・| before `flash_range_program` / `flash_range_erase` | `--wrap=flash_range_program` / `--wrap=flash_range_erase` |
| P4 ROM 蜻ｼ蜃ｺ逶ｴ蠕・| after those return | same wrappers |

`--wrap` redirects the SDK objects' references at link time (verified in the .map: the
`__wrap_*` sections are kept and placed, so the SDK calls resolve to them; the link would
fail on `__real_*` otherwise). Recorder is NOLOAD + magic/CRC (W9 diag_rec pattern),
Core0-only writer, no printf/mutex/IRQ inside hooks.

Also recorded: `--wrap=flash_safe_execute` (rc + duration; `cap` counts calls where the HAL's
`UINT32_MAX` was replaced by `W10_TIMEOUT_MS`), lockout end counter (no ring entry).
D-gate (W9 recipe) reintroduced to force a second BCHO store per HID-open and thereby
deterministically reach the erase path (`delete_tag_until_offset` zero-fill program).

## 2. Artifacts (owner-facing)

UF2s in `log/`:

- `pico-bcon-w10a-crumb.uf2` 821760 B SHA256 `C59C2EE43EF0DD2FE1A072BC8DA7D90078FCBF1A04D9810A1DF4518BF392A5E7`
  (W10a: crumbs only, timeout unchanged) 竊・rec#1
- `pico-bcon-w10b-100ms.uf2` 821760 B SHA256 `5BB9F6862859BD43D56387F969D4C2A59B6BFE07543FBF870BF0246DA0AB763A`
  (W10b: same + `W10_TIMEOUT_MS=100`) 竊・rec#2 diagnostic (NOT a permanent fix)
- `pico-bcon-prod-wireless.uf2` 817152 B SHA256 `C6C35188CBFEF0F4698524A5B850C34AF78FE1E491FCA11B7BA0D0DA98D47ACD`
  (rec#3: current tree as-is, no diagnostics, wireless, 115200)

Build recipes (temp dirs, never `build/`; full-path tools):
`cmake -S . -B <dir> -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0 [-DW10_TIMEOUT_MS=100]`
then `cmake --build <dir> --target pico-bcon` 竊・BUILD EXIT=0 for all three.
Marker-only edits (CMake restructure to `w10diag.cmake`, two `/* W10DIAG */` lines in store.c)
were rebuilt and reproduced the same UF2 SHA256 窶・no functional drift.

## 3. Owner runbook 窶・reading the W10 dump

Lines (printed at boot, BEFORE TLV init/loads):

```
w10epoch post=<prev> current=<this>
w10hal lk req= ack= fail= end= fse ok= fail= cap=
w10rom p beg= end= e beg= end= last rc= dur=
w10ev n= idx= force gen= n=
w10e ep= ts= k= a=0x窶ｦ b=窶ｦ     (x32, oldest 竊・newest)
```

`k`: 1 INIT, 2 LK_REQ, 3 LK_ACK, 4 LK_END, 5 FSE_OK, 6 FSE_FAIL, 7 ROMP_BEG, 8 ROMP_END,
9 ROME_BEG, 10 ROME_END, 11 FORCE. Counters are cumulative across WDT reboots; per-event `ep`
identifies the boot (`ts` is per-boot `time_us_32`). Power cycle 竊・`post=0 current=1` (fresh).

Interpretation of a death (read the LAST ring entries):

- last `k=2` (LK_REQ) with no `k=3` 竊・**(a) Core1 never acked** the lockout; hang is the
  requester waiting (SDK `UINT32_MAX` 竕・49 days 竊・WDT at 2 s).
- `k=3` then `k=7`/`k=9` (ROM beg) with no `k=8`/`k=10` 竊・**(b) ROM/QMI stall** after a
  successful ack (the wrapped `flash_range_*` never returned).
- `k=3` with no `k=7`/`k=9` 竊・hang between ack and the ROM call (callback prologue /
  inner SDK bookkeeping).
- W10b only: `k=6` with `a=0xfffffffe` (rc=窶・) and `cap`>0 竊・the capped 100 ms lockout
  **timed out and returned** (SDK 2.3.0 shared-id protocol should leave the lockout usable);
  note whether the session recovered or re-hung later.
- Expected pre-death shape (if the erase path is the trigger): FORCE 竊・FSE + LK_REQ/ACK cycles
  for the normal store (no ROM_ERASE), then the forced store's LK_REQ/ACK 窶ｦ and the failure.
  `w10fire var=D` printing (or not) marks whether the forced store returned.

## 4. Restore evidence

Restored 4 files from `backups/w10/*` (`Copy-Item`), deleted `src/w10_diag.c`,
`src/w10_diag.h`, `w10diag.cmake` (Test-Path False). Hashes restored == Step-1 baselines:
main.c `F40AFE1A窶ｦ04918`, store.c `40456D82窶ｦC87726`, store.h `FF4B5284窶ｦFE340`,
CMakeLists `6E3628C3窶ｦB42D254`. `git status --porcelain` = 30 lines, identical to pre-work.
Marker grep `W10DIAG|w10_diag|w10diag` over `src/` + `CMakeLists.txt` 竊・zero hits.
Diffs retained: `diffs/w10-{main,store,store-h,cmake}-vs-backup.diff`, `w10-tracked.diff`,
`w10_diag.{c,h}.txt`, `w10diag.cmake.txt` (marker coverage mechanically verified:
added=12/13/1/7, unmarked=0; new files nonblank=178/13/9, unmarked=0).

## 5. Concerns / limitations

- C1: The 4 points are requester-side. The victim's own entry/exit of the handler is not
  directly observed; "ack" is inferred from the successful pop on core0 (that is exactly
  the P2 boundary requested).
- C2: W10b caps only calls that pass `UINT32_MAX` (the HAL's and store.c's outer wrapper).
  A cap that fires turns a would-be hang into `PICO_ERROR_TIMEOUT`; the HAL ignores the rc,
  so a partial record can be left in the bank (diagnostic-only wear/corruption; re-flash
  or KEY_DELETE before production use).
- C3: D-gate adds one forced BCHO write per HID-open (same as W9; diagnostic-only).
- C4: `--wrap` adds one call layer in the flash path; W10a/W10b share it, so the
  W10a-vs-W10b delta is only `W10_TIMEOUT_MS`.
- C5: Crumbs are Core0-only; a Core1-side stall is only visible via the absent ack.

## 6. rec#3 framing (for the owner's HW check)

The production UF2 is the current tree as-is. The measured op-in death mechanism needs the
store to erase an old record (the zero-fill program / additional `flash_safe_execute` in the
same op). That path is **rare in the natural flow but not harmless**: it is entered whenever
the tag already exists and the store is not deduped 窶・e.g. a console/BD_ADDR change (BCHO for
a different peer), Grip color save (`BCCL`), capture save (`BCW1`), wired-mode save (`BCWR`).
Expectation on the production build: ordinary reconnects dedupe (no write 竊・no mine); the
mine remains armed for the first erase-path traversal. This is a skeleton-key warning, not a
"production is fixed" claim.

(End of report)
