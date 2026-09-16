# Task-15 report: op-in re-verification build (fixed tree + HAL/loop crumbs, no D-gate)

Status: BUILT, AWAITING OWNER HW RUN. No commits/pushes/PRs/branch ops. `pico-wakecon`
untouched. No secret key bytes. No SDK/BTstack source edits.

## 1. Purpose and design

After the task-14 fix (outer `flash_safe_execute` removed → no nesting → no PRIMASK leak),
the remaining open question is whether the **op-in death** (young-bank S3 lockout race,
rec#1's (a)/(b)) also disappears. This build re-instruments the FIXED tree so a re-run
under the op-in precondition answers it:

- Base = fixed tree as-is (outer removed + `store_irq_guard` + BCON `pm=` stay).
- TEMP (reverted after build): the W10c recorder verbatim + VER bumped to 3 (fresh epochs;
  cross-flash resets) + `--wrap` link options + `w10_init()/w10_dump()` after the banner.
- **NO D-gate.** The original op-in observations (W8ep1–4, Phase-0 boot1, 9b-A epoch 1)
  were all NATURAL first stores, so the honest trigger is the prelude below, not a forced
  second write. (A D-gate would also do — it is not needed and would muddy the verdict.)

What the run decides (read the next boot's dump):

- `w10hal req>ack` (or `fail>0`) with the last ring event at LK_REQ → **op-in persists**:
  the 4 points split (a) Core1 no-ack (REQ without ACK) vs (b) ROM stall (ROM_BEG
  without ROM_END). Report immediately — the inner-handshake race is independent of the
  outer wrapper and needs its own fix.
- All points closed (`req=ack`, `beg=end`, outer `FSE_OK` last) + session survives with
  `pm=0` and SUBs → **op-in gone** (at least for this bank state; repeat the prelude
  2–3× to strengthen). The `w10loop`/`w10poll`/`w10hci` lines corroborate loop health.
- `irq leak healed` appearing would mean the fix does not hold (not expected; the
  single-level path cannot clobber the shared slot).

## 2. Artifact (owner-facing)

- `log/pico-bcon-opin-verify.uf2` 822272 B
  SHA256 `CFFB457DF492C4964BA06485E02C656B358EA36E6B2EF316DD4ECC541D40DFAE`
  (fixed tree + crumbs, no D-gate, wireless, 115200 baud, `W10_TIMEOUT_MS=0`).
  Recipe (temp dir, never `build/`): `cmake -S . -B <dir> -G Ninja
  -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0 -DW10_TIMEOUT_MS=0` → BUILD EXIT=0;
  wrap symbols verified kept in the .map.

## 3. Owner procedure (the op-in prelude — same as 9b/Phase-0)

1. Flash `pico-bcon-opin-verify.uf2`, open the log capture (UART0 115200).
2. Send KEY_DELETE over the DATA uart (115200 Bd): frame
   `[0xAB][0x33][0x00][SEQ=0x00][CRC8/SMBUS over 0x33,0x00,0x00]`
   (e.g. in Python with pyserial, reusing `poc_send.py`'s `crc8_smbus`:
   body=`bytes((0x33,0,0))`, frame=`bytes((0xAB,))+body+bytes((crc,))`).
   Expect `keys deleted (classic + host tag)` in the log (BCHO tombstoned, wroff kept).
3. USB power unplug/replug (clears NOLOAD; flash tombstones persist).
4. Fresh pairing from the Switch Change-Grip screen (non-bonded; `link keys=0` at boot).
5. Capture: either survival (`pm=0`, SUBs, no WDT) or the ~2 s death. KEEP CAPTURING past
   the reboot; paste 2–3 boots (the dump block at each banner top, especially the
   `w10hal`/`w10rom`/`w10poll`/`w10loop`/`w10hci` lines + the last `w10e` events).
6. Do NOT power-cycle between the death and the capture (NOLOAD crumbs survive only
   WDT/soft resets).

## 4. Restore evidence

TEMP reverted, fix kept: `src/w10_diag.c`, `src/w10_diag.h`, `w10diag.cmake` deleted
(Test-Path False ×3); `CMakeLists.txt` and `src/main.c` byte-identical to the fixed-tree
baseline (hash-compare vs task-15 backups: IDENTICAL). `store.c`/`store.h` untouched by
task-15 (no D-gate refactor needed). Marker grep
`W10DIAG|W10C|FIXVERIFY|w10_diag|w10diag` over `src/` + `CMakeLists.txt` → zero hits.
`git status --porcelain` = 30 lines (fix hunks live in already-modified/untracked files).
Diffs retained: `diffs/t15-{main,cmake}-vs-backup.diff` (added 5/7, unmarked 0),
`t15-tracked.diff`, `t15_diag.{c,h}.txt`, `t15_diag.cmake.txt` (nonblank 307/34/9,
unmarked 0). Production UF2 `pico-bcon-prod-fix1.uf2` rebuilt from the final tree and
verified byte-identical to the stored copy (SHA `2DCD81B6…65DF`), so it matches the tree
exactly (the only delta since its first build was a comment reword).

## 5. Concerns / limitations

- C1: a single survival does not prove absence (race, bank-state dependent) — repeat the
  prelude 2–3×; each fresh tombstone-bank boot is an independent trial.
- C2: if op-in persists, the fix stands for Death-B regardless (separate mechanisms,
  separately verified); do NOT re-add the outer wrapper (it reintroduces the leak).
- C3: VER 3 means the first boot after flashing shows `post=0 current=1` (empty);
  verdicts come from the second boot onward (compare consecutive WDT reboots).

(End of report)
