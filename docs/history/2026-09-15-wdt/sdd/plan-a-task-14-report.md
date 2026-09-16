# Task-14 report: Death-B permanent fix (drop the outer flash_safe_execute) + verify UF2s

Status: IMPLEMENTED, UNVERIFIED ON HW (owner run pending). No commits/pushes/PRs/branch ops
(commit hold until verification per owner). `pico-wakecon` read-only (comparison only),
never modified. No secret key bytes. No SDK/BTstack source edits.

## 1. Condition (1) proof — the mechanism is confirmed, not inferred

`C:\Users\moilo\.pico-sdk\sdk\2.3.0\src\rp2_common\pico_flash\flash.c`:

- `:106` `static uint32_t irq_state[NUM_CORES];` — file-scope STATIC, shared (not local).
- `:203` (enter) `irq_state[get_core_num()] = save_and_disable_interrupts();`
- `:210` (exit) `restore_interrupts_from_disabled(irq_state[get_core_num()]);`

A nested `flash_safe_execute` (outer in `store.c` + inner HAL ones in
`btstack_flash_bank.c`) therefore clobbers the shared slot: the inner enter overwrites the
outer's saved 0 with 1, and the outer exit restores PRIMASK=1. Every nested store leaks a
global IRQ disable on core0. W10c measured the consequence (`pm=1` persisting, 3/3 boots).
Because the slot is shared (not per-call local), removing the nesting removes the leak by
construction — no re-occurrence monitoring beyond the shipped guard is required for THIS
mechanism. (A future, different leaker would still be caught by the guard, §3.)

## 2. The fix (stays in tree, uncommitted)

`src/bt/store.c` (`t14-store-vs-backup.diff`):

- `tag_store_safe`: outer `flash_safe_execute(tag_store_fn, …)` → direct `tag_store_fn(&op)`
  call. Rationale comment cites the SDK lines + the 9b safety case (HAL wraps every mutation:
  erase :82, per-page program :170, delete-via-zero through write, reads are XIP memcpy).
  Single-level (non-nested) operation is now the stated invariant.
- `store_host_forget` / `store_cap_forget`: outer wrapper → direct `tag_delete_fn(&op)`.
- `store_irq_guard(pm_entry)` (permanent insurance, NOT a diagnostic): reads PRIMASK via
  `mrs`; if entry was 0 and exit is non-zero, prints `irq leak healed (n=…)` and executes
  `cpsie i`. Entry!=0 calls are left alone. Cost: one `mrs` per store; the log fires only
  on a leak. Called from `tag_store_safe` and both forget paths (i.e. after every flash
  mutation path in this file).
- Known granularity change (same as 9b's disclosed concern): atomicity shrinks from
  whole-op to per-page (HAL lockout released between pages). Acceptable for the small
  BCHO/BCCx/BCW1/BCWR records; the alternative (nesting) is provably broken.

`src/main.c` (`t14-main-vs-backup.diff`): permanent observability — `pm_rd()` helper and a
`pm=%lu` field appended to the 1 s BCON line (buffer 160→192). If `pm` ever reads non-zero
in the field, something other than the fixed nesting leaks (the guard would also have
logged). `store.h` unchanged.

## 3. Verification UF2 (fix + TEMP D-gate) and production UF2 (fix, no D-gate)

- `log/pico-bcon-fix-verify.uf2` 817664 B
  SHA256 `1B179E497A34F285AC72981892768C4CEF3DBC46C4397824D06C484BEF2B57CE`
  = fix + a TEMP D-gate (forced second BCHO write per HID-open, the W10a recipe that
  deterministically triggered Death-B). The D-gate was reverted after the build
  (`FIXVERIFY` grep zero in tree). Recipe: wireless, 115200 baud, `WIRED_DEFAULT=0`.
- `log/pico-bcon-prod-fix1.uf2` 817152 B
  SHA256 `2DCD81B633C7BC534BF211EDCF63004514573ECBA62FA7C5F6BE2148655DF791`
  = fix only (what is in the tree now).

Owner verify run (fix-verify UF2): flash → connect from the Switch (any reconnect; the
D-gate fires per HID-open) → expect NO Death-B: session survives minutes, SUBs flow,
`forced store done` prints, BCON shows `pm=0` every second, `host=1` persists across
reboots, and `irq leak healed` NEVER appears. If it still dies, paste the log (the BCON
`pm` field + the last lines before the reboot decide the next step). After a pass, flash
the prod-fix1 UF2 for normal use.

## 4. Ledger notes (mine + open items + hold)

- The OLD production build (`pico-bcon-prod-wireless.uf2`, pre-fix) still carries the mine:
  every nested store (BCHO for a changed peer, BCCL color save, BCW1 capture save, BCWR
  wired save, both forget paths) leaks PRIMASK=1. `pm` in BCON (and the guard's log) is
  the detector. Do NOT ship or trust the old UF2 for longevity runs.
- Whether the op-in death (young-bank S3 lockout race) also disappears after outer removal
  is a SEPARATE re-verification (open): it needs the young-bank prelude + a crumbs build.
  The fix removes the Death-B leak; the op-in race (lockout handshake level) is untouched
  by this change and stays open.
- W10b (100 ms cap) is definitively unnecessary: the bug is on the success path (a leak
  left by completed calls), not on a timeout. The cap would never fire here.
- Commit hold: the fix stays uncommitted until the owner HW run passes.

(End of report)
