# W7 brief: Unified diagnostic instrumentation (counters + fine stages + timer accounting)

ONE diagnostic UF2 combining: flash-op counters, fine-grained poll/usb stages, and our-timer add/remove/fire accounting, plus hci_dump ON (comparability; proven inert). Then revert fully. W6 UF2 already serves as the hci_dump-only control — do NOT rebuild it.

## Steps

- [ ] **Step 1: Record baseline and back up**

```powershell
git status --porcelain | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\status-before-w7.txt
Copy-Item src/main.c C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\backups\w7\src_main.c.bak
Copy-Item src/bt/store.c C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\backups\w7\store.c.bak
Copy-Item src/bt/store.h C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\backups\w7\store.h.bak
Copy-Item CMakeLists.txt C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\backups\w7\CMakeLists.txt.bak
```

(New files created below are listed explicitly for deletion at restore; NEVER use git clean.)
Work from: C:\pico-bcon.

- [ ] **Step 2: Scratch ownership map FIRST (gate)**

Grep all `scratch` uses in `src/` AND read the SDK's watchdog_hw struct + any SDK code that writes scratch (search the SDK for `scratch\[` — report which indices the SDK reserves; W3 already learned `scratch[4]` collides). Produce a written map, e.g. `[0]=magic/version, [1]=recorder index + CRC, [2]=critical stage mirror`, all disjoint from SDK use. If fewer than 3 free regs exist, STOP and report BLOCKED with the map (do not overlap SDK).

- [ ] **Step 3: RAM flight recorder (new files, W7DIAG-marked, deleted at restore)**

Create `src/diag_rec.c` + `src/diag_rec.h` (names must not collide; check first) with:
- `__attribute__((section(".uninitialized_data")))` ring buffer (verify this section exists in the build — check the link map or Pico SDK docs in-tree; if unavailable, STOP and report BLOCKED with what you found).
- Record header: magic `0xD1A6nnnn`, version, write index, CRC over header+committed entries.
- Entry kinds (small fixed struct: timestamp_us + kind + arg1 + arg2): FLASH_OP (begin/end, op type, rc, duration), STAGE (poll/usb stages below), TIMER (add/remove/fire per timer id + timeout ms), EV (HCI event byte, replaces W3's ev).
- Rule: Core0-only writes to the ring EXCEPT the IRQ tick may bump a single scratch-resident counter (never the ring). No printf in recorder code. No flash writes from instrumentation, ever. No heavy mutex (single-writer discipline documented in a comment).

- [ ] **Step 4: Flash-op counters (no SDK edits — our layers only)**

(a) In `src/bt/store.c` `tag_store_safe`/`tag_delete_fn` (the single choke points for OUR tag ops): count safe_execute calls, program calls (store_tag), delete calls (delete_tag), record last op/begin/end/rc into the recorder. Do not change error semantics.
(b) Link-key-db shim: at the `hci_set_link_key_db(...)` call site, interpose our own `btstack_link_key_db_t` struct that forwards every method to the TLV instance but counts `put_link_key`/`delete_link_key` calls (+timestamps) into the recorder. Static storage, exact method signatures from the bundled BTstack headers (read them first).
(c) Ruling carried from controller: SDK-internal erase/program granularity is OUT of scope (SDK sources live outside this repo and are never edited); wrapper + shim coverage observes 100% of initiated ops. State this boundary in the report.

- [ ] **Step 5: Fine stages + usb_handler stages**

In `poll_tick`: POLL_ENTER (first line), POLL_BODY_DONE (after inbox drain), WDT_UPDATE_DONE (after `watchdog_update()`), POLL_TAIL_DONE + POLL_RETURN_IMMINENT (last lines). In `usb_handler` (read it first — it calls `poll_tick` then re-arms): USB_HANDLER_ENTER, POLL_RETURNED (right after the poll call returns), SET_TIMER_DONE, ADD_TIMER_DONE, USB_HANDLER_RETURN_IMMINENT. Record each into the recorder (+ critical stage mirrored to scratch). This separates "poll never returned" / "returned but set missed" / "set done but add missed" / "added but never fired".

- [ ] **Step 6: Our-timer accounting (no BTstack list walk — no public API, no SDK edits)**

For `usb_timer` (load-bearing feed timer) and `empty_timer`/`stats_timer`: count add/remove/fire events + last timeout value into the recorder. Implement with minimal W7DIAG-marked lines at the existing set/add call sites and handler entries (do NOT restructure timer logic). This answers "was usb_timer present and firing" without touching BTstack internals. (Controller ruling: list-walk replaced by owned-timer accounting.)

- [ ] **Step 7: hci_dump ON + boot dump**

Re-add the 6 hci_dump lines exactly as in the W6 brief/diff (`..\bcon-wab\diffs\` W6 artifacts + `briefs\task-6-brief.md` — read them). Add a W7DIAG boot-dump block after the `wdt=` banner line: if recorder magic+CRC valid, print summary (op counts, last stage chain, last timer events, last ev) via probe_line (boot context only, never IRQ).

- [ ] **Step 8: Compile the wireless build**

```powershell
cmake -S . -B $env:TEMP/wab-w7 -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0
cmake --build $env:TEMP/wab-w7 --target pico-bcon
```

CMakeLists must pick up the new files ONLY if the build globs sources — check first: if sources are listed explicitly, add the new file (that CMakeLists hunk is part of the change, W7DIAG-comment it); if globbed, no edit. SDK full paths if needed. Never reuse `build/`. Allow >= 600000 ms. Expected: exit 0, no BUMP flag.

- [ ] **Step 9: Save artifacts**

```powershell
Copy-Item $env:TEMP/wab-w7/pico-bcon.uf2 log/pico-bcon-w7-diag.uf2
git diff -- src/main.c src/bt/store.c src/bt/store.h CMakeLists.txt | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\diffs\w7-tracked.diff
```

New files (`src/diag_rec.c/.h`) are untracked: copy them to `diffs/w7-diag_rec.c.txt`, `w7-diag_rec.h.txt` as the review surface. All artifacts non-empty.

- [ ] **Step 10: Restore and prove**

Restore the 3 backed-up files from backups; DELETE exactly the 2 created files (`src/diag_rec.c`, `src/diag_rec.h`) plus revert any CMakeLists hunk via backup (back up CMakeLists.txt in Step 1 too — add it). Status-after identical to status-before; hashes True for all restored files; `Get-ChildItem` proves the 2 files are gone; grep `W7DIAG|diag_rec|scratch` in src/ → zero hits.

- [ ] **Step 11: Report** — write `C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\task-7-report.md` (scratch map + SDK reservation evidence, per-site implementation, build + exit code, UF2 path + size, artifact inventory, restore evidence, self-review, concerns) PLUS the reading guide: which recorder fields answer (i) physical-flash-zero proof for Session-1-type deaths, (ii) poll-vs-rearm localization, (iii) usb_timer presence/firing, (iv) T3-delete physical-op question. DO NOT commit.
