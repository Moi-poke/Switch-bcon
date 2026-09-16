# W9 Phase-2 report: wrapper-removal A/B (9b) — task-11b

Status: DONE_WITH_CONCERNS (concerns are documented limitations, none blocking).
Scope: Phase 2 ONLY (9b W9-A control + W9-B trial, each with outer-return-to-2s
breadcrumbs). Phase 0 / Phase 1 (9a+9c observe) / Death-B track / W10 / W11
explicitly untouched — nothing implemented for them. No commits, no pushes, no
PRs, no branch ops. `C:\Users\moilo\pico-wakecon` never touched. No secret key
bytes anywhere (no tag payloads / keys / BD_ADDR in new code; tag IDs and
generation counters only). No SDK/BTstack source edits. No subagents used. Work
from `C:\pico-bcon`, HEAD/BASE `c366bf4` confirmed via `git rev-parse HEAD` at
task start; pre-existing Phase-3 + Plan-A tree preserved (status-after identical
to status-before).

## 1. Safety-case re-verification: GO (inner safe-execute present)

Read directly in the build SDK 2.3.0
(`C:\Users\moilo\.pico-sdk\sdk\2.3.0\src\rp2_common\pico_btstack\btstack_flash_bank.c`):

- Line 82: `flash_safe_execute(pico_flash_bank_perform_flash_mutation_operation, &mop, UINT32_MAX);`
  inside `pico_flash_bank_erase` (whole-bank erase path).
- Line 170: identical `flash_safe_execute(...)` call inside `pico_flash_bank_write`'s
  per-page program loop (every page programs under its own inner lockout).
- Read path (`pico_flash_bank_read`) is XIP `memcpy`, no protection needed.
- TLV delete path programs zeros through `pico_flash_bank_write`, i.e. equally
  covered by the line-170 inner wrap.

NO-GO condition NOT triggered: the inner safe-execute exists at both cited
lines, so removing our outer wrapper leaves every HAL mutation protected. The
trial is safe to flash.

## 2. Per-variant implementation

Shared 9b core (byte-identical in both UF2s — `w9ab-a-diag_rec.c.txt` and
`w9ab-b-diag_rec.c.txt` SHA256-identical, `A6E54BD4…9EFE5F3EE`):

- 9a S-stages, our layers only (`src/bt/store.c` + NOLOAD recorder, W9 versions
  of the W7/W8 `diag_rec`/scratch/hci_dump patterns): S0 `tag_store_safe` enter,
  S1 pre-outer (A) / pre-direct-call (B), S2 first line of `tag_store_fn`, S3
  pre-`tlv->store_tag`, S10 post-return, S11 callback exit, S12 post-outer (A) /
  post-direct-call (B). Each stage → dedicated S-only NOLOAD trail + `scratch[2]`
  mirror as `100+s` (`[3]` epoch stays, `[4..7]` SDK-reserved untouched) + ring
  entry. S4–S9 excluded per the no-SDK rule. Recorder magic `0xD1A6000B`/ver 11
  (deliberately incompatible with the observe UF2's `0009`, so cross-flashing
  resets the recorder instead of misparsing it).
- D-gate forced BCHO write (brief fallback path — documented here as permitted:
  no phase-D helper exists in-tree, so a minimal W9DIAG one-shot in the hid-open
  handler): inside `HID_SUBEVENT_CONNECTION_OPENED` success, after the untouched
  normal `store_host(a)`, `{ force_fire() → store_host_force(a) → "w9fire var=D
  gen=<g>" }`. `store_host_force` = W1/W5-precedent `store_host_inner(addr,true)`
  dedupe bypass; payload stays the TRUE 6-byte peer addr (generation lives in
  the NOLOAD counter + log line, never XORed into the payload — a corrupted
  payload would diverge TLV from `probe_host_addr` and break reconnect logic).
  TLV `store_tag` always programs flash physically, so bypass alone guarantees
  the write (same basis as W5). One-shot per open by construction (handler runs
  once per open); repeat opens mint new generations. Flagged side effect: on a
  fresh pairing the session performs 2 BCHO programs (normal + forced);
  diagnostic-only wear, disclosed.
- hci_dump ON (W6-proven inert, session comparability), link-key-db 9-method
  counting shim (W8-verbatim, signatures match `btstack_link_key_db.h` 9/9),
  poll stages, timer FIRE/ADD accounting, EV record. Core1 heartbeat proxy
  declined (a "may", keeps the diff minimal). 9c bank CRC deliberately NOT
  carried (Phase-1 scope; "EXACTLY Phase 2").

W9-A (control, `log/pico-bcon-w9a-control.uf2`): outer `flash_safe_execute`
kept in `tag_store_safe` + both `*_forget` paths; dump banner
`w9var A-control double-wrap`.

W9-B (trial, `log/pico-bcon-w9b-nowrap.uf2`): `tag_store_safe` calls
`tag_store_fn(&op)` DIRECTLY; `store_host_forget`/`store_cap_forget` call
`tag_delete_fn(&op)` DIRECTLY; dump banner `w9var B-nowrap outer-removed`
(mis-flash protection). A-vs-B delta = wrapper-removal hunk + disclosure
comment + 1 label line (see `diffs/w9ab-a-vs-b-store.diff` + the `w9var` lines);
wrapper presence is the ONLY functional variable.
KNOWN granularity change (disclosed in code + here): atomicity unit shrinks
from whole-op to per-page (HAL lockout released between pages); any
partial-write signature must be reported separately by the owner.

## 3. Outer-return-to-2s breadcrumb list (all five present, none omitted)

- Last-BTstack-timer-service time: `svc_us[usb|empty|stats]` = `time_us_32()` at
  each owned-timer FIRE (`w9tmrsvc` line). Freeze pattern = that timer stopped
  being dispatched.
- Last async-context service time: `loop_us` = post-`poll_tick` sample in
  `usb_handler` every 1 ms (`w9loop` line; our-layer proxy — the 1 ms pump
  completing end-to-end proves the run loop is serviced).
- PRIMASK/BASEPRI snapshot: read-only `mrs` (exact `__get_PRIMASK()` /
  `__get_BASEPRI()` semantics, inline-asm so no header risk), sampled at every
  loop service (`pm/bp`) AND at S12 (`w9s12 us/pm/bp` = IRQ state at 2s-window
  entry). `0/0` = IRQs open.
- hci TX/RX liveness: RX = existing `ev_n/ev_last`; TX = `tx_req`
  (`hid_device_request_can_send_now_event` calls) + `tx_grant` (CAN_SEND_NOW
  receipts) on the `w9hci` line.
- Timer list head/next-timeout: PUBLIC accessors USED (not omitted):
  `extern btstack_linked_list_t btstack_run_loop_base_timers` +
  `btstack_run_loop_base_get_time_until_timeout()` (`btstack_run_loop.h:134,171`).
  Validity for our port verified: BOTH linked run-loop implementations
  (`platform/embedded/btstack_run_loop_embedded.c:126,212` and pico
  `btstack_run_loop_async_context.c:58,155,157` — the latter is even compiled
  into our link, build log line 92/184) funnel all timers through
  `btstack_run_loop_base_{add_timer,process_timers,get_time_until_timeout}` on
  that single list. Caveat recorded: the header marks the extern "private data
  (access only by run loop implementations)" — our use is strictly read-only
  (never written), capped at 16 nodes (`trunc=1` = walk hit the cap, itself a
  list-corruption signal). Sampled as `tmr_n/next_to/first_to/trunc` on `w9loop`
  (note: own `usb_timer` is already dequeued before its callback, so `tmr_n`
  counts the OTHER pending timers, BTstack-internal ones included).

## 4. Owner runbook — boot-dump line formats (all decimal unless noted)

`w9var …` / `w9epoch post= current=` / `w8flash-total safe=/ tag=/ del=/
lkput=/ lkdel=/` (begins/ends, literal W8) / `w9flash-boot …` (this-boot delta) /
`w9last store tag=%lx epoch= rc= dur=` / `w9last delete …` /
`w8tmr-total usb a= f= to= empty … stats …` / `w9tmr-boot …` /
`w9stage last= n= trail=8` (poll 1–10) / `w9opstage last=S n= trail=8` (raw
0,1,2,3,10,11,12) / `w9force gen= n=` / `w9tmrsvc usb= empty= stats=` (us) /
`w9loop us= tmr_n= next_to= first_to= trunc= pm= bp=` / `w9s12 us= pm= bp=` /
`w9hci rx_n= rx_last=0x tx_req= tx_grant=`. Fresh boot:
`w9rec invalid (fresh boot; ring reset)`.

## 5. Owner decision table (≥3 forced-write sessions per variant)

| Observation | Proves |
|---|---|
| W9-B kills ≈ W9-A, identical signatures (same last-S, same begins>ends) | Double-wrap is NOT the mechanism; redirect to bank-state (9c) + Core1-victim audit |
| W9-B survives where W9-A dies | Outer wrapper (or its interaction) is load-bearing; permanent worker drops the outer wrap (HAL coverage suffices) — worker-design amendment |
| Death with last=S1 (either variant) | Outer lockout-acquire wait (A) / hung before direct call (B) |
| Death with last=S2/S3 | Inside callback at/before `store_tag` entry (H1-inner or TLV-entry) |
| Death with last=S11 w/o S12 | Outer unlock/release wait (A only; in B this pattern implicates post-call return path — new signature, report it) |
| Death with last=S12, no migrate, committed (`w9last` rc=0) = Death-B | Read `w9loop`/`w9s12`/`w9tmrsvc`/`w9hci`: `loop_us`≈`s12_us` + `tmr_n`→0/`next_to`=-1 = timer-vanished; `pm`=1 = IRQ-killed (masked at/after outer return); `trunc`=1 or absurd `first_to` = list-corrupted; TX/RX counters advancing with frozen `w9stage` = pump wedged but HCI alive |
| `w9force gen` increments but no `w9fire` line | Hang INSIDE the forced write (pre-completion); S-trail pinpoints the stage |

## 6. Build evidence

Fresh temp dirs (never `build/`), wireless recipe, full-path tools:

- A: `cmake -S . -B $env:TEMP\wab-w9a -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0`
  → config EXIT=0; `cmake --build $env:TEMP\wab-w9a --target pico-bcon` → first
  build FAILED (missing `diag_rec_ev` header decl — self-caught, see §8 F1), no
  UF2; after fix → **BUILD EXIT=0**.
- B: same recipe in `$env:TEMP\wab-w9b` → config EXIT=0, **BUILD EXIT=0** (clean,
  first attempt).

## 7. UF2 + artifacts

- `log/pico-bcon-w9a-control.uf2`, **828416 bytes**, SHA256
  `715DFB7D00F5E059EE50B379D7B574C5B0379811A3291E3A1BE6812CE0443F55`.
- `log/pico-bcon-w9b-nowrap.uf2`, **828416 bytes**, SHA256
  `D7560750A0DCF2B2527F68C322298942D1144FCAD070EC04D6D2A4E80A589D71`.
  (Equal sizes = UF2 padding granularity; SHAs differ as expected.)
- `diffs/w9ab-a-{main,store,store-h,cmake}-vs-backup.diff`,
  `w9ab-a-diag_rec.{c,h}.txt`, `w9ab-a-tracked.diff` (63074 B, includes
  pre-existing Phase-3/Plan-A noise vs HEAD — same caveat as W1/W5/W9o);
  `w9ab-b-*` mirror set; `w9ab-a-vs-b-store.diff` (3275 B, the falsification
  hunk). All non-empty. Marker coverage mechanically verified: zero unmarked
  `+` lines in all eight vs-backup diffs; zero unmarked non-blank lines in both
  new files.

## 8. Restore evidence

Restored 4 files from `backups/w9ab/*` via `Copy-Item` (never
checkout/restore/clean); deleted `src/diag_rec.c/.h` (`Test-Path` False/False).
`status-after-w9ab.txt` identical to `status-before-w9ab.txt` (`Compare-Object`
empty). Hashes restored==baseline, all match Step-1 values: main.c
`F40AFE1A…04918`, store.c `40456D82…C87726`, store.h `FF4B5284…FE340`,
CMakeLists `6E3628C3…B42D254`. Marker grep `W9DIAG|diag_rec|scratch` over
`src/` → zero hits. Tree holds only the original Phase-3/Plan-A entries; no
SDK, BTstack, or wakecon paths touched. UF2s + diffs + backups retained;
`$env:TEMP\wab-w9a|w9b` build dirs retained outside the repo (`build/`
untouched).

## 9. Self-review findings

- F1 (caught by compiler, fixed, disclosed): `diag_rec_ev` defined in
  `diag_rec.c` but missing from `diag_rec.h`; first A build failed with
  Wimplicit-function-declaration. Fixed by adding the 2-line decl; rebuilt A
  (EXIT=0) before saving the UF2, so the shipped A UF2 contains the fix.
  Vs-backup diffs + marker coverage re-verified after the fix.
- F2: B's `tag_store_safe` drops the `PICO_OK !=` refused branch — correct: a
  direct call cannot refuse; `op.rc` carries the result into the unchanged END
  record. No unused-variable warning (`w9_t0` still feeds the duration).
- F3: shim signatures are W8/W9o-verbatim (9/9 pattern); `time_us_32` in
  `main.c` shim resolves via existing `pico/stdlib.h`; `btstack_run_loop.h` in
  a non-BTstack TU compiles (no `tusb.h` co-inclusion — TU-separation rule
  holds).
- F4: longest dump line (`w9loop` ≈ 85 chars worst-case) fits `w9m[160]`.
- F5: `git status` shows no modified/new paths beyond the task's files plus the
  pre-existing tree entries.

## 10. Concerns

- C1: per-1 ms `loop_sample` does an FNV pass over ~2 KB NOLOAD state + a capped
  list walk — same cost class as W8's per-stage pushes (diagnostic only, fully
  reverted).
- C2: `opstage_trail` stores raw S-numbers, so S0=0 is indistinguishable from an
  empty slot except via `opstage_n` (same caveat as W9o).
- C3: in W9-B, S1/S12 mean pre/post-direct-call (not outer acquire/release) —
  the decision table must be read per-variant (§5).
- C4: on first flash of either UF2 the recorder magic mismatches observe (`0009`
  vs `000B`) → `w9rec invalid` on the virgin boot; compare consecutive
  WDT-reboot boots for verdicts, not the virgin boot.
- C5: D-gate doubles BCHO wear on fresh pairings (normal + forced write per
  open); diagnostic-only.
- C6: `loop_tmr_n` excludes our own `usb_timer` (dequeued pre-callback) — by
  design, documented in code (§2 shim comment block) and §3.
- C7: S-stages and poll stages share `scratch[2]` (latest wins); NOLOAD trails
  are authoritative, scratch is a debugger hint only.

(End of file)
