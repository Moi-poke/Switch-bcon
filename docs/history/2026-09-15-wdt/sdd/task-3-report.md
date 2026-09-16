# W3 report: IRQ breadcrumb locator (Death-A test)

## 1. Scratch-gate result (Step 1)
- `status-before-w3.txt` recorded; working tree was already dirty from handoff baseline (M CMakeLists.txt, spec/protocol_v3.md, src/main.c, etc.).
- `src/main.c` backed up to `backups/w3/src_main.c.bak`; `diffs/w3-baseline-check.diff` recorded.
- Grep `scratch` over `src/` (`*.c,*.h`): **ZERO hits — GATE PASS**, proceeded.
- Grep `W3DIAG` over `src/` before edits: zero hits (clean).

## 2. Pre-flight verification (asked-now items, resolved by reading headers/SDK)
- **Repeating-timer callback signature**: `typedef bool (*repeating_timer_callback_t)(repeating_timer_t *rt)` (SDK 2.3.0 `src/common/pico_time/include/pico/time.h:741`). Brief's `static bool w3_irq_cb(repeating_timer_t *rt)` matches exactly — no adaptation.
- **snprintf/stdio**: `stdio.h` already included (`src/main.c:13`); snprintf already used. No include added.
- **watchdog_hw visibility**: already visible via `hardware/watchdog.h` (`src/main.c:26`), which includes `hardware/structs/watchdog.h` declaring `watchdog_hw` + `scratch[8]` (RP2350 structs header:52-55). No include added. T4 had not removed the include.
- **pico/time.h / add_repeating_timer_ms**: available via `pico/stdlib.h` → `pico/time.h` chain; `add_repeating_timer_ms` confirmed at `time.h:837`. No include added.
- **Includes added: none.** Report: zero.

## 3. Per-site implementation (all in `src/main.c`, every added line `/* W3DIAG */`-marked)
1. **Apparatus** (after `s_reboot_at` statics, ~L116-131): `W3_MAGIC 0xC0DECAFEu`, file-static `s_w3_timer`, `w3_irq_tick()` (`scratch[1]++`, `scratch[0]=W3_MAGIC`), `w3_irq_cb()` wrapper returning true. Extra `/* W3DIAG */` prefixes added vs brief so *every* line is marked; semantics identical to brief's exact code.
2. **poll_tick entry** (first body line): `watchdog_hw->scratch[2]++`.
3. **Stage=1** (after inbox-drain loop, before `exec_fx`): `scratch[3]=1u`.
4. **Stage=2** (just before `watchdog_update()`): `scratch[3]=2u`.
5. **packet_handler** (line after `ev = hci_event_packet_get_type(packet);`, L727-730 anchor as brief assumed — structure matched, no adaptation): `scratch[4]=(uint32_t)ev+1u`.
6. **Timer registration** (after `reconnect_timer` setup block, next to other timer setups, one-shot wireless path): `add_repeating_timer_ms(100, w3_irq_cb, NULL, &s_w3_timer);`.
7. **Boot dump** (after `wdt=` printf): `w3pm irq=… poll=… stage=… ev=…` via `probe_line` when `scratch[0]==W3_MAGIC`; every line marked.

## 4. Build (Step 3)
- Configure: `cmake -S . -B $env:TEMP/wab-w3 -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0` — exit 0. (Note: `cmake`/`ninja` not on PATH; used `C:\Users\moilo\.pico-sdk\cmake\v4.3.4\bin` + `...\ninja\v1.13.2`; toolchain auto-detected 15_2_Rel1. Never touched `build/`.)
- Build `--target pico-bcon`: **exit 0** (228/228, incl. `src/main.c.obj`, no new warnings/errors). One comment-only fix (blank line → `/* W3DIAG */`, see §5) triggered a 2-step rebuild + relink, also exit 0.

## 5. Artifacts (Step 4)
- UF2: `C:\pico-bcon\log\pico-bcon-w3-crumb.uf2`, **817664 bytes**, SHA256 `E3B2F062C5AF70AEE9F20C8F134AE4E1CAF844E95B8C0887198D8A266D1FC2D1`.
- `diffs/w3-main.diff` (`git diff -- src/main.c`): mixes handoff baseline noise (831 ins/139 del vs HEAD) as brief predicted — NOT W3-only evidence.
- `diffs/w3-vs-backup.diff` (`git diff --no-index` backup-vs-current): the clean W3-only evidence — 7 hunks, **31 added lines, all 31 W3DIAG-marked, 0 unmarked non-blank lines** (verified programmatically). One blank added line was re-marked so the count is airtight; change is comment-only, binary-equivalent, rebuilt after.

## 6. Restore evidence (Step 5)
- Restored via `Copy-Item backups/w3/src_main.c.bak → src/main.c` (no git checkout/clean; no commits/pushes/PRs at any point).
- `status-before-w3.txt` vs `status-after-w3.txt`: **identical** (Compare-Object empty).
- Hashes: current `690F3A79…C7034E10C27A` == backup `690F3A79…C7034E10C27A` → **True**.
- Post-restore grep `W3DIAG` in `src/`: **zero hits**. Grep `scratch` in `src/`: **zero hits**.

## 7. Files changed (net vs pre-task tree)
- None. `src/main.c` byte-restored (hash proof above). Only new files: `log/pico-bcon-w3-crumb.uf2`, workspace artifacts under `bcon-wab/` (`status-before/after-w3.txt`, `diffs/w3-baseline-check.diff`, `w3-main.diff`, `w3-vs-backup.diff`, `backups/w3/src_main.c.bak`, this report).

## 8. Self-review
- Diff-vs-backup shows exactly the 7 specified additions, each line W3DIAG-marked, no logic reorder, no include changes, no other files touched. Post-restore greps prove full removal. Backup/restore discipline followed exactly (UF2 + diffs + byte-restore + hash/status proofs). No subagents used.

## 9. Concerns
1. `scratch[4]` vs SDK: `watchdog_enable()` writes its own marker to scratch[4] (checked by `watchdog_enable_caused_reboot()`). Nobody in `src/` calls that function, and `main.c` uses `watchdog_caused_reboot()` (scratch-independent), so the ev breadcrumb is safe: boot dump reads pre-`watchdog_enable()`, packet_handler writes post-enable. `watchdog_enable_caused_reboot()` would misreport under a W3 build — irrelevant for this diagnostic, but do not reuse scratch[4] in production code.
2. Registration lives in the wireless path only (`wired_loop()` returns never); Death-A test must run the wireless (`WIRED_DEFAULT=0`) build — which is what was built.
3. `w3_irq_tick` runs in alarm-pool IRQ context and does RMW on `scratch[1]`; sole writer is the IRQ itself, and `scratch[]` are plain MMIO words — safe against the wedged loop by design (loop-independent heartbeat is the point).
4. Handoff baseline noise in `w3-main.diff`: reviewers must scope to `w3-vs-backup.diff` / W3DIAG-marked lines.

## 10. How to read the post-mortem (owner guide)
After flashing `log/pico-bcon-w3-crumb.uf2` and reproducing the death, capture the boot banner on UART0. If the previous boot wedged into a WDT reset you will see:
`w3pm irq=<I> poll=<P> stage=<S> ev=0x<EE>` (only printed when `scratch[0]==0xC0DECAFE`, i.e. the previous boot ran the W3 build long enough for one 100 ms IRQ tick).
- **Wedge signatures**: `poll` frozen (not advancing across deaths) + `stage` tells WHERE: `S=1` = wedged after inbox drain (exec_fx/flush/pack/usb/link_poll region); `S=2` = reached `watchdog_update()` area yet WDT still fired (feed starved downstream, e.g. inside `watchdog_update` path or IRQ lockup); `S` stale/0 with `poll` frozen = wedged inside/before the inbox-drain loop or poll_tick never entered. `ev` = last HCI event type before death (correlate with hid-open 0x66/0x6a etc.). `irq` advancing while `poll` frozen = run loop wedged but IRQs alive (classic loop wedge, not a global lockup); `irq` also frozen/low = IRQ-level stall or very early death (check `wdt=` and `core1 boot=` lines).
- **Caveats**: first boot after flashing prints no `w3pm` (fresh scratch); UF2-drag or `watchdog_reboot()` (WIRED_MODE switch path) clears/overwrites scratch — only trust `w3pm` after a genuine WDT death (`wdt=1`). Counters are cumulative since the W3 boot, not per-second — compare across repeated deaths, not absolute values.
