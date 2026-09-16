# W9 brief: phase-gated write windows (resolves U1 write-alone vs write+SNIFF interaction)

Diagnostic UF2s only. Six single-variable variants A..F, each performing exactly ONE forced physical flash write at a specified BT-phase gate. Answers verification-status §3 U1: every death so far has a SNIFF-mode change adjacent to the write — this task separates "write alone kills" from "write+SNIFF kills", with variant E (stable post-handshake idle) as the critical discriminator.

Design precedent: W5 forced-write pattern (generation-numbered host payload so the `store.c:80-81` memcmp dedupe cannot skip it) + W8 Step 1/7/8/9 discipline (backup/restore proof, isolated temp build dirs, UF2 into `log/`, no commits).

## Constraints

- No commits, pushes, PRs, branch ops. `C:\Users\moilo\pico-wakecon` never touched. No secret key bytes in reports/logs/code (peer BD_ADDR OK).
- No SDK/BTstack source edits (instrumentation in our layers only: `src/main.c`, `src/bt/store.c`, `src/bt/store.h` decl only if needed; `CMakeLists.txt` untouched — no new files).
- Isolated temp build dirs (never reuse `build/`; one dir per variant: `$env:TEMP/wab-w9a` … `w9f`). Wireless recipe `-DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0`, no BUMP flag.
- Every added line carries a `W9DIAG` marker. Work from `C:\pico-bcon`. BASE `c366bf4`; tree holds uncommitted Phase-3 work — preserve it.
- Restore proof required per variant build (hashes equal, marker grep zero). NEVER use git checkout/restore/clean.
- Report path: `C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\task-9-report.md`.

## Shared diagnostic core (identical in all six UF2s except the gate)

- [ ] **Forced-write bypass (diag-only, W5 precedent).** In `src/bt/store.c`, add a `W9DIAG`-marked `store_host_force`-style function used ONLY by the diagnostic gate: same body as `store_host()` (`store.c:76-98`) but (a) SKIPS the `probe_host_known && memcmp==0` early-return at lines 80-82, and (b) appends/increments a generation byte in the stored payload (e.g. addr[5] XOR gen, or 7-byte payload addr+gen) so the TLV layer always sees a changed value and performs a physical write. Normal `store_host()` call in `handle_hid_meta` (`main.c:702`) stays untouched. Gate call sites invoke ONLY the force variant, exactly once per boot (one-shot flag, never re-armed).
- [ ] **Write proof (minimum).** W8-style counters or minimum: `safe begin/end + rc + dur_us` around `tag_store_safe`/`tag_store_fn` (same choke points as W7/W8, `store.c:40-44,58-72`) plus a boot dump line (via `probe_line`, boot context) printing write count + tag + gen + rc + dur. A session counts as valid ONLY if the dump/proof shows the forced write physically ran (`rc=0`, `dur≈2ms` per V3).
- [ ] **SNIFF-proximity logging (all variants).** hci_dump ON (same 6 lines as W6/W7/W8) + a `W9DIAG`-only `HCI_EVENT_MODE_CHANGE` case in `packet_handler` (`main.c:754-818`, next to the existing `HCI_EVENT_ENCRYPTION_CHANGE` case at 808-815) printing `modechg mode=<m> interval=<i> t=<ms>` via `probe_line`. Reading guide must tabulate write→nearest-MODE_CHANGE Δms (before/after) for every session — this adjacency table IS the U1 evidence.

## Variants (separate UF2s, single-variable: only the gate differs)

| Var | Gate condition in code terms (one-shot, calls `store_host_force` once) | Expected log signature | Death/survival decision rule |
|-----|---|---|---|
| A unconnected idle | Timer/context with NO connection yet: fire `store_host_force` from `usb_handler`/`poll_tick` path (`main.c:602-606/459`) guarded by `probe_hid_cid==0 && probe_connected_at_ms==0` (no `CONNECTION_COMPLETE` seen). Peer never paged. | `w9fire var=A gen=<g>` + write proof, no conn/disc lines within ±10s | DEATH here ⇒ write-alone kills (SNIFF not required). SURVIVAL ⇒ proceed to B–F. |
| B ACL connected | Immediately after `handle_conn_complete` success (`main.c:641-655`, `cst==0`): set one-shot pending flag there, execute the single forced write on the next `poll_tick`. `probe_hid_cid` still 0, no encryption yet. | `conn status=0x00 ok` → `w9fire var=B` → write proof, pre-encryption | DEATH here, A alive ⇒ ACL context required. SURVIVAL ⇒ proceed. |
| C encrypted | Immediately after `HCI_EVENT_ENCRYPTION_CHANGE` with `en==1` (`main.c:808-815`): one-shot forced write on next `poll_tick`. | `encrypt change ... en=1` → `w9fire var=C` → write proof | DEATH here, B alive ⇒ encryption-gated interaction. |
| D just-after-open | Immediately after `HID_SUBEVENT_CONNECTION_OPENED` success (`main.c:681-708`, after the existing `store_host(a)` at 702 + `link_mark_connected` at 707): one-shot forced write. This is the historical death zone (host-save at open). | `hid open. host ... saved` → `w9fire var=D` → write proof | Expected DEATH (positive control, replicates 9/9 signature). SURVIVAL ⇒ flag as anomaly, re-run before trusting E/F. |
| E stable post-handshake idle (**critical**) | D-gate PLUS stability delay: same open trigger, but defer the one-shot write until `now - open_ms >= 15000` AND `probe_ssp_count` settled AND at least one MODE_CHANGE already logged (SNIFF entered, link idle-stable). Implement as timestamp captured at open + condition checked in `poll_tick`. | `hid open` … ≥15s of idle (≥1 `modechg` line, no SSP/disc) … `w9fire var=E` → write proof | DEATH here with SNIFF already settled ⇒ write-alone kills even at stable idle (U1 resolved toward single-cause). SURVIVAL here with D dead ⇒ killing factor is the open-handshake window (SNIFF transition proximity), not the write per se — U1 resolved toward interaction. |
| F after disconnect | After `handle_disc` (`main.c:657-675`) or `HID_SUBEVENT_CONNECTION_CLOSED` (`main.c:710-714`): one-shot forced write on next `poll_tick` with `probe_hid_cid==0`. Radio up, no link. | `disc reason=...`/`hid closed` → `reconnect armed` → `w9fire var=F` → write proof | DEATH here, A alive ⇒ disconnect-teardown state matters. SURVIVAL ⇒ write needs a live link. |

- [ ] Variants run as separate UF2s with full restore between (single-variable): rebuild each variant from the same Step-1 baseline with ONLY the gate hunk differing; between HW sessions power-cycle the Pico and leave host/pairing state untouched unless the reading guide says otherwise (record any re-pair explicitly — it is a variable change).

## Steps

- [ ] **Step 1: Record baseline and back up**

```powershell
git status --porcelain | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\status-before-w9.txt
Copy-Item src/main.c C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\backups\w9\src_main.c.bak
Copy-Item src/bt/store.c C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\backups\w9\store.c.bak
Copy-Item src/bt/store.h C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\backups\w9\store.h.bak
```

Verify pre-state: marker grep `W7DIAG|W8DIAG|W9DIAG|W10DIAG|diag_rec|scratch` over `src/` returns zero hits (W8 restored). No new files in this task.

- [ ] **Step 2–6: Implement shared core + ONE variant gate at a time** (shared core identical; only the gate hunk differs per variant; one-shot flag; no logic restructuring outside the gate).
- [ ] **Step 7: Compile each variant in its isolated dir**

```powershell
cmake -S . -B $env:TEMP/wab-w9a -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0
cmake --build $env:TEMP/wab-w9a --target pico-bcon
# repeat with wab-w9b … wab-w9f for variants B..F
```

Expected: exit 0 each. Never reuse `build/`.

- [ ] **Step 8: Save artifacts**

```powershell
Copy-Item $env:TEMP/wab-w9a/pico-bcon.uf2 log/pico-bcon-w9-a.uf2
# … -b, -c, -d, -e, -f
git diff -- src/main.c src/bt/store.c src/bt/store.h | Out-File C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\diffs\w9-<var>-tracked.diff
```

All UF2s non-empty. Record size + SHA256 per variant.

- [ ] **Step 9: Restore and prove** (after ALL variant builds): restore the 3 backed-up files via `Copy-Item` (no git checkout/restore/clean); `status-after-w9.txt` identical to `status-before-w9.txt` (`Compare-Object` empty); file hashes equal to Step-1 baseline; marker grep `W9DIAG` over `src/` zero hits (others must be zero too).
- [ ] **Step 10: HW run + report** — write `task-9-report.md`: per-variant implementation, build exit codes, UF2 paths+sizes+SHAs, per-session table (var, gen, write proof rc/dur, nearest-MODE_CHANGE Δms, death/survival + time-to-WDT signature ≈2.0s yes/no), U1 verdict (write-alone vs interaction, with E as the deciding row), restore evidence, self-review, concerns.

## NO-GO gate

STOP and report BLOCKED without building if any of these cannot be implemented without restructuring: (a) the generation-numbered force bypass cannot guarantee a physical write without touching TLV/SDK internals; (b) any gate condition (esp. E's stable-idle detector) requires new cross-module state or BT-callback restructuring beyond a timestamp+flag; (c) MODE_CHANGE cannot be logged from our `packet_handler` layer. Report which gate failed and the minimal restructure it would need.

DO NOT commit.
