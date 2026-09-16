# W9 Phase-1 report: combined observe UF2 (9a stages + 9c bank CRC) — task-11

Status: DONE_WITH_CONCERNS (concerns are documented limitations, none blocking).
Scope: Phase 1 ONLY (9a + 9c, one UF2). Phase 2 (9b), Death-B track, W10/W11
explicitly deferred — nothing implemented for them. No commits, no pushes, no
PRs, no branch ops. `C:\Users\moilo\pico-wakecon` never touched. No secret key
bytes anywhere (bank CRCs/offsets only). No SDK/BTstack source edits. No
subagents used. Work from `C:\pico-bcon`, HEAD/BASE `c366bf4` confirmed via
`git rev-parse HEAD` at task start; pre-existing Phase-3 + Plan-A tree preserved
(status-after identical to status-before).

## 1. Pre-burn check 1 — snapshot timing: PASS

Order in `main()` (post-edit `src/main.c`, direct-read verified):
`L1001 s_wdt_recovered = watchdog_caused_reboot()` (original line, untouched) →
`L1002 diag_rec_init()` (RAM only, NOLOAD continue) →
`L1003 diag_rec_crc_snapshot()` (both banks, XIP reads only) →
`L1006 link_init()` (RAM-only: unique-ID MAC, no TLV/flash — verified in
`src/bt/link_conn.c`) → `L1019–L1029` TLV-init block →
`L1032–L1035 store_*_load` → `L1036–L1038 wired=` banner → `L1039 w9_boot_dump()`
→ `L1047` Core1 boot → hci_dump/log init → timer arms → run loop.
Nothing TLV- or flash-touching precedes L1003. Virgin-bank format-path coverage:
`btstack_tlv_flash_bank_init_instance` runs at L1022, ~20 lines AFTER the
snapshot, so any TLV-init-driven mutation (including virgin-bank format) cannot
contaminate the snapshot. Additionally the dump re-reads live CRCs (`dump0/1`,
see check 2); a snap-vs-dump drift on a virgin board would positively reveal the
format path.

## 2. Pre-burn check 2 — four-point set, banks separate: PASS

Dumped four-point set (never merged): `bank0_crc / bank1_crc / active_index /
write_offset`. Exact struct fields used (PUBLIC header
`lib/btstack/platform/embedded/btstack_tlv_flash_bank.h`, read-only field
access from our layer, zero SDK edits):
`btstack_tlv_flash_bank_t.current_bank` (int8_t → `active`, `%ld`, -1 = invalid)
and `btstack_tlv_flash_bank_t.write_offset` (uint32_t → `wroff`).
Stash call `L1027 diag_rec_tlv_geo(current_bank, write_offset)` sits inside the
TLV block, after init, before any load. Raw CRCs are CRC32-IEEE (our-layer
bitwise, no tables) over XIP reads at `XIP_BASE + PICO_FLASH_BANK_STORAGE_OFFSET`
(bank = `PICO_FLASH_BANK_TOTAL_SIZE/2` = 4 KB at default 2-sector geometry;
`pico/btstack_flash_bank.h:24-28`). Both snapshot-time (`snap0/1`, L1003) and
dump-time (`dump0/1`, live re-read inside `w9_boot_dump`) values print on the
same `w9bank` line. Previous boot's two CRCs persist in NOLOAD
(`bank_crc_prev[2]`, shifted before overwrite in `diag_rec_crc_snapshot`), and
the dump prints per-bank changed-vs-last-boot flags (`chg0/1 = snap != prev`).
Known reduction (concern C3): per-256B-page bitmap is NOT provided — only
per-bank changed flags. Storing a full 8 KB prev image in NOLOAD was out of
scope; the Phase-1 branch decision needs only bank-level change detection.

## 3. Pre-burn check 3 — one dump block, epoch-linked: PASS

`w9_boot_dump()` (`src/main.c`, boot context only, `probe_line` + 160 B buffer)
prints the whole block consecutively, keyed first by `w9epoch
post=<prev> current=<this>`. Block order: `w9epoch`, `w8flash-total`
(cumulative, literal W8 format — U3/U5 continuity), `w9flash-boot` (this-boot
delta), `w9last store` (tag/epoch/rc/dur), `w9last delete`, `w8tmr-total`
(cumulative, literal W8 format), `w9tmr-boot`, `w9stage` (shared poll trail),
`w9opstage` (9a S-only trail), `w9bank` (9c four-point set + snap/dump + chg),
`w9ev`. One boot reads as "ep_n stopped at S2, CRC unchanged since last boot".

## 4. What was built (9a + 9c)

- 9a S-stages, our layers only (`src/bt/store.c` + NOLOAD recorder, W9 versions
  of the W7/W8 `diag_rec`/`scratch`/hci_dump patterns, extended with epoch
  linkage + cumulative totals + last_tag/last_epoch): S0 `tag_store_safe` enter,
  S1 immediately before outer `flash_safe_execute`, S2 first line of
  `tag_store_fn`, S3 immediately before `tlv->store_tag`, S10 immediately after
  it returns, S11 last line of callback, S12 immediately after outer returns.
  Each stage → dedicated S-only NOLOAD trail (`opstage_last/trail/n`, immune to
  1 ms poll traffic) + `scratch[2]` mirror as `100+s` (disjoint from poll ids
  1–10; `[3]` epoch stays, `[4..7]` SDK-reserved untouched) + ring entry
  (`a1 = 100+s`). S4–S9 excluded per the no-SDK rule (documented, not attempted).
- 9c bank CRC per §4/checks 1–2. Poll stages, timer FIRE/ADD accounting, EV,
  link-key shim, hci_dump ON: reused from W8 verbatim (renamed W8→W9) for
  session comparability. Core1-victim heartbeat proxy declined (a "may", not a
  "must"; keeps the diff minimal).
- Diffs: `src/main.c` +138/−3 (the −3 = 2 TLV call-site lines replaced by shim
  + wired_loop `}` re-emitted marked, see self-review F1), `src/bt/store.c`
  +19/−0, `CMakeLists.txt` +1/−0 (`src/diag_rec.c` source line), new
  `src/diag_rec.h/.c` 100 % W9DIAG-marked non-blank lines. Every added line
  carries `W9DIAG` (mechanically verified: zero unmarked `+` lines in all three
  vs-backup diffs). Error/return semantics unchanged.

## 5. Owner runbook — exact boot-dump line formats

Flash `log/pico-bcon-w9-observe.uf2` (wireless, 115200 log), run to a `wdt=1`
death, read the next boot's block (all decimal unless noted, CRCs 8-hex):

- `w9epoch post=%lu current=%lu`
- `w8flash-total safe=%lu/%lu tag=%lu/%lu del=%lu/%lu lkput=%lu/%lu lkdel=%lu/%lu` (begins/ends)
- `w9flash-boot ...` (same fields, this-boot delta)
- `w9last store tag=%lx epoch=%lu rc=%ld dur=%lu`
- `w9last delete tag=%lx epoch=%lu rc=%ld dur=%lu`
- `w8tmr-total usb a=%lu f=%lu to=%lu empty a=%lu f=%lu to=%lu stats a=%lu f=%lu to=%lu`
- `w9tmr-boot usb a=%lu f=%lu empty a=%lu f=%lu stats a=%lu f=%lu`
- `w9stage last=%lu n=%lu trail=%u,%u,...x8` (poll ids 1–10)
- `w9opstage last=S%lu n=%lu trail=%u,%u,...x8` (raw S-numbers 0,1,2,3,10,11,12)
- `w9bank snap0=%08lx snap1=%08lx dump0=%08lx dump1=%08lx active=%ld wroff=%lu chg0=%u chg1=%u`
- `w9ev last=0x%02lx n=%lu` / fresh-boot: `w9rec invalid (fresh boot; ring reset)`
- Sample (illustrative, not measured):
  `w9bank snap0=9f2ac411 snap1=9f2ac411 dump0=9f2ac411 dump1=9f2ac411 active=0 wroff=128 chg0=0 chg1=0`

9a decision table: last=S1 → outer lockout-acquire wait (H1-outer); last=S2/S3
→ inside callback, hung at/before `store_tag` entry (H1-inner or TLV-entry);
last=S10 without S11 → new signature (never observed); last=S11 without S12 →
outer unlock/release wait; past S3 reuses W5/W7/W8 interpretation. 9c branch:
(a) any `chg*=1` across an in-op death (host stays 0) → partial flash mutation
inside the op (H2昇格, pull W11); (b) `chg0=chg1=0` → zero physical mutation,
suspect pre-write-scan CPU-side loop → worker-design amendments (boot-time
bank-integrity check + scan iteration cap + safe reformat path).

## 6. Build evidence

Fresh temp dir `$env:TEMP\wab-w9o` (never `build/`):
`cmake -S . -B <tmp> -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0` → ok
(SDK 2.3.0, toolchain 15_2_Rel1, full-path cmake/ninja). First
`--target pico-bcon` FAILED (self-review F1 brace; no UF2 produced); after the
one-line fix, rebuild → **EXIT=0** (final link `pico-bcon.elf` ok).
ELF proof: `w9_state` (0x784 = 1920 B) at VMA `0x20000110` in
`.uninitialized_data` (NOLOAD, survives WDT reboot).

## 7. UF2 + artifacts

- `log/pico-bcon-w9-observe.uf2`, **827392 bytes**, SHA256
  `1BA217561A9034691ADFB574F01E12DE7B767A3F5E46B12D92BA1704588D12FD`.
- `diffs/w9o-tracked.diff` (73809 B, `git diff`; includes pre-existing
  Phase-3/Plan-A noise vs HEAD, W1/W5 precedent), `diffs/w9o-main-vs-backup.diff`
  (16748 B), `diffs/w9o-store-vs-backup.diff` (2863 B),
  `diffs/w9o-cmake-vs-backup.diff` (613 B), `diffs/w9o-diag_rec.c.txt`
  (10237 B), `diffs/w9o-diag_rec.h.txt` (7123 B). All non-empty.

## 8. Restore evidence

Restored 4 files from `backups/w9o/*` via `Copy-Item` (never
checkout/restore/clean); deleted `src/diag_rec.c/.h` (`Test-Path` False/False).
`status-after-w9o.txt` identical to `status-before-w9o.txt` (`Compare-Object`
empty → True). Hashes restored==backup==baseline, all True: main.c
`F40AFE1A…04918`, store.c `40456D82…C87726`, store.h `FF4B5284…FE340`,
CMakeLists `6E3628C3…B42D254`. Marker grep `W9DIAG|diag_rec|scratch` over
`src/*.c,*.h` → zero hits. Line endings preserved (store.c all-CRLF 255/0,
main.c LF-only). UF2 + diffs + backups retained; tree holds only the original
Phase-3/Plan-A entries.

## 9. Self-review findings

- F1 (caught by compiler, fixed, disclosed): the shim-block anchor edit consumed
  wired_loop's closing `}`; first build failed with "invalid storage class ...
  In function 'wired_loop'". Fixed by re-emitting the brace as a marked line
  (`} /* W9DIAG: close wired_loop ... */`); final build exit 0. Vs-backup diff
  content re-verified after the fix (marker coverage re-run, zero unmarked).
- F2: a mid-restore `Write-Output (... -eq ...)` printed `CMake False`; direct
  re-check showed restored==backup==baseline True and empty diff — a comparison
  artifact, not a file difference. Recorded so the transcript oddity is not
  mistaken for a restore failure.
- F3: shim signatures match `btstack_link_key_db.h` 9/9 (same pattern as W8);
  `hci_set_link_key_db(&w9_lk_shim)` type-matches (pointer-to-const).
- F4: `w9m[160]` fits the longest line (`w9bank` ≈ 106 chars worst-case).
- F5: `git status` shows no modified/new paths beyond the task's files plus the
  pre-existing tree entries; no SDK, BTstack, or wakecon paths touched.

## 10. Concerns

- C1: S-stages and poll stages share `scratch[2]` (latest wins); the NOLOAD
  trails are authoritative, scratch is a debugger hint only.
- C2: `opstage_trail` stores raw S-numbers, so S0=0 is indistinguishable from an
  empty slot except via `opstage_n` — documented here and in-code.
- C3: page-bitmap reduced to per-bank changed flags (see §2).
- C4: `active=-1` means `current_bank` invalid — treat as geometry-invalid.
- C5: on a true fresh flash, `prev=0` so `chg*` will read 1; compare consecutive
  WDT-reboot boots for the branch decision, not the virgin boot.
- C6: cost ≈ W8 class (FNV/record + 2×4 KB CRC32 once per boot, sub-ms);
  diagnostic only, fully reverted.
- C7: dump-time live re-read runs before Core1 launch — no flash op in flight.
- C8: `$env:TEMP\wab-w9o` build dir retained outside the repo; `build/` untouched.

(End of file)
