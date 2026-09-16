# W9 brief: safe-execute境界の分離＋wrapper有無A/B＋bank CRC（U・op内死の直接計測）

Diagnostic UF2s only. Resolves external-review #3 §9 (最優先：safe execute境界の分離).
Background: W8 proved deaths both INSIDE the op (epochs 1–4: begins>ends, no
BTstack write-log, host stays 0) and AFTER completion (epoch 5), plus 26-min
survival with a key-save and no BCHO write (epoch 6). Our `tag_store_safe`
wraps `tlv->store_tag` in outer `flash_safe_execute`, while SDK
`pico_btstack/btstack_flash_bank.c:82,170` wraps every HAL erase/program page
in an INNER `flash_safe_execute` (verified in SDK 2.3.0 sources) — i.e. every
store is a nested double-wrap. Nested lockout is protocol-transparent by code
reading (victim re-locks on new request id), so nesting per se does not explain
the hang; the hanging STAGE is unknown. This task definitionally separates the
stages without touching SDK/BTstack sources.

## Execution order (review #4 + #5 — binding)

Phase 0 (FIRST, zero code change, no flash of new binaries): reproduce
op-in-death with the CURRENTLY FLASHED observe UF2 still in place.
Rationale: op-in did not reproduce on the mature bank (wroff~2255);
flashing 9b (or anything) mutates flash further and may close the
reproduction window for good. 9b can be burned any time; the op-in window
may be closable only now.
Protocol (single variable = flash/bank state; operator-executed):
CONFIRMED W8 prelude (owner testimony 2026-09-15/16 night): T_KEY_DELETE
frame sent over data UART
(`body=(0x33,0x00,0x00)`, i.e. TYPE=0x33/LEN=0/SEQ=0 + CRC8/SMBUS, 115200bps
matching the diagnostic build) → `keys deleted` path
(`gap_delete_all_link_keys` + `store_host_forget`: tombstones programmed,
write_offset UNCHANGED, no erase) → Pico USB power unplug/replug (power
cycle clears NOLOAD; warm reset does NOT suffice for a clean epoch) →
Switch side untouched (no reboot, no deregister). Net effect: tombstone-
heavy young bank (host=0, link keys=0, offsets continuing prior lineage
86b→89d). Virgin-bank theory is DEAD (no erase ever happened); the
tombstone-scan side is the live candidate — a pre-write iterator walk over
tombstoned entries is now the top desk suspect for the S2/S3 hang.
1. Keep the observe UF2 flashed (do NOT rebuild/reflash anything).
2. Recreate EXACTLY: send the same T_KEY_DELETE one-liner on the data-UART
   port at the flashed build's baud → confirm `keys deleted (classic +
   host tag)` in the log (if absent, the frame was not accepted: fix
   port/baud, do not proceed) → USB power unplug/replug → Change-Grip
   first connection (Switch side as-is), capture from boot banner.
3. Verdict: op-in reproduces (begins>ends + no write-log + host stays 0)
   → trigger is post-delete tombstone-bank first store. Read chg0/chg1
   across THIS in-op death immediately:
   chg*=1 → partial mutation inside op (H2昇格 + W11追加);
   chg0=chg1=0 → zero mutation → pre-write scan loop prime suspect
   (iterator over tombstones; worker gains bank-integrity check +
   iteration cap + reformat path).
Phase 1 (ONE observe UF2 — already built and flashed): 9a + 9c as specified.
9c is the branch parent — its result redefines everything downstream.
Pre-read first: `w8last`/`w8tmr` rules below, then branch:
(a) CRC changes across an in-op death → stop was inside erase/migrate
(H2昇格); additionally pull W11 boundary separation.
(b) CRC unchanged → zero physical mutation before hang; prime suspect
becomes pre-write-scan CPU-side infinite loop over a corrupt bank
(iterator with bad length never terminates). Consequence: permanent fix
must additionally gain boot-time bank-integrity check + scan iteration
cap (+ safe reformat path) — record as worker-design amendments, do NOT
finalize worker scope until this lands.
Phase 2: 9b (wrapper-removal A/B) ONLY after Phase-1 localization — it is
the falsification test of the transparency conclusion, not a first step.
NO-GO as specified is retained. 9b UF2 additionally carries
outer-return→2s breadcrumbs (review #5): last-BTstack-timer-service time,
last async-context service time, PRIMASK/BASEPRI snapshot (our-layer
`__get_PRIMASK()` etc., read-only), hci TX/RX liveness counters, and timer
list head/next-timeout ONLY if a public accessor exists (else record the
absence and use BCON + hci-dump traffic as proxies). Rationale: the clean
Death-B case (S12, no migrate, committed) localizes damage to post-outer-
return, so the 2s window is where timer-vanished vs IRQ-killed vs
list-corrupted gets decided.
Death-B track (separate, do not mix): W10 period A/B and W11 boundary work
belong to the epoch-5 signature (post-complete scheduler loss), never to
op-in-death. Run on their own track only if Death-B reproduction remains.

## Dispatch scope for THIS run: Phase 1 ONLY (9a + 9c combined observe UF2)

Phase 2 (9b) is explicitly deferred until Phase-1 localization lands.
Build exactly one UF2: `log/pico-bcon-w9-observe.uf2`.

## Pre-burn checks (binding — verify each in the report or STOP)

1. **Snapshot timing (highest priority).** The CRC snapshot must run BEFORE
   any current-boot TLV read OR any TLV-init-driven mutation: order in
   `main()` is strictly `diag_rec_init()` (RAM only, NOLOAD continue) →
   CRC snapshot of both banks into the recorder → everything else
   (TLV init, `store_*_load`, `link_key_count`, hci_dump/log init, Core1
   boot, timer arms). Rationale (proven): boot-time TLV reads predate log
   init, so a late snapshot would capture post-TLV state, not the raw
   trace the previous boot's in-op death left behind. If TLV init itself
   can mutate (virgin-bank format path), the snapshot-before-init order
   already covers it — state this coverage explicitly in the report.
2. **Four-point set, banks kept separate.** Dump `bank0_crc / bank1_crc /
   active_index / write_offset` — never a merged CRC (merged hides whether
   the active bank switched or one bank progressed). `active_index` and
   `write_offset` are readable only after TLV init via the PUBLIC
   `btstack_tlv_flash_bank_t` struct layout (read-only field access from
   our layer; no SDK edits); raw CRCs are snapshotted pre-init per check 1.
   Report both snapshot-time and dump-time values if they can differ, and
   name the exact struct fields used. Persist the previous boot's two CRCs
   in NOLOAD so the dump prints per-bank changed-vs-last-boot flags.
3. **One dump block, epoch-linked.** Stage trail (9a), the four-point CRC
   set (9c), flash op counters and last-op records print in a SINGLE
   boot-dump block keyed by `post_epoch / current_epoch`, so one line reads
   as "ep_n stopped at S2, CRC unchanged since last boot". Keep the W8
   `w8flash-total / w8tmr-total` cumulative lines (they closed U3/U5) and
   the per-op `last_tag / last_epoch`.

## Constraints

- No commits, pushes, PRs, branch ops. `C:\Users\moilo\pico-wakecon` never touched. No secret key bytes (bank CRCs and offsets only, never tag payloads / keys).
- No SDK/BTstack source edits — hard boundary (see below for what this excludes and how each exclusion is worked around).
- Isolated temp build dirs (never reuse `build/`; one dir per variant). Wireless recipe `-DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0`, no BUMP flag.
- Every added line carries a `W9DIAG` marker. Work from `C:\pico-bcon`. BASE `c366bf4`; tree holds uncommitted Phase-3 + Plan-A work — preserve it.
- Restore proof required per variant (hashes equal, marker grep zero). NEVER use git checkout/restore/clean.
- Report path: `C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\task-11-report.md` (numbered 11 to avoid colliding with the existing task-9/task-10 A/B briefs).

## Variant 9a — our-layer stage breadcrumbs (answers: which safe-execute stage hangs)

Instrument ONLY our layers (`src/bt/store.c`, W8 `diag_rec` recreated or extended — W8 files were deleted at restore; recreate as W9 versions):

- S0 `tag_store_safe` enter, S1 immediately before outer `flash_safe_execute`,
  S2 first line of `tag_store_fn` (callback entered = outer lockout acquired),
  S3 immediately before `tlv->store_tag`, S10 immediately after it returns,
  S11 last line of callback (returning), S12 immediately after outer
  `flash_safe_execute` returns (with its rc).
- Record each stage into the NOLOAD ring + mirror latest to a scratch reg
  (reuse W7/W8 scratch map; `[3]` epoch stays). Boot dump prints the last
  stage + trail, as W7/W8 did.
- Decision table: dies with last=S1 → outer lockout-acquire wait (H1-outer);
  last=S2/S3 → entered callback, hung before/inside `store_tag` entry
  (H1-inner or TLV-entry); last=S10 without S11 → hung between return and
  callback exit (never observed yet — would be new); last=S11 without S12 →
  outer unlock/release wait. Any result past S3 (durations, write-log
  presence) reuses W5/W7/W8 interpretation.
- EXCLUDED by the no-SDK rule (document in report, do not attempt): S4–S9
  (TLV-internal, migrate decision, HAL erase/program boundaries) and
  lockout-layer acks. Rationale: all live in `lib/btstack` or
  `src/rp2_common/pico_*` outside this repo. Migration is inferred
  indirectly via Variant 9c.

## Variant 9b — outer-wrapper removal A/B (answers: is the double-wrap the killer)

- W9-A (control): current double-wrap unchanged + 9a stages (single UF2 may combine 9a+9b-control).
- W9-B (trial): `tag_store_safe`/`tag_delete_fn` call `tag_store_fn` /
  `tlv->delete_tag` DIRECTLY (no outer `flash_safe_execute`), stages kept.
  Safety case (verified, cite in report): every HAL mutation path already
  runs inside the HAL's own inner `flash_safe_execute`
  (`btstack_flash_bank.c:82` erase, `:170` page-program loop); reads are XIP
  `memcpy` (same file, read path); TLV delete path programs zeros through
  `pico_flash_bank_write`, i.e. equally covered. KNOWN granularity change
  to disclose: atomicity unit shrinks from whole-op to per-page (lockout
  released between pages); reporters must note any partial-write
  signatures separately.
- Both variants perform the SAME forced BCHO write gate (reuse the W9-phase
  D gate: just-after-open one-shot, generation-numbered to beat dedupe;
  if the phase brief's helper is unavailable, a minimal W9DIAG one-shot in
  the hid-open handler is acceptable and must be documented as such).
- Decision rule (≥3 forced-write sessions each): kill-rate + signature
  (in-op vs post-complete) per variant. If W9-B kills ≈ W9-A with identical
  signatures → double-wrap is NOT the mechanism; redirect to bank-state
  (9c) and Core1-victim audit. If W9-B survives where W9-A dies → outer
  wrapper (or its interaction) is load-bearing; permanent worker design
  drops the outer wrap (HAL coverage suffices) — record as a worker-design
  amendment.
- NO-GO gate: if reading `btstack_flash_bank.c` in the build SDK shows the
  inner safe-execute missing (different SDK revision), STOP and report
  BLOCKED (the safety case collapses; do not run the trial).

## Variant 9c — bank CRC per boot (answers: does the bank mutate across in-op deaths)

- Pure our-layer code, no SDK edits: at boot (after the w8-style dump, before
  radio up), CRC32 both 4KB banks by direct XIP reads at
  `PICO_FLASH_BANK_STORAGE_OFFSET` (`pico/btstack_flash_bank.h:24-28`,
  total = 2 sectors, bank = 4KB) and print `bank0 CRC / bank1 CRC /
  changed-pages-vs-last-boot bitmap`. Payloads/keys never printed (offsets
  and CRCs only). RAM cost trivial; XIP reads need no flash protection.
- Interpretation: any CRC change across an in-op death boot (host stays 0)
  proves partial flash mutation happened inside the op → bank-state
  progression (migrate/partial-program) is real and the monotonic
  die×4→complete→survive order has a physical substrate. No change →
  the op truly performed zero mutation before hanging (lockout-wait side
  gains).
- May ride on the 9a UF2 (same build) to save a flash cycle; report must
  state which UF2 carried it.

## Steps (per variant; shared core identical)

- [ ] **Step 1: baseline + backup** (`status-before-w9x.txt`;
  `backups/w9x/{src_main.c.bak,store.c.bak,store.h.bak,CMakeLists.txt.bak}`;
  marker grep zero pre-check incl. `W9DIAG`).
- [ ] **Steps 2-6: implement the variant** (stages per 9a; wrapper removal per
  9b with the granularity disclosure; CRC reader per 9c; hci_dump ON as
  W6/W7/W8 for session comparability).
- [ ] **Step 7: compile** (isolated dirs `$env:TEMP/wab-w9a` etc., exit 0).
- [ ] **Step 8: artifacts** (`log/pico-bcon-w9a-stage.uf2`,
  `log/pico-bcon-w9b-nowrap.uf2`, CRC linesdocumented; diffs to
  `diffs/w9x-*` incl. vs-backup; size+SHA256 recorded).
- [ ] **Step 9: restore + prove** (Copy-Item restore, new-file deletion,
  status identical, hashes True, marker grep zero).
- [ ] **Step 10: report** (`task-11-report.md`): stage decision table outcome
  per death, A/B kill-rates + signatures, CRC deltas per boot, safety-case
  re-verification (SDK file:line), restore evidence, self-review. DO NOT commit.

## Explicitly out of scope (needs SDK edits — do not attempt here)

- S4–S9 TLV-internal/migrate/HAL-erase/program boundary crumbs; lockout
  victim-handler crumbs; Core1-victim lockout ack proof (heartbeat proxy only:
  may record `core1_iters` + `time_us_32` immediately before S1 as our-layer
  context, nothing more).
- Permanent worker code changes (separate approval track; this task only
  records worker-design amendments if 9b implicates the wrapper).
- Plan B (BAUD_SET) / Plan C (personality) work.
