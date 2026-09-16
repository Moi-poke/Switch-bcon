# W8 report: epoch + boot-baseline diagnostic UF2 (resolves U3/U5 accumulation ambiguity)

Status: COMPLETE. No commits, pushes, PRs, branch ops. Tree restored byte-identical (hash-proven). `C:\Users\moilo\pico-wakecon` never touched. `build/` never used (build dir `$env:TEMP/wab-w8`). W6/W7 UF2s intact in `log/`. No secret key bytes anywhere.

## 1. Scratch ownership map (Step 2 gate — PASS, no BLOCKED)

Re-verified from SDK 2.3.0 sources (not just W7 precedent):
- Only writer of watchdog `scratch[]` in the SDK is `src/rp2_common/hardware_watchdog/watchdog.c`: `watchdog_enable()` → `scratch[4]=0x6ab73121`; `watchdog_reboot(pc!=0)` → `scratch[4]=0xb007c0d3,[5]=pc^magic,[6]=sp,[7]=pc`; `watchdog_reboot(0,…)` → `scratch[4]=0`. SDK-wide grep for `scratch[` confirms no other TU writes watchdog scratch (`powman` scratch is a different peripheral; `crt0.S` `stmia` to scratch is inside `#if PICO_NO_FLASH` under `PICO_CRT0_DEBUG_ENTRY_RESETS_VIA_BOOTROM` — compiled out for this flash UF2 build).
- `src/` users: zero hits (marker grep at pre-state).
- W8 map: `[0]`=magic/ver, `[1]`=index+CRC, `[2]`=stage mirror (unchanged W7 roles), **`[3]`=boot epoch counter (u32, new)**; `[4..7]` SDK-RESERVED, never touched.

## 2. Recorder with epoch + baseline (Step 3)

Created `src/diag_rec.c` (136 lines) + `src/diag_rec.h` (113 lines), no name collision, both deleted at restore. Every non-blank line W8DIAG-marked (mechanically verified: 0 blank, 0 unmarked in each).
- Ring: `static w8_state_t w8_state __attribute__((section(".uninitialized_data")))` — no initializer. ELF-verified: section exists, `w8_state` size `0x758` (1880 B) at VMA `0x20000110`; matches hand computation exactly (344 B header/counters/baseline + 96x16 B ring).
- Magic `0xD1A60008`, ver 8, FNV-1a/32 CRC (whole state, crc word zeroed), 96-entry `{ts,kind,a1,a3}` ring. Same Core0-only / no-printf / no-flash / no-mutex / no-secrets discipline as W7.
- New vs W7: `boot_epoch` (this boot, mirrored to `scratch[3]`), `post_epoch` (previous boot's epoch, captured before increment; 0 = fresh power/reflash boot), `last_epoch[6]` per flash op, boot-baseline block `base_begins/ends[6]`, `base_tmr_add/fire[4]`, `base_epoch`.
- `diag_rec_init` strict order: validate magic/CRC → keep `post_epoch=boot_epoch` if valid else memset-fresh with `post_epoch=0` → `boot_epoch=post+1`, mirror to `scratch[3]` → snapshot `base_*` → recompute CRC + mirrors. WDT-reason-flag-first is satisfied in `main.c` (flag read immediately after banner, before `diag_rec_init`; `watchdog_caused_reboot()` verified pure-read in SDK `watchdog.c:116-126`, original assignment kept with identical value — zero behavior change). Baseline precedes all init timer arms and any flash op (arms/flash only occur later in `main`/handlers).
- Honesty note: the kept `w8rec invalid (fresh boot; ring reset)` dump path is unreachable post-`init` (init always leaves valid state) — kept per brief. Fresh boot is instead identified by `post=0 current=1` with boot deltas == totals.

## 3. Flash-op counters (Step 4)

- `src/bt/store.c` (+12/−0 vs backup, all lines W8DIAG-marked, error semantics unchanged): same three choke points as W7 (`tag_store_safe` outer incl. refusal path `0xFFFFFFFFu`, `tag_store_fn` inner, `tag_delete_fn` with rc=0 documented for void return). Includes `pico/time.h` + `diag_rec.h` (W5/W7-proven). Epoch/tag land in `last_tag/last_epoch` inside `diag_rec_flash` (both phases stamp current `boot_epoch`).
- Link-key shim in `src/main.c`: same 9-method forwarder as W7 (signatures unchanged from `w7-main-vs-backup.diff`, `w7_`→`w8_` rename only), `put/delete` wrapped with begin/end + duration. Hookup replaces exactly the 2 `hci_set_link_key_db` lines (−2, the only deletions in the task).
- Boundary (carried, per brief): SDK-internal erase/program granularity is OUT of scope. **`delete_found` (whether BTstack's internal `delete_tag_until_offset` matched an entry) is NOT observable without SDK edits — not attempted.** Reading guide correlates the DEL counter with the hci_dump `Erase tag '...'` log line (present => physical write; absent => target-missing scan or other-boot value, disambiguated by `last_epoch` + `last_tag`).

## 4. Fine stages + owned-timer accounting (Step 5)

Verbatim W7 (`w7-main-vs-backup.diff` anchors): 10 stage points in `poll_tick`/`usb_handler`, FIRE-at-entry / ADD-after-`add_timer` for usb/empty/stats incl. the 3 init arms. `reconnect_timer` untouched. `tmr_rem` field kept from W7 schema (no call sites for these timers — expected zero).

## 5. hci_dump ON + extended boot dump (Step 6)

6 hci_dump lines re-added at identical anchors (markers `W7DIAG`→`W8DIAG`). `diag_rec_init()` right after the banner; `w8_boot_dump()` after the `wdt=` banner (which precedes the 3 init-arm ADDs, so a fresh dump shows zero boot deltas — deltas become meaningful on the post-death boot). Exact line formats implemented:

```
w8epoch post=<prev> current=<new>
w8flash-total safe=a/b tag=a/b del=a/b lkput=a/b lkdel=a/b
w8flash-boot  safe=a/b tag=a/b del=a/b lkput=a/b lkdel=a/b
w8last store tag=<hex> epoch=<e> rc=<r> dur=<us>
w8last delete tag=<hex> epoch=<e> rc=<r> dur=<us>
w8tmr-total usb a=.. f=.. to=.. empty a=.. f=.. to=.. stats a=.. f=.. to=..
w8tmr-boot  usb a=.. f=.. empty a=.. f=.. stats a=.. f=..
w8stage last=.. n=.. trail=..,..,..,..,..,..,..,..
w8ev last=0x.. n=..
```

(`*-boot` = current-minus-baseline, u32 subtraction; `to=` only in total lines; `w8last store` = `STORE_SAFE` outer op, `w8last delete` = `DELETE_TAG`; `tag=` is `%lx` hex, e.g. `4243484f`=`BCHO`, `42435731`=`BCW1`.)

## 6. Build (Step 7)

- Configure: `cmake -S . -B $env:TEMP/wab-w8 -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0` (full-path cmake/ninja from `$env:USERPROFILE\.pico-sdk`, SDK 2.3.0, toolchain 15_2_Rel1) → ok. `build/` untouched.
- Build: `cmake --build $env:TEMP/wab-w8 --target pico-bcon` → **exit 0, 229/229** (incl. `src/diag_rec.c.obj` [29/229], `src/bt/store.c.obj` [50/229], `src/main.c.obj` [73/229]). No new warnings/errors; no BUMP flag.
- CMakeLists: one-line `src/diag_rec.c` hunk (W8DIAG-commented) — sources are an explicit list, so it was required.

## 7. UF2 + artifacts (Step 8, all non-empty)

- `log/pico-bcon-w8-epoch.uf2`, **824832 bytes**, SHA256 `226FDBECEAA00C554C5F137454E410C0881B5DD74B4AF540B6C296C4529BFED5`.
- `diffs/w8-tracked.diff` (59630 B, brief-mandated `git diff` of the 4 tracked paths — includes pre-existing Phase-3-vs-HEAD noise, as in W1/W5/W7).
- `diffs/w8-diag_rec.c.txt` (6399 B), `diffs/w8-diag_rec.h.txt` (5159 B).
- Extra: `diffs/w8-main-vs-backup.diff` (15385 B, +116/−2), `diffs/w8-store-vs-backup.diff` (2258 B, +12/−0).
- `store.h` untouched (0 changes). vs-backup added lines with no W8DIAG marker: 0/0/0 (main/store/cmake).

## 8. Restore evidence (Step 9)

- Restored 4 files from `backups/w8/*` via `Copy-Item` (no git checkout/restore/clean); DELETED exactly the 2 created files. Never used git clean.
- `status-after-w8.txt` identical to `status-before-w8.txt` (`Compare-Object` empty) → True.
- Hashes equal to Step-1 baseline, all True: main.c `690F3A79…4E10C27A`, store.c `40456D82…D772C87726`, store.h `FF4B5284…8490FE340`, CMakeLists.txt `6E3628C3…951B42D254`.
- `Get-ChildItem src/diag_rec.*` → empty (0) → True; marker grep `W7DIAG|W8DIAG|diag_rec|scratch` over `src/` → zero → True.

## 9. Self-review

- Vs-backup diffs show exactly the specified additions; zero unmarked added lines; new files 100% marked. Shim 9/9 signatures unchanged from W7 proof; error/return semantics unchanged everywhere.
- Gates/STOPs obeyed: backups before edits; scratch gate passed with SDK-source-level map (`[3]` free proven, not assumed); `.uninitialized_data` verified in-tree AND in-ELF with exact size match; no SDK/BTstack source edits; wakecon untouched; W6/W7 UF2s not rebuilt and intact.
- Recorder cost ≈ CRC over 1880 B (~2 µs) per record; ~12 records/ms in usb path ≈ 2–3% Core0 — diagnostic-UF2 only, do not ship.
- Caveats (same as W7): NOLOAD survives WDT reboot but NOT power loss/reflash/debugger-RAM-clear (those boots show `post=0 current=1`, boot deltas == totals — expected). Stall inside a flash op longer than the 2 s window resets before END → begin>end with `last_tag`/`last_epoch` identifying the op.

## 10. Concerns

- `tests/host` build not attempted (`diag_rec.h` needs SDK headers; firmware-only concern; everything reverted).
- hci_dump include style `"hci_dump.h"` reused verbatim from compile-proven W6/W7.
- Epoch wraps mod 2^32 (irrelevant in practice); power/reflash restarts at 1 — `post_epoch` makes this unambiguous.

## 11. Reading guide — worked expectations (flash UF2 at 115200 baud log, run to `wdt=1` death, read `w8*` lines on next boot)

- U3 (`del=1/1` question): if `w8flash-boot del=0/0` while `w8flash-total del=1/1` with `w8last delete epoch=<post>` (and `tag=` of a prior boot's op) ⇒ other-boot value, case closed — no delete ran in the death boot. If instead boot `del=1/1` with `delete tag=4243484f epoch=<current>` ⇒ live host-forget path (`store_host_forget`, KEY_DELETE FX); `tag=42435731` ⇒ cap-forget path (`store_cap_forget`) — which currently has no callers, so flag as a NEW finding. Cross-check hci_dump for the `Erase tag` line (present ⇒ physical write happened).
- U5 (`adds-fires=+2` question): exact arithmetic — steady state per boot is `adds − fires = 1` (every fire is followed by a re-arm; the +1 is the currently-armed live timer). If every timer's **boot** delta satisfies `a−f=1` while **totals** show `a−f=+2` ⇒ the extra +1 is cross-boot accumulation (previous boot's armed-but-never-fired timer or its init arm), case closed. If any timer's boot delta shows `a−f=+2` ⇒ live double-arm in the death boot — new finding.
- Duration note: `dur=` (µs, alongside tag+epoch) already localizes stall-in-write (begin>end) and quantifies op cost. Program/erase/lockout-split instrumentation is deliberately NOT added: it would require SDK/BTstack source edits (`flash_range_*` granularity is SDK-internal, out of scope per constraints) for no additional fault-boundary information — the wrapper+shim already observe 100% of initiated ops from in-repo paths.
