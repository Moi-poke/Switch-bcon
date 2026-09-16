# W7 report: Unified diagnostic instrumentation (counters + fine stages + timer accounting)

Status: COMPLETE. No commits, no pushes, no PRs. Tree restored byte-identical (hash-proven). No subagents used. `C:\Users\moilo\pico-wakecon` never touched. `build/` never used (build dir `$env:TEMP/wab-w7`).

## 1. Scratch ownership map (Step 2 gate — PASS, no BLOCKED)

HW: `watchdog_hw_t.scratch[8]`, indices 0–7 (SDK 2.3.0 `src/rp2350/hardware_structs/include/hardware/structs/watchdog.h:52`).

SDK writers (only `src/rp2_common/hardware_watchdog/watchdog.c` writes watchdog scratch):
- `watchdog_enable()` → `scratch[4] = 0x6ab73121` (WATCHDOG_NON_REBOOT_MAGIC); read back by `watchdog_enable_caused_reboot()`.
- `watchdog_reboot(pc!=0)` → `scratch[4]=0xb007c0d3, [5]=pc^magic, [6]=sp, [7]=pc`; `watchdog_reboot(0,…)` → `scratch[4]=0`.
- This is the W3 lesson: `scratch[4]` collides with the SDK enable-magic — never touch.

Other SDK scratch traffic: `pico_crt0/crt0.S` (+`crt0_riscv.S`) writes `scratch[2..7]` ONLY under `#if PICO_NO_FLASH && PICO_CRT0_DEBUG_ENTRY_RESETS_VIA_BOOTROM` (debugger-loaded NO_FLASH path) — compiled out for this flash UF2 build; noted and avoided anyway. `pico_low_power` writes `powman_hw->scratch` (different peripheral, irrelevant).

`src/` uses: zero (`Get-ChildItem src -Recurse | Select-String scratch` → no hits; all prior W-task crumbs reverted).

W7 map (4 free regs 0–3 ≥ 3 needed → gate PASS):
- `scratch[0]` = recorder magic/version (`0xD1A60007`), written at `diag_rec_init`.
- `scratch[1]` = packed recorder index + CRC (low16 = ring slot, high16 = CRC>>16), refreshed on every record.
- `scratch[2]` = critical-stage mirror (last poll/usb stage id, 0 = none yet).
- `scratch[3]` = SPARE, never written by W7.
- `scratch[4..7]` = SDK-RESERVED (enable magic / reboot pc/sp). Never touched.
- No IRQ tick (the brief's "may bump a single scratch counter" deliberately declined): with no IRQ writer the single-writer (Core0-only) discipline is total, and the stage mirror + timer fire counters already give run-loop-independent liveness. `scratch[3]` stays free for that future use.

## 2. RAM flight recorder (Step 3)

Created `src/diag_rec.c` + `src/diag_rec.h` (no name collision; both deleted at restore). Every non-blank line carries `W7DIAG` (mechanically verified: 110/110 in `.c`, 98/98 in `.h`).
- Ring: `static w7_state_t w7_state __attribute__((section(".uninitialized_data")))` — NO initializer (NOLOAD is never zero-filled).
- Section verified two ways (no BLOCKED): (a) in-tree link scripts `memmap_default.incl → sections_default.incl → sections_default_data.incl → section_uninitialized_data.incl` (`.uninitialized_data (NOLOAD)` in RAM); (b) in the built ELF: section exists, `w7_state` (0x6e4 = 1764 B) at VMA `0x20000110`. Size matches the hand computation exactly (228 B header/counters + 96×16 B entries).
- Header: magic `0xD1A60007`, ver 7, `idx` (monotonic; slot = idx%96), `n`, FNV-1a/32 CRC over the whole state with the crc word forced to zero (recomputed per record; ~1.8 KB hash ≈ 2 µs — negligible).
- Entry: `{ts, kind, a1, a2, a3}` (16 B): FLASH_OP (a1=(op<<1)|phase; begin: a2=tag,a3=len; end: a2=rc-as-u32,a3=dur_us), STAGE (a1=stage id), TIMER (a1=(id<<2)|act; a2=timeout_ms,a3=fire count), EV (a1=event byte). Ops 1..5 (store_safe/store_tag/delete_tag/lk_put/lk_del), timers 1..3 (usb/empty/stats).
- Rules honored: Core0-only writes (no IRQ path exists), no printf in recorder code (boot dump lives in `main.c`), no flash writes, no mutex; no secrets ever stored (no BD_ADDR/keys/payloads — LK records carry zeros).

## 3. Flash-op counters (Step 4, no SDK/BTstack edits)

(a) `src/bt/store.c` (+12/−0 vs backup; CRLF preserved, all lines W7DIAG-marked; error semantics unchanged):
- `tag_store_safe`: `W7OP_STORE_SAFE` begin (tag,len) before `flash_safe_execute`, end (rc,dur) after; refusal path records end with `0xFFFFFFFFu`.
- `tag_store_fn` (runs inside `flash_safe_execute`, Core0): `W7OP_STORE_TAG` begin/end around `tlv->store_tag` (RAM-only record calls; the IRQ-disabling happens inside `flash_range_*`, our calls sit outside it).
- `tag_delete_fn`: `W7OP_DELETE_TAG` begin/end around `tlv->delete_tag` (void return → rc recorded 0, documented).
- Added includes `pico/time.h` (W5-proven requirement for `time_us_32`) + `diag_rec.h`. Verified these two callbacks are the ONLY `store_tag`/`delete_tag` call sites in `src/`, and all four public writers (`store_host/color/cap_save/wired`) funnel through `tag_store_safe` — 100% of initiated tag ops observed.
(b) Link-key-db shim in `src/main.c`: static `w7_lk_shim` (`btstack_link_key_db_t`, static storage) forwarding all 9 methods with exact signatures from `classic/btstack_link_key_db.h`; `put_link_key`/`delete_link_key` wrapped with `W7OP_LK_PUT`/`W7OP_LK_DEL` begin/end + duration. Call site changed 2-for-2 (`w7_lk_inner = …get_instance(…)` + `hci_set_link_key_db(&w7_lk_shim)`; the 2 removed lines are the only deletions in the whole task).
- Boundary (per brief): SDK-internal erase/program granularity inside `flash_range_*` is OUT of scope (SDK sources outside this repo, never edited). Wrapper + shim observe 100% of initiated ops: zero begins ⇒ zero physical ops from firmware paths.

## 4. Fine stages + usb_handler stages (Step 5, `src/main.c`)

`poll_tick`: `POLL_ENTER`(1) first line; `POLL_BODY_DONE`(2) after inbox drain (before `exec_fx`); `WDT_UPDATE_DONE`(3) after `watchdog_update()`; `POLL_TAIL_DONE`(4) + `POLL_RETURN_IMMINENT`(5) last lines. `usb_handler`: `USB_HANDLER_ENTER`(6) first; `POLL_RETURNED`(7) right after poll returns; `SET_TIMER_DONE`(8) after set; `ADD_TIMER_DONE`(9) after add; `USB_HANDLER_RETURN_IMMINENT`(10) last. Each records into the ring AND mirrors to `scratch[2]`. Localization ladder: last∈{1..5}=poll never returned (sub-localized) / =7 without 8 = set missed / =8 without 9 = add missed / =9/10 with frozen fire counters = re-armed but never fired.

## 5. Owned-timer accounting (Step 6, no BTstack list walk)

`usb/empty/stats` FIRE at handler entries; ADD after every `add_timer` (3 handler re-arms + 3 init arms) with `(uint32_t)ts->timeout` as last-timeout value (no call-site restructuring; also exact for `empty`'s `probe_send_interval_ms()`). `reconnect_timer` untouched per brief. REMOVE counter exists in schema but has no call sites for these three timers (only `remove_timer` in `src/main.c` targets `reconnect_timer`) — expected zero, documented.

## 6. hci_dump ON + boot dump (Step 7)

- The 6 W6 lines re-added at identical anchors (include block + before `hci_set_bd_addr`), with markers renamed `W6DIAG→W7DIAG` (required: the restore gate greps `W7DIAG`, and every added line must be marked). Content/placement otherwise exact.
- `diag_rec_init()` right after the `=== pico-bcon ===` banner (continues a valid recorder across WDT reboot; resets only on bad magic/CRC).
- `w7_boot_dump()` (static, `main.c`, boot context only, `probe_line`+`snprintf` w/ 160 B buffer) called after the `wdt=` banner: prints `w7rec`, `w7flash` (begins/ends per op), `w7lastrc` (rc + durations), `w7tmr` (adds/fires/last-timeouts), `w7stage` (last + 8-trail), `w7ev`. Prints `w7rec invalid (fresh boot; ring reset)` on first flashing.

## 7. Build (Step 8)

- Configure: `cmake -S . -B $env:TEMP/wab-w7 -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0` → ok (SDK 2.3.0, toolchain 15_2_Rel1; ninja+cmake via `.pico-sdk` full paths on PATH per W5 lesson; `build/` untouched).
- Build: `cmake --build $env:TEMP/wab-w7 --target pico-bcon` → **exit 0, 229/229** (incl. `src/diag_rec.c.obj` [26/229], `src/bt/store.c.obj` [56/229], `src/main.c.obj` [70/229]). No new warnings/errors; no BUMP flag.
- CMakeLists: sources are an explicit list (not globbed), so the one-line `src/diag_rec.c` hunk (W7DIAG-commented) was required and added.

## 8. UF2 + artifacts (Step 9, all non-empty)

- `log/pico-bcon-w7-diag.uf2`, **823808 bytes**, SHA256 `8B7A25FDDCA05C6C745E54BCBD824C9E127879966D2720CD9C8C34893E6CD5EE`.
- `diffs/w7-tracked.diff` (103540 B, brief-mandated `git diff` of the 4 tracked paths — includes pre-existing wireless-baseline noise vs HEAD, as in W1/W5).
- `diffs/w7-diag_rec.c.txt` (5058 B), `diffs/w7-diag_rec.h.txt` (4384 B) — review surface for the untracked files.
- Extra (W1/W5 precedent): `diffs/w7-main-vs-backup.diff` (25470 B), `w7-store-vs-backup.diff` (4512 B), `w7-cmake-vs-backup.diff` (1134 B) — the authoritative minimal record.

## 9. Restore evidence (Step 10)

- Restored 4 files from `backups/w7/*` via `Copy-Item` (no git checkout/restore/clean); DELETED exactly the 2 created files (`src/diag_rec.c`, `src/diag_rec.h`).
- `status-after-w7.txt` identical to `status-before-w7.txt` (`Compare-Object` empty).
- Hashes, all True (equal to Step-1 baseline): main.c `690F3A79…4E10C27A`, store.c `40456D82…D772C87726`, store.h `FF4B5284…8490FE340`, CMakeLists.txt `6E3628C3…951B42D254`.
- `Get-ChildItem src/diag_rec.*` → empty (files gone); marker grep `W7DIAG|diag_rec|scratch` over `src/*.c,*.h` → **zero hits**.
- Line endings preserved (verified pre-restore): main.c LF-only, store.c all-CRLF (vs-backup +12/−0), CMakeLists LF-only. (New files landed CRLF via editor default — compiles identically, deleted at restore.)

## 10. Files changed

- `src/`: none (post-restore). New files only: `log/pico-bcon-w7-diag.uf2` + workspace artifacts (`status-before/after-w7.txt`, `diffs/w7-*`, `backups/w7/*`, this report).

## 11. Self-review

- Vs-backup diffs show exactly the specified additions: main.c +97/−2 (the −2 are the replaced `hci_set_link_key_db` lines), store.c +12/−0, store.h untouched, CMakeLists +1. Zero unmarked added lines in main.c and store.c (queried); new files 100% marked per-line.
- Shim signatures match `btstack_link_key_db.h` field-for-field (9/9); `hci_set_link_key_db` takes pointer-to-const → `&w7_lk_shim` is an exact match. Error/return semantics unchanged everywhere (shim forwards returns; store paths keep identical returns).
- Gates/STOPs obeyed: backups before edits; scratch gate passed with written map (no overlap with SDK [4..7]); `.uninitialized_data` verified in-tree AND in-ELF (no BLOCKED); no SDK/BTstack source edits; wakecon untouched.
- W6 UF2 was not rebuilt (per brief; `log/pico-bcon-w6-hcidump.uf2` intact).

## 12. Concerns

- Recorder cost ≈ 2 µs/record (FNV over 1764 B); ~12 records/ms in the usb path ≈ 2–3% Core0. Fine for a diagnostic UF2; do not ship.
- `.uninitialized_data` survives WDT reboot (SRAM retained) but NOT power loss, UF2 reflash, or RAM-clearing debugger resets → those boots print `w7rec invalid` and reset. Expected.
- WDT-window caveat (W5 precedent): a stall inside a flash op longer than the 2 s window resets before END records → begin>end signature with `last_tag` identifying the op.
- `tests/host` build not attempted (`diag_rec.h` needs SDK headers; firmware-only concern; everything reverted).
- hci_dump include style `"hci_dump.h"` reused verbatim from compile-proven W6 (BTstack include dirs come from `pico_btstack_classic`).

## 13. Reading guide — which recorder fields answer the four questions

Flash `log/pico-bcon-w7-diag.uf2` (wireless, 115200 baud log), run to a `wdt=1` death, read the `w7*` boot-dump lines on the next boot. (`w7flash a/b` = begins/ends.)

(i) Physical-flash-zero proof (Session-1-type deaths): the `w7flash` line. ALL five `begins` zero (`safe/tag/del/lkput/lkdel = 0/...`) ⇒ no flash op was initiated through any in-repo path (tag choke points cover 100% of TLV tag writes/deletes; the shim covers 100% of link-key puts/deletes) ⇒ no physical erase/program could have been issued. Non-zero with `begins==ends`, `rc=0` and death later ⇒ post-write bucket. Any `begins>ends` ⇒ died INSIDE that op (stall-in-write); `w7lastrc` (`dur=` + which op) + `last_tag` identify it.
(ii) Poll-vs-rearm: `w7stage last=` + `trail=`. Normal run repeats `6,1,2,3,4,5,7,8,9,10` every 1 ms. `last`∈{1..5} (esp. 1 without 5, or 2/3/4 stale) ⇒ poll never returned (wedge inside `poll_tick`; the value sub-localizes: 1 pre-body, 2 post-drain, 3 post-feed, 4 tail). Trail ending `7` without `8` ⇒ returned but `set_timer` missed; `8` without `9` ⇒ set done, `add_timer` missed; `9/10` with frozen `w7tmr usb f=` ⇒ re-armed but never fired (run-loop wedge). Cross-check `scratch[2]` with a debugger if the ring is suspect.
(iii) usb_timer presence/firing: `w7tmr usb a=%lu f=%lu to=%lu`. `f` large and growing ⇒ present and firing (`to=` should read 1). `a≫f` ⇒ re-armed but starved. `a==f≈0` ⇒ never (re)armed. `empty`/`stats` columns discriminate a global run-loop stall (all frozen) from a usb-specific one.
(iv) T3-delete physical-op question: `lkdel=a/b` plus `del=a/b` on the `w7flash` line. The KEY_DELETE FX path calls `gap_delete_all_link_keys()` (per-key `delete_link_key` → shim `lkdel`) + `store_host_forget()` (→ `tag_delete_fn` → `del`). Non-zero `lkdel`/`del` begins ⇒ T3 delete DID initiate physical-op writes; all-zero ⇒ the delete path issued nothing (pure RAM/log). Pair with (i): `begins==ends`, `rc=0` ⇒ the delete's flash ops completed and any later death is post-write.
