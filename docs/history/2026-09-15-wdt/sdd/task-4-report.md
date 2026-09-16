# W4 report: Deferred flash + breadcrumb combined (wedge-signature capture)

## 1. Changes applied (verbatim W1 + verbatim W3)

### W1 part — deferred host-flash write (Death-B test)
- `src/bt/store.c`: extracted `store_host` body into `static void store_host_inner(const bd_addr_t addr, bool force)`; the `memcmp` early-return is skipped when `force` is true. `store_host()` calls inner with `false` (byte-identical behavior for existing callers); new `store_host_force()` calls inner with `true`. Save counter + `host saved (n=)` log stay shared.
- `src/bt/store.h`: declared `void store_host_force(const bd_addr_t addr); // W1DIAG: ...`.
- `src/main.c`: (a) `static btstack_timer_source_t defer_host_timer; /* W1DIAG ... */` next to the other timer decls; (b) one-shot `defer_host_handler()` (calls `store_host_force(probe_host_addr)`, never re-arms) next to `empty_handler`; (c) `btstack_run_loop_set_timer_handler(&defer_host_timer, &defer_host_handler);` with the other registrations; (d) `handle_hid_meta` OPEN branch no longer calls `store_host(a)` — copies `a` to `probe_host_addr`, sets `probe_host_known`, re-arms the 3 s one-shot, logs `hid open. host %s (flash in 3s)`. RAM update stays immediate; only the flash op moves. No include added (`string.h` already present).
- W1DIAG markers NOT added to the new main.c logic beyond the timer-decl comment — per brief, the exact W1 hunks were reused so the combined diff stays reviewable; the W3 markers distinguish the new part.

### W3 part — IRQ breadcrumb locator (Death-A test)
- All added lines `/* W3DIAG */`-marked, placements per the W3 brief (anchors matched, no adaptation needed — tree unchanged since W1/W3, all backup hashes matched current files before editing).
- Apparatus after `s_reboot_at` statics: `W3_MAGIC 0xC0DECAFEu`, `s_w3_timer`, `w3_irq_tick()` (`scratch[1]++`, `scratch[0]=W3_MAGIC`), `w3_irq_cb()` wrapper returning true.
- `poll_tick` first body line: `scratch[2]++` (poll entry count); after inbox-drain loop before `exec_fx`: `scratch[3]=1u`; just before `watchdog_update()`: `scratch[3]=2u`.
- `packet_handler` line after `ev = hci_event_packet_get_type(packet);`: `scratch[4]=(uint32_t)ev+1u`.
- Registration after the `defer_host_timer` setup: `add_repeating_timer_ms(100, w3_irq_cb, NULL, &s_w3_timer);`.
- Boot dump after the `wdt=` printf: prints `w3pm irq=%lu poll=%lu stage=%lu ev=0x%02lx` via `probe_line` when `scratch[0]==W3_MAGIC`.
- Includes added: none (`stdio.h`, `hardware/watchdog.h`, `pico/time.h` chain all already available — same finding as W3).

### Scratch zero-user gate (re-run per brief)
- `Get-ChildItem -Recurse -File -Include *.c,*.h src | Select-String -Pattern "scratch"` → **ZERO hits → GATE PASS**, proceeded. T4 removal still holds.

## 2. Build (Step 4)
- Configure: `cmake -S . -B $env:TEMP/wab-w4 -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0` → exit 0 (SDK 2.3.0, toolchain 15_2_Rel1 auto-detected; cmake/ninja via `.pico-sdk` full paths; never touched `build/`).
- Build: `cmake --build $env:TEMP/wab-w4 --target pico-bcon` → **exit 0 (228/228, incl. `src/main.c.obj` + `src/bt/store.c.obj`)**. No new warnings/errors observed; no BUMP flag.

## 3. Artifacts (Step 5)
- UF2: `C:\pico-bcon\log\pico-bcon-w4-defer-crumb.uf2`, **817664 bytes**, SHA256 `3C22CFEDBDC3A1D599C1D2DD6C27C66D6D64FE4DF9A4580B579AA91359CC724C`.
- `diffs\w4-main.diff` (`git diff -- src/main.c`, 87694 bytes): mixes handoff baseline noise vs HEAD — NOT W-only evidence (same caveat as W1/W3).
- `diffs\w4-store.diff` (`--no-index` backup-vs-current store.c, 2294 bytes): non-empty, W1-only hunks.
- `diffs\w4-store-h.diff` (`--no-index` backup-vs-current store.h, 1116 bytes): non-empty third diff since store.h was touched (untracked file, appears in neither of the above).
- `diffs\w4-main-vs-backup.diff` (extra, `--no-index` backup-vs-current main.c): clean W-only evidence for main.c, same role as `w3-vs-backup.diff`.

## 4. Restore evidence (Step 6)
- Restored all three files via `Copy-Item` from `backups\w4\*.bak` (no git checkout/clean; no commits/pushes/PRs at any point).
- Hashes (current == backup → True for all three):
  - `src/main.c`: `690F3A7937571F7240BDC2922E5F4BCB7DAEB1A83963EECCC3A9C7034E10C27A` → **True**
  - `src/bt/store.c`: `40456D8201792366F838FDC2121CC5370E61E11ED15B5D9D21CF45D772C87726` → **True**
  - `src/bt/store.h`: `FF4B52843FB98C9E545350660A0D0F9B3554143BE19C09CE695FF308490FE340` → **True**
- `status-before-w4.txt` vs `status-after-w4.txt`: **identical** (Compare-Object empty).
- Post-restore grep `W1DIAG|W3DIAG|defer_host|store_host_force|scratch` over `src/*.c,*.h` → **zero hits**.

## 5. Files changed (net vs pre-task tree)
- None in `src/`. Only new files: `log/pico-bcon-w4-defer-crumb.uf2` + workspace artifacts under `bcon-wab/` (`status-before/after-w4.txt`, `diffs/w4-*.diff`, `backups/w4/*`, this report). HEAD still `c366bf4`; `git status` shows only the pre-existing handoff baseline entries.

## 6. Self-review
- Programmatic check on added-line sets: `w4-main-vs-backup == w1-main-vs-backup ∪ w3-vs-backup` exactly — **44 added lines = 15 (W1) + 29 (W3), zero extra, zero missing**; removed-line sets likewise equal (the 2 W1 removals: `store_host(a);` + the `saved` snprintf). store.c/store.h W4 diffs are byte-identical to the W1 diffs modulo the backup-path prefix.
- No logic reorder, no include changes, only `src/main.c` + `src/bt/store.c` + `src/bt/store.h` touched. Post-restore greps + triple-hash proofs confirm full removal.
- Backup/restore discipline followed exactly (baseline status, 3 backups, UF2 + diffs, byte-restore, hash/status proofs). No subagents used. Did not touch `C:\Users\moilo\pico-wakecon`.

## 7. Concerns
1. Same `scratch[4]` caveat as W3 (see reading guide below): nobody in `src/` calls `watchdog_enable_caused_reboot()`, so the ev breadcrumb is safe in this diagnostic; do not reuse `scratch[4]` in production code.
2. If the HID-open peer address equals the already-known host AND the deferred timer fires, `store_host_force` performs a flash write even though `store_host` would have skipped it — intended (forces the Death-B write deterministically).
3. If disconnect precedes the 3 s firing, the save still happens — intended and harmless (per W1 brief).
4. Registration of both the defer timer and the W3 IRQ timer lives in the wireless path only; the Death test must run the wireless (`WIRED_DEFAULT=0`) build — which is what was built.
5. Reviewers must scope main.c review to `w4-main-vs-backup.diff` / DIAG-marked lines; `w4-main.diff` contains handoff baseline noise.

## 8. Post-mortem reading guide (from the W3 report, incl. ev-field caveat)
After flashing `log/pico-bcon-w4-defer-crumb.uf2` and reproducing the death, capture the boot banner on UART0. If the previous boot wedged into a WDT reset you will see:
`w3pm irq=<I> poll=<P> stage=<S> ev=0x<EE>` (only printed when `scratch[0]==0xC0DECAFE`, i.e. the previous boot ran the W4 build long enough for one 100 ms IRQ tick).
- **Wedge signatures**: `poll` frozen (not advancing across deaths) + `stage` tells WHERE: `S=1` = wedged after inbox drain (`exec_fx`/flush/pack/usb/`link_poll` region); `S=2` = reached `watchdog_update()` area yet WDT still fired (feed starved downstream, e.g. inside the `watchdog_update` path or IRQ lockup); `S` stale/0 with `poll` frozen = wedged inside/before the inbox-drain loop or `poll_tick` never entered. `ev` = last HCI event type before death (correlate with hid-open 0x66/0x6a etc.). `irq` advancing while `poll` frozen = run loop wedged but IRQs alive (classic loop wedge, not a global lockup); `irq` also frozen/low = IRQ-level stall or very early death (check `wdt=` and `core1 boot=` lines).
- **Caveats**: first boot after flashing prints no `w3pm` (fresh scratch); UF2-drag or `watchdog_reboot()` (WIRED_MODE switch path) clears/overwrites scratch — only trust `w3pm` after a genuine WDT death (`wdt=1`). Counters are cumulative since the W4 boot, not per-second — compare across repeated deaths, not absolute values.
- **ev-field caveat**: `scratch[4]` shares its register with the SDK's `watchdog_enable()` boot marker (read by `watchdog_enable_caused_reboot()`). Nobody in `src/` calls that function and `main.c` uses the scratch-independent `watchdog_caused_reboot()`, and the boot dump reads `scratch[4]` before `watchdog_enable()` while `packet_handler` writes it after — so the `ev=` value is trustworthy in this build. But `watchdog_enable_caused_reboot()` would misreport under a W4 boot, and any future production use of `scratch[4]` would collide with this breadcrumb.
- **W4-specific reading**: the deferred write fires ~3 s after `hid open ... (flash in 3s)`. If the death follows the flash by ~2 s AND `w3pm` shows a frozen `poll` with `S=1`/`S=2`, the wedge is post-flash in the run loop (Death-B confirmed with location). If no death occurs within seconds of the deferred write across repeated connects, the immediate-callback context (not the flash itself) is implicated instead.
