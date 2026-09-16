# Quiesced flash worker — permanent-fix design (pico-bcon)

- Status: design only. No source edited, no build run, no commit.
- SSOT: `spec/protocol_v3.md` (`PROTO_VER=3`). Spec + `src/proto/*` win over docs/history.
- Scope: Pico 2 W + BTstack Classic + Switch Pro Controller emulation, `src/main.c` + `src/bt/store.c` + `src/proto/dispatch.h`.
- Reference-only: `C:\Users\moilo\pico-wakecon` was not touched.
- Secrets: this doc contains no key bytes. Logging rules below forbid them permanently.

## 0. Established facts (do not re-litigate)

1. Flash WRITE while BT is active kills the run-loop ~2.0 s later, 9+/9+ sessions.
   Program completes in ~2 ms, then the scheduler dies between `ADD_TIMER_DONE`
   and the next `FIRE`. No-write sessions survive (~14+).
   A second coexisting signature (op-in-death, branch (b) decided) hangs INSIDE
   the store op before the first program: stage trail stops at S3 (entered
   callback, before `tlv->store_tag`'s first program), bank CRCs fully unchanged
   (`chg0=chg1=0`, `write_offset` unmoved), host stays 0, WDT fires ~2 s later.
   Prime suspect for op-in-death is a CPU-side infinite scan loop over a
   tombstoned TLV bank (first-touch-after-tombstoning pattern: same-bank first
   store hangs, second store completes as Death-B or survives). The quiesced
   worker (§4) stops the TRIGGER of both signatures but leaves an already
   corrupt bank (the landmine) in place — quiesce alone is necessary but NOT
   sufficient. The permanent fix is hemostasis (§4.1–§4.8) PLUS the confirmed
   additions (§4.9); neither part is sufficient on its own.
2. Current flash writers (all via `flash_safe_execute`, Core1 lockout proven, `victim=1`):
   - `store_host` — HID open handler (`src/main.c` `handle_hid_meta`,
     `HID_SUBEVENT_CONNECTION_OPENED`), deduped (T2: early-return when
     `probe_host_known && memcmp == 0`).
   - `FX_KEY_DELETE` — `exec_fx` (`src/main.c`): `gap_delete_all_link_keys()` +
     `store_host_forget()`.
   - `FX_COLOR_SET` — `exec_fx`: `memcpy(spi_color_6050, ...)` + `store_color()`.
   - `FX_WIRED_MODE` — `exec_fx`: `store_wired(w)` then self-reboot in ~500 ms
     (`s_reboot_at`, `watchdog_reboot`).
   - `store_cap_save` — capture path (`src/bt/link_cap.c` `link_cap_tick`, on
     `CAP-DONE` when `best >= 0`).
   - Two further write paths that must be in scope although not in the original
     one-line list:
     - `store_cap_forget` — `link_cap_clear()` (`TAG_CAP` delete). A flash op.
     - `gap_delete_all_link_keys()` on auth-fail (`src/main.c` `packet_handler`,
       `HCI_EVENT_AUTHENTICATION_COMPLETE` `status != 0`). This mutates the
       BTstack link-key DB (TLV) from inside an HCI event callback while BT is
       active — same hazard class.
3. Switch uses non-bonding auth (`AuthReq=0x00`), so fresh link keys are never
   persisted by BTstack in the normal path. The only link-key DB writes are the
   two explicit delete paths above.
4. External-review safety conditions for any worker (stage 1 gate, all required):
   `ACL==0 AND HID disconnected AND no outgoing pending AND no pairing in
   progress AND no L2CAP signaling pending AND no TX pending`.
5. Stage 2 (`BT-stop -> save -> resume`) is future work. This design must not
   preclude it, but must not implement it.
6. WDT is fed from the 1 ms `usb_timer` at the end of `poll_tick`
   (`watchdog_update()` is the tail of `poll_tick`; `usb_handler` re-arms 1 ms).
   Any worker runs in that context and must never block the feed path. All flash
   ops stay inside `flash_safe_execute`.

## 1. Goal and non-goals

### Goal

Eliminate the "flash-write-while-BT-active → scheduler death ~2 s later" failure
by removing **all** synchronous TLV writes from BT-active contexts and replacing
them with a single deferred worker that runs **only** when the quiesce gate
(§4.1) passes, in `poll_tick` context (Core0, `usb_timer`), still inside
`flash_safe_execute`, still feeding the WDT every tick. This hemostasis stops
the trigger (Death-B post-completion death and the op-in trigger) but does NOT
repair a bank already corrupted by tombstoning — the landmine survives quiesce.
Permanent success additionally requires §4.9 (integrity check + scan cap +
reformat path); any claim below that quiesce alone "eliminates" the failure is
read as "eliminates the trigger pending §4.9".

Success criteria:

- Zero TLV `store_tag` / `delete_tag` calls while the gate is closed, by
  construction (single choke point). Necessary but not sufficient for the
  op-in signature: the first quiesced flush onto a tombstoned bank still walks
  the scan path, so this criterion is met only jointly with the §4.9 scan cap
  (hang degrades to an error) and the §4.9 integrity/reformat path (landmine
  removed at boot).
- No-write-equivalent survival for sessions that previously died (target: match
  the ~14+ no-write survival baseline, then extend with soak).
- No user-visible protocol change: `spec/protocol_v3.md` untouched,
  `dispatch.h` `FX_*` enum untouched in stage 1.
- `tests/host` stays green without modification (or with only additive tests).
- Power loss at any point leaves a bootable image with either the old or the new
  tag value, never a bricked TLV bank (BTstack flash-bank TLV already guarantees
  bank-swap atomicity per tag; worker must preserve that property by keeping one
  `store_tag` per tag per run).

### Non-goals

- Stage 2 (`hci_power_control(OFF) -> save -> ON -> resume`): explicitly out.
  The worker call site and state machine reserve hooks for it (§4.8) but do not
  implement power cycling, re-enumeration, or reconnect orchestration.
- No change to wire protocol, frame layout, SEQ/CRC, STATUS bits, `WIRED_MODE`
  reboot semantics (~500 ms), or TLV tag IDs / namespaces (`BCHO`/`BCCL`/
  `BCW1`/`BCWR`; wakecon `NXxx` separation stays).
- No change to bonding behavior, SSP parameters, or Switch non-bonding auth.
- No change to Core1 (UART1 DMA ring + parser + mutex push only). Core1 never
  gains flash/BT/USB calls.
- No new flash write paths (e.g. persisting link keys, persisting stats).
- No bundling of the WDT composite-heartbeat + scratch-breadcrumb
  instrumentation into the worker approval (§6 is a separate optional proposal).

## 2. Current write paths (exact locations)

| # | Call site today | Tag / DB | Context today | Hazard |
|---|---|---|---|---|
| W1 | `handle_hid_meta` → `store_host(a)` (`src/main.c`, `HID_SUBEVENT_CONNECTION_OPENED`) | `TAG_HOST` (`BCHO`), 6 B | HCI/HID event callback, HID just connected (`probe_hid_cid != 0`) | Direct hit: ACL>0, HID up, TX pending likely |
| W2 | `exec_fx` `FX_COLOR_SET` → `store_color()` (`src/main.c`) | `TAG_COLOR` (`BCCL`), 13 B | `poll_tick` (`usb_timer` 1 ms), BT may be fully active | Direct hit whenever Switch connected |
| W3 | `exec_fx` `FX_KEY_DELETE` → `gap_delete_all_link_keys()` + `store_host_forget()` | link-key DB + `TAG_HOST` delete | `poll_tick`, BT may be active | Double hit (DB + tag) |
| W4 | `exec_fx` `FX_WIRED_MODE` → `store_wired(w)` then `s_reboot_at = now+500` | `TAG_WIRED` (`BCWR`), 1 B | `poll_tick`, BT may be active | Special: reboot in 500 ms preempts the ~2 s death window, which is why this path has historically survived; still must be quiesced or made reboot-safe (§4.3) |
| W5 | `link_cap_tick` → `store_cap_save()` (`src/bt/link_cap.c`, `CAP-DONE best>=0`) | `TAG_CAP` (`BCW1`), `CAP_BLOB_SIZE` | `link_poll` from `poll_tick`; scan just stopped, BT working, possibly reconnect imminent | Hit: BT working, LE/Classic state churning |
| W6 | `link_cap_clear` → `store_cap_forget()` | `TAG_CAP` delete | Caller-dependent (currently synchronous) | Same class as W5 |
| W7 | `packet_handler` auth-fail → `gap_delete_all_link_keys()` (`src/main.c`, `HCI_EVENT_AUTHENTICATION_COMPLETE`) | link-key DB | HCI event callback, pairing just failed, ACL may still exist | Hit: must be deferred like W3 |

Reads (`store_*_load`, `link_key_count`) are XIP direct reads before Core1 launch
(`src/main.c` `main`, before `multicore_launch_core1`) and stay as-is.

## 3. Options compared

All three options keep `flash_safe_execute` (Core1 lockout) and keep Core1
untouched. They differ in **when** the write happens and **how many** writes one
quiesced window performs.

### Option A — dirty-flag + quiesced single worker in `poll_tick` context (recommended)

- `exec_fx` / HID handler / capture completion stop calling `store_*` /
  `gap_delete_all_link_keys` directly. They instead (a) update the RAM shadow
  (`spi_color_6050`, `probe_host_*`, `probe_cap_saved`, `s_wired`) and (b) set a
  dirty bit in a single `uint32_t` bitmap (`DIRTY_HOST | DIRTY_COLOR | DIRTY_CAP |
  DIRTY_WIRED | DIRTY_KEYS_FORGET | DIRTY_HOST_FORGET | DIRTY_CAP_FORGET`).
- Once per `poll_tick`, after `exec_fx` + `flush_outbox` and before
  `watchdog_update()`, evaluate the quiesce gate (§4.1). If open **and**
  dirty != 0, run `flash_worker_run()` once: drain the bitmap in fixed order
  (§4.3), at most one full drain per tick, bounded time, then fall through to
  `watchdog_update()`. If gate closed, do nothing (dirty persists in RAM).
- Wired boot (`wired_loop`) has no BTstack/CYW43; gate is trivially open there,
  so the same worker can flush immediately — no special case needed beyond a
  `s_bt_init == false → quiesced` short-circuit.

Trade-offs:

- Power-fail safety: dirty lives in RAM; sudden loss before flush keeps the
  **old** persisted value. That is the safe direction (previous behavior on
  Switch: re-pair / re-set color / re-capture; never a brick). No torn write:
  one `store_tag` per tag per run preserves BTstack bank atomicity.
- Aggregation: excellent. All pending tags coalesce into **one** gate opening
  (§4.2). COLOR_SET storms collapse to last-wins (one 13 B write). HOST saves
  collapse with the existing T2 dedupe re-checked at flush time. Worst case is
  one drain per quiesce window, not one flash op per event.
- UX delay: writes become eventually-consistent. COLOR_SET ACK/outbox flush is
  immediate (RAM shadow + log), persistence follows at next quiesce (typically
  HID disconnect / idle). Acceptable: Switch reads color only at first connect
  (cached; re-pair needed anyway per spec §12), host tag only matters for
  reconnect, cap blob only matters for next BEACON. `WIRED_MODE` keeps its
  500 ms reboot deadline via the §4.3 deadline rule.
- Code size: small (~150–250 lines: bitmap, gate query, worker drain, logging,
  deadline handling). One choke point; easy to audit that no other TU calls
  `tag_store_safe` / `delete_tag` / `gap_delete_all_link_keys` directly.
- Risk: low for the hemostasis itself. `poll_tick` ordering is explicit; WDT feed stays last; worker is
  non-blocking and time-bounded. Main risk is "gate never opens" (always
  connected) — mitigated by §4.5 retry/backoff + STATUS visibility, and by stage
  2 later. Residual risk explicitly NOT covered here: a bank already corrupted
  by tombstoning still hangs the first quiesced flush's pre-write scan
  (branch (b) op-in signature) — that landmine is §4.9's scope (integrity
  check + scan cap + reformat), without which hemostasis alone is insufficient.

### Option B — defer-to-reboot (write on next clean boot before radio up)

- Never write at runtime. Dirty bitmap persists **only as intent**; the actual
  `store_tag` happens on the next boot in the existing window between TLV init
  and radio up (`src/main.c` `main`: after `store_*_load`, before
  `cyw43_arch_init` / `hci_power_control`), or in `wired_loop` before USB.
- Since RAM does not survive reboot, intent needs its own persistence — which is
  itself a flash write, recursing into the same hazard. Practical variants are
  (B1) write intent to a scratch flash sector outside TLV (new wear + new
  failure modes), or (B2) piggyback on the `WIRED_MODE` reboot only (all other
  tags wait for an unrelated reboot that may never come).

Trade-offs:

- Power-fail safety: B2 loses acknowledged writes on power loss (user set color,
  got ACK, unplugged, color gone — worse than A). B1 adds a second flash
  subsystem to audit.
- Aggregation (link-key vs host-tag vs color-tag): poor. B2 couples unrelated
  tags to the WIRED reboot cadence; link-key forget (security-sensitive,
  user expects immediate effect on `KEY_DELETE`) would linger until a reboot the
  user never asked for. Host-tag vs color-tag vs cap-tag have different
  lifetimes; forcing them through one reboot funnel conflates them.
- UX delay: worst. `KEY_DELETE` must take effect logically at once (it does:
  RAM + `gap_delete_all_link_keys` logical effect can be immediate) but
  persistence is unbounded-delay. Users cannot trust "keys deleted" across a
  power cut.
- Code size: medium (boot-window sequencer + intent encoding), but B1's scratch
  sector roughly doubles the flash code to review.
- Risk: medium-high. Boot path is the least-tested-for-writes path; a bug there
  bricks boot rather than killing a session. Also collides with the
  `store_wired_load_def` / `link_apply_wired_mode` ordering that is already
  subtle.

Verdict: reject as the primary mechanism. B remains the correct **fallback for
the WIRED tag only** (reboot preempts death), not the general worker.

### Option C — idle-window writer with the safety-condition gate evaluated per-tick, one tag per tick

- Same gate as A, evaluated per-tick, but the writer flushes **at most one tag
  per tick** (round-robin or priority order), spreading a multi-tag backlog over
  several ticks/windows instead of draining in one run.
- Motivation would be bounding single-tick latency (each `store_tag` is
  ~1–2 ms erase/program inside `flash_safe_execute` with Core1 locked out).

Trade-offs:

- Power-fail safety: equivalent to A per tag, but multi-tag backlogs have a
  wider torn-set window (e.g. `KEY_DELETE`'s key-DB delete lands ticks before
  its host-tag delete; power loss between them leaves a half-applied delete).
  A closes that window to one drain.
- Aggregation: worse than A. N dirty tags cost N gate openings instead of one.
  Gate openings are the scarce resource (they require ACL==0 + HID down +
  quiet); spending one per tag multiplies exposure to gate flapping (reconnect
  fires between tag 1 and tag 2, stranding the rest).
- UX delay: slightly worse than A for backlogs (N windows instead of 1); equal
  for single-tag dirties (the common case).
- Code size: similar to A plus a cursor/continuation state machine — marginally
  larger and harder to prove "exactly one flash op per tick".
- Risk: medium. Interleaving reconnect/page activity between per-tag ticks is
  precisely the hazard we are avoiding; A minimizes time-spent-near-flash while
  the radio picture can change.

Verdict: reject in favor of A. If single-drain latency ever proves too high
(measure first), the correct fix is to cap A at a time budget and resume next
window **with the §4.3 order preserved**, not to default to one-tag-per-tick.

### Comparison matrix

| Dimension | A: quiesced single worker (drain) | B: defer-to-reboot | C: one-tag-per-tick |
|---|---|---|---|
| Flash ops while gate closed | 0 (choke point) | 0 at runtime | 0 while closed |
| Gate openings per backlog | 1 | 0 (uses boot) | N (one per tag) |
| Power-loss semantics | old value kept, bootable | B2: ACKed write lost; B1: second flash subsystem | torn set across ticks possible |
| Link-key forget latency | logical immediate, persisted at quiesce | unbounded (bad for security UX) | persisted over N windows |
| Host vs color vs cap aggregation | coalesced, last-wins | funneled through unrelated reboot | spread, flapping-prone |
| UX delay (common single-tag) | next idle/disconnect | next reboot (possibly never) | next idle/disconnect |
| `WIRED_MODE` 500 ms deadline | handled by deadline rule (§4.3) | natural fit (only tag B suits) | same as A but split |
| Code size / audit surface | small, one drain fn | medium + scratch (B1) | small + cursor state |
| WDT risk | bounded drain before `watchdog_update` | boot-path risk (worse) | smallest per-tick, but more windows |
| Stage-2 preclusion | none (hook reserved) | complicates (boot sequencer owns writes) | none, but state machine harder to reuse |

## 4. Recommended design (Option A) — specification

### 4.1 Quiesce gate (all conditions required, evaluated per `poll_tick`)

```c
bool flash_worker_quiesced(void);   // new, lives near link layer (not in dispatch)
```

Gate opens iff **all** hold (external-review conditions, mapped to concrete
signals):

1. `ACL == 0` — no Classic/LE connection handles outstanding. Source:
   BTstack connection state tracked in `link_conn.c` (add an explicit counter
   incremented on `CONNECTION_COMPLETE` success / `LE_CONNECTION_COMPLETE` and
   decremented on `DISCONNECTION_COMPLETE`; do not infer from `probe_hid_cid`
   alone — HID CID is necessary but not sufficient).
2. `HID disconnected` — `probe_hid_cid == 0` (`src/bt/hid.c`).
3. `No outgoing pending` — `!probe_outgoing_tried && !probe_reconnect_pending &&
   outgoing_at_ms == 0` **and** the `reconnect_timer` is not due this tick
   (re-arm check). Covers `hid_device_connect` in flight from
   `link_reconnect_handler`.
4. `No pairing in progress` — no open SSP/auth sequence: track a
   `pairing_active` flag set on `HCI_EVENT_CONNECTION_REQUEST` /
   SSP events (`0x31/0x32/0x33`) / `HCI_EVENT_LINK_KEY_REQUEST` and cleared on
   `HCI_EVENT_AUTHENTICATION_COMPLETE` / `HCI_EVENT_ENCRYPTION_CHANGE` /
   `DISCONNECTION_COMPLETE`. (`probe_ssp_count` alone is a counter, not a state;
   add the boolean.)
5. `No L2CAP signaling pending` — no open L2CAP connect/config for HID Control /
   Interrupt outstanding (track via HID library state or L2CAP event callbacks;
   conservative default: treat any non-zero `probe_hid_cid` **or** any ACL as
   closed, plus an explicit `l2cap_signal_pending` flag if the BTstack version
   exposes one — never assume quiet from CID alone).
6. `No TX pending` — `!probe_send_now_wanted` and no `CAN_SEND_NOW` owed
   (`src/bt/hid.c` `probe_request_send` / `probe_can_send_now` accounting), and
   `empty_timer` not coalesced into an immediate send this tick.

Short-circuits (gate trivially open):

- Wired boot (`s_bt_init == false`, `wired_loop`): no BTstack/CYW43 — worker may
  flush immediately. This also covers factory-first-boot and the post-`WIRED=1`
  reboot before radio ever comes up.
- `link_bt_working() == false && ACL == 0 && probe_hid_cid == 0` during beacon
  warmup races: still evaluate all six; the short-circuit is only the wired
  case, never "BT working but idle-looking".

The gate function is **pure query**: no side effects, no logging on the hot
path (log only on closed→open / open→closed transitions at `probe_line` level,
rate-limited to the 1 s `stats_timer` cadence, not per-tick).

### 4.2 Aggregation (which tags coalesce into one worker run)

Single `uint32_t` dirty bitmap, owned by Core0 (`src/main.c` static), set at the
current synchronous call sites, cleared bit-by-bit only by the worker after a
verified `rc == 0`:

| Bit | Meaning | Set at | Payload source at flush |
|---|---|---|---|
| `DIRTY_HOST` | `TAG_HOST` save pending | HID open handler (replaces direct `store_host`) + reconnect-learned addr | RAM `pending_host_addr[6]` snapshot taken at set time; re-validate T2 dedupe at flush (if `probe_host_known && memcmp == 0`, clear without writing) |
| `DIRTY_HOST_FORGET` | `TAG_HOST` delete pending | `FX_KEY_DELETE` | — (delete) |
| `DIRTY_KEYS_FORGET` | link-key DB delete pending | `FX_KEY_DELETE` + auth-fail path (replaces both direct `gap_delete_all_link_keys` calls) | — (logical disconnect already done; DB delete at flush) |
| `DIRTY_COLOR` | `TAG_COLOR` save pending | `FX_COLOR_SET` (keeps `memcpy(spi_color_6050, ...)` immediate) | `spi_color_6050` live value at flush (last-wins across storms) |
| `DIRTY_CAP` | `TAG_CAP` save pending | `link_cap_tick` `CAP-DONE best>=0` (replaces direct `store_cap_save`) | `probe_cap_saved` snapshot; re-encode at flush (`cap_encode` then `store_tag`) |
| `DIRTY_CAP_FORGET` | `TAG_CAP` delete pending | `link_cap_clear` (replaces direct `store_cap_forget`) | — (delete) |
| `DIRTY_WIRED` | `TAG_WIRED` save pending | `FX_WIRED_MODE` on change (replaces direct `store_wired`) | `s_wired` live value at flush |

Coalescing rules:

- Repeated `COLOR_SET` before flush: single `DIRTY_COLOR`, last RAM value wins.
  (`probe_color_set_count++` still increments at accept time for diagnostics.)
- `HOST` save + `HOST_FORGET` both set (open then `KEY_DELETE` before quiesce):
  forget wins; worker order (§4.3) applies delete last-word-wins and skips the
  redundant save (clear `DIRTY_HOST` without writing, count as coalesced).
- `CAP` save + `CAP_FORGET` both set: forget wins, same skip-and-count rule.
- `KEYS_FORGET` is idempotent (`gap_delete_all_link_keys` on empty DB is a
  no-op); repeated sets collapse to one.
- `WIRED` never coalesces away a change: `s_wired` is the single source of
  truth; intermediate flaps (0→1→0 before flush) correctly persist only the
  final value, and the reboot arming follows the final value (§4.3).

### 4.3 Ordering guarantees and the WIRED deadline

Fixed drain order per worker run (deletes and security first, mode last):

1. `DIRTY_KEYS_FORGET` (`gap_delete_all_link_keys` inside `flash_safe_execute`
   context discipline — see note below).
2. `DIRTY_HOST_FORGET` (`delete_tag(TAG_HOST)`).
3. `DIRTY_CAP_FORGET` (`delete_tag(TAG_CAP)`).
4. `DIRTY_HOST` (`store_tag(TAG_HOST)` unless T2-dedupe or forget-wins skips).
5. `DIRTY_CAP` (`store_tag(TAG_CAP)` unless forget-wins skips).
6. `DIRTY_COLOR` (`store_tag(TAG_COLOR)`).
7. `DIRTY_WIRED` (`store_tag(TAG_WIRED)`) — **always last**.

Rationale: security deletes (`KEY_DELETE`) must not be reorderable behind a
color/cap save that could strand them across a power cut; `WIRED` last because
it arms/approves the reboot and must reflect the final post-delete state.

`gap_delete_all_link_keys` note: it is a BTstack/HCI-layer call, not a raw
`store_tag`. Deferring it to the quiesced worker is the point (today's auth-fail
and `KEY_DELETE` paths call it while BT is active). At flush time the gate
guarantees ACL==0/HID-down, which is the safe context for it. The logical
effect users expect immediately (link torn down, reconnect stopped) is still
applied synchronously via the existing `link_note_disconnected` /
`hid_device_disconnect` / `probe_outgoing_tried = false` paths — only the
**DB mutation** is deferred. The worker logs both halves separately.

`WIRED_MODE` deadline rule (the one tag with a timer):

- `FX_WIRED_MODE` on change sets `DIRTY_WIRED`, updates `s_wired` + USB/HID
  posture (`usb_wired_set_enabled`, `link_apply_wired_mode`) exactly as today,
  and arms `s_reboot_at = now + 500` as today.
- The worker prioritizes `DIRTY_WIRED`: if `DIRTY_WIRED` is set, the worker runs
  at the first gate opening **even if other bits are also set** (order still
  §4.3: WIRED goes last within that same drain).
- If the gate never opens before `s_reboot_at - 100 ms`, the reboot still fires
  on schedule (never delay reboot past 500 ms for persistence). Rationale: the
  reboot itself preempts the ~2 s death window, and the post-reboot boot window
  (radio still down) is a safe place to repair: on boot, if `TAG_WIRED` does
  not match the reboot intent, the boot sequencer reconciles — but since RAM
  intent is lost on reboot, the rule is: **attempt flush while gate open; else
  reboot anyway**. Post-reboot, `s_wired` is re-derived from `store_wired_load`
  (old value) — document this as a known, accepted, rare divergence (user
  re-sends `WIRED_MODE`; next reboot applies it), rather than blocking reboot
  and risking the WDT/death window. The worker logs `wired flushed=0/1` at
  reboot time so HW trials can distinguish the two cases.
- Alternative considered and rejected: forcing `WIRED` synchronous "because
  reboot saves us". Rejected as the general rule because it reintroduces a
  BT-active write by policy; the deadline rule above achieves the same survival
  (reboot preempts death) without blessing synchronous writes.

### 4.4 Dedupe interaction

- `store_host` T2 dedupe (`probe_host_known && memcmp == 0 → return`) is
  preserved and **moved to two places**: (a) at set time (skip setting
  `DIRTY_HOST` when unchanged — no flash wear, no worker wake), and (b) at
  flush time (re-check before `store_tag` — the peer may have reconnected to
  the same host in the meantime, or a `KEY_DELETE` may have cleared
  `probe_host_known`). Both checks are RAM-only.
- `store_color` has no content dedupe today; add a cheap one at flush time
  (compare pending `spi_color_6050` against a `last_flushed_color[13]` shadow;
  skip write when equal, count as `color_coalesced`). Never at accept time
  (accept must always update the live SPI answer — Switch reads it over HID).
- `store_cap_save` keeps its `cap_encode` failure → `return false` path; at
  flush time an encode failure clears nothing, keeps `DIRTY_CAP`, and follows
  §4.5 retry semantics (it is a data error, not a flash error — do not spin).
- `link_key_count()` (read-only iterator) is unaffected and remains callable
  for `BT READY` logging.

### 4.5 Failure semantics (`rc != 0` path)

Every tag op in the drain returns through the existing `tag_store_safe` /
`flash_safe_execute` wrappers, which yield `PICO_OK` + inner `rc`. Rules:

1. Per-tag bite: on `rc != 0` (or `flash_safe_execute != PICO_OK`), **keep that
   bit set**, clear only the bits that verified `rc == 0`, stop the drain at
   the failed tag (preserve §4.3 order for the retry — do not skip ahead to
   WIRED on a failed KEYS/HOST step), and exit the worker before the WDT feed.
   A §4.9.2 scan-cap trip on the flush path feeds this exact rule (it arrives
   as `rc != 0`): keep dirty, keep connection state, retry later — never
   reformat live, never skip silently.
2. Bounded retry: per-bit `fail_count` (saturating at 255) + `last_rc`. Retry at
   subsequent gate openings with backoff (attempt at most once per quiesce
   opening; never busy-loop within a tick). No cap on total attempts (tags are
   user data; dropping them silently is worse than retrying), but see rule 4.
3. No WDT interaction: the drain never sleeps, never waits on Core1 beyond what
   `flash_safe_execute` already does (~2 ms proven), and never extends a tick
   past its budget. If a drain would exceed the budget (measure; nominal full
   drain is single-digit ms), stop after the current tag and resume next
   opening — order preserved by the fixed sequence + remaining bitmap.
4. `WIRED` failure across the reboot deadline: reboot anyway (§4.3). The bit is
   moot post-reboot; log it.
5. Observability: each failure logs `wflash tag=<name> rc=<n> attempt=<k>`
   (names, never bytes). `g_vs.errcode` / STATUS bits are **not** repurposed
   for flash errors in stage 1 (STATUS errcodes are protocol-reject codes per
   spec §5.5; overloading them confuses the PC side). Flash health is reported
   via `probe_line` + the 1 s stats line (add `wpend=<bitmap> wfail=<n>`
   counters there), not via STATUS.

### 4.6 Logging (no secrets) and counters

Allowed: tag names (`host/color/cap/wired/keys`), `rc`, attempt counts, dirty
bitmap hex, `saved(n=…)` counters (existing `host saved (n=…)` style), peer
BD_ADDR (public identity, already logged as `hid open. host … saved`), cap
`saved=0/1` + `seen=N` + entry index (already logged), `wired flushed=0/1`.

Forbidden forever: LTK / link-key bytes, `key[]` contents from the key iterator,
cap payload bytes beyond the existing MAC/index metadata, color bytes are
borderline (they are user-chosen controller colors, not secrets — logging them
is permitted but pointless; log `color set (n=…)` instead).

New monotonic counters (Core0 statics, printed on the 1 s stats line):
`w_runs`, `w_tags_ok`, `w_tags_fail`, `w_coalesced`, `w_gate_closed_ticks`
(saturating), plus permanent-fix counters `tinv_fail` (integrity verdicts),
`tscan_cap` (scan-cap trips, boot + flush), `rfmt_runs` / `rfmt_ok`
(reformats attempted / verified). Keep the existing `s_host_save_count` semantics (increment only on
verified flush, not on dirty-set).

### 4.7 Host-test impact (`tests/host` must stay green)

Touched paths and covering tests:

| Change | Host test | Expectation |
|---|---|---|
| `src/proto/dispatch.h/.c` — **unchanged** in stage 1 (FX enum, `v3_on_frame`, `v3_pack_status` untouched) | `config` (`tests/host/test_config.c`: HELLO/PING/CONFIG→FX mapping, outbox, STATUS layout) | Green, no modification. The worker consumes `g_vs.fx` exactly as `exec_fx` does today; dispatch semantics do not move. |
| `src/proto/protocol.c` (parser) — untouched | `protocol` (`tests/host/test_protocol.c`) | Green, no modification. |
| `src/proto/pack.c`, `src/proto/spi.c`, `src/usb/usb_hid.c` — untouched | `usb` (`tests/host/test_usb.c`) | Green, no modification. |
| New: dirty bitmap + order + gate predicate | None today — **additive** option: place the bitmap/order/coalesce logic in a Pico-independent micro-TU (or `static` + host harness `#include`) with a new `test_wflash.c` asserting order (keys→host-forget→cap-forget→host→cap→color→wired), forget-wins coalescing, T2 re-check, and `rc!=0` keep-bit semantics with a stubbed `store_tag`. The §4.9.2 trip-cap (`TLV_SCAN_TRIP_CAP=1024`, derived `>= 2 * (4096 / HDR_MIN)`) and the §4.9.1 walk/verify logic (bounds + termination over a stubbed 4 KB image) go in the same micro-TU with the same stub harness: assert cap-trips return error (never a truncated list) and assert corrupt images vote `fail`. Do **not** compile `store.c` (Pico flash + BTstack TLV) or `main.c` (SDK/BTstack) into the host suite. | Suite stays green; new test is additive and MSVC-clean (`/utf-8`, no Japanese comments required but permitted). |
| `src/bt/store.c` — refactor `store_host/store_color/store_cap_save/…` into `*_request()` (RAM + dirty) + `*_flush()` (flash op) wrappers | Not compiled into host suite (`tests/host/CMakeLists.txt` does not reference `src/bt/`) | No impact. |

Rule: stage-1 diff must not alter any line compiled by `tests/host/CMakeLists.txt`
except to add a new test executable. CI command unchanged (AGENTS.md: vcvars64
cmd, full-path `ctest.exe` from the cmake dir).

### 4.8 Placement sketch (non-normative, for the implementer)

- `src/main.c`: replace the five `store_*` / `gap_delete_all_link_keys` call
  sites (§2) with dirty-set calls; add `flash_worker_tick(now)` between
  `flush_outbox()` and the pack section of `poll_tick` (or immediately before
  `watchdog_update()` — either is fine provided the WDT feed stays last and the
  worker never runs after the `s_reboot_at` check has fired).
- Quiesce query: implement in link layer (`src/bt/link_conn.c` owns
  ACL/outgoing/pairing state; `hid.c` owns TX state) as
  `bool link_flash_quiesced(void)`, declared in `src/bt/link.h`. `main.c`
  calls it; no `tusb.h`/`btstack.h` co-inclusion issues (worker TU is Core0,
  BTstack-only — never pull `tusb.h` into it per the `hid_report_type_t`
  gotcha in `docs/poc_dualcore_result.md`).
- Stage-2 reservation: `flash_worker_tick` gains a `FLASH_W_STAGE2` branch
  later (`hci_power_control(OFF)` → drain → `ON` → `link_radio_update`); stage
  1 ships with the branch absent but the call site and bitmap already in place,
  so stage 2 is a policy extension, not a rewrite.
- Estimated size: +150–250 lines (worker + gate + counters + logs), −~15 lines
  of direct store calls. No new tasks, no new timers, no new mutexes (Core0
  only; Core1 never touches the bitmap).

### 4.9 Confirmed permanent additions (branch (b) — required, not optional)

Hemostasis (§4.1–§4.8) stops the trigger. This section removes the landmine:
an already-tombstoned bank that hangs the pre-write scan before the first
program (S-trail stops at S3, `chg0=chg1=0`, `write_offset` unmoved). All three
items below are CONFIRMED scope (decided on the Phase-0 verdict), not
provisional. Quiesce-alone-sufficient readings of §§1–4.8 are superseded by
this section.

#### 4.9.1 Boot-time bank-integrity check (XIP reads only, before radio up)

Where it runs: in `src/main.c` `main`, in the existing window after TLV init
and after `store_*_load` but before `cyw43_arch_init` / `hci_power_control`
(the same boot window Option B identified; radio is still down, Core1 not yet
launched or locked out — reads need no `flash_safe_execute`). XIP-mapped reads
only. Zero flash writes on this path (no erase, no program, no TLV repair
inline — repair is exclusively §4.9.3, decided after the check completes).
Wired boot runs the identical check in the same relative position (before USB
bring-up); the check is transport-independent.

What it validates, per bank (both 4 KB banks, `TOTAL=2` sectors), as one
single epoch-linked block (snapshot, then dump, then compare — per the W9
3-binding-check precedent):

1. Geometry sanity: `current_bank` in {0,1}, `write_offset` within
   `[0, 4096]` for each bank; active bank's offset points inside its bank.
2. Tag-length walk: starting at bank base, iterate entries reading
   `(tag, len)` headers from XIP; every `len` must satisfy
   `hdr_end + len <= bank_base + 4096` and `tag` must be in the known
   namespace (`BCHO`/`BCCL`/`BCW1`/`BCWR` plus BTstack link-key namespace) or
   the tombstone marker; any out-of-bounds or unknown-wild `len` is a failure.
3. Termination: the walk must reach an erased terminator (all-`0xFF` from the
   final entry end to the bank end, i.e. the free region reads back `0xFF`);
   reaching bank end without a terminator, or finding non-`0xFF` bytes past the
   computed end, is a failure. A fully-erased bank (all `0xFF`, offset 0) is
   healthy-empty, not a failure.
4. Per-bank CRC vs NOLOAD-carried previous values: CRC32 over the full 4 KB
   image of each bank, compared against the snapshot carried in
   `__uninitialized_ram` noinit (preserved across `watchdog_reboot`, cleared
   on power loss — compare consecutive WDT boots, never a virgin boot, per W9
   concern C5). Expected-change rule: on a boot that follows a verified flush,
   exactly the written bank changes by exactly the committed bytes (W9
   precedent: `wroff 2255->2269 +14B`, `chg0=1/chg1=0`); on an op-in-death
   reboot, `chg0=chg1=0` with an S3-stopped trail is the op-in signature, not
   a CRC failure by itself. A CRC mismatch with no recorded flush (and no
   WDT-boot lineage explaining it) votes failure; CRC equality never
   overrules a walk/termination failure (branch (b) proved a lethal bank can
   be CRC-stable).

Failure threshold: ANY ONE hard failure (out-of-bounds `len`, unknown-wild
header, unterminated walk, offset out-of-range, unexplained CRC change)
quarantines the bank set and arms §4.9.3. There is no count-to-N for
corruption — one corrupt bank is enough. A well-formed tombstoned bank (valid
chain, `host=0`/`keys=0`, healthy-empty or clean deletes) PASSES the check;
its residual scan risk is handled by §4.9.2, not by reformatting a healthy
bank.

Logging: one epoch-linked line per boot (totals + offsets + CRCs + verdict),
no tag payload bytes, no key bytes — names and counts only
(`tinv bank=… wroff=…/… crc=…/… verdict=pass|fail:<reason>`).

#### 4.9.2 Scan iteration cap (finite trip count, error — never silent skip)

Scope: every TLV-iterator walk in OUR layer paths — `src/bt/store.c`
wrappers (`store_*`, `*_forget`, `link_key_count`, `tag_store_safe` callers),
the §4.9.1 boot walker, and the worker drain's pre-write validation walks.
SDK-internal walks (W9 stages S4–S9, inside `btstack_flash_bank.c` erase /
page-program) are explicitly out of scope: we do not patch the SDK here; the
cap guarantees OUR code can neither enter an unbounded scan nor silently
swallow an SDK-side hang (a flush-path walk that does not return within its
cap degrades exactly like §4.5 rule 1: keep the bit, stop the drain, retry
later — the WDT feed path is never blocked).

Bound (normative arithmetic): bank size `BANK = 4096` bytes. Smallest
persisted tag in our namespace is `TAG_WIRED` (`BCWR`), 1 byte payload, so the
smallest plausible entry is header + 1 byte. Taking the conservative BTstack
entry header minimum `HDR_MIN = 8` bytes (4-byte tag + 4-byte length; any real
header is >= this), `MIN_ENTRY = 8 + 1 = 9` bytes, hence
`MAX_ENTRIES = floor(4096 / 9) = 455` entries per fully-packed bank. Even under
a degenerate header-only assumption (`8` bytes, zero payload),
`floor(4096 / 8) = 512`. The cap is set at `TLV_SCAN_TRIP_CAP = 1024` walks —
exactly `2 × 512`, i.e. ≥2× any well-formed bank population under either
assumption, plus margin for the two-bank scan. The implementer asserts
`TLV_SCAN_TRIP_CAP >= 2 * (4096 / HDR_ACTUAL_MIN)` statically against the real
header `sizeof` at build time; if the assertion fails the build fails (the
cap is derived, not tuned).

Degradation (never silent skip): exceeding the trip count returns an error
(`PICO_ERROR_TIMEOUT` or the existing `rc != 0` channel), logs
`tscan cap tag=<name> trips=1024` (names only), keeps the dirty bit set per
§4.5 rule 1, leaves connection/pairing state untouched, and — on the boot
path — votes integrity failure (→ §4.9.3). Partial-walk results are never
consumed as if complete: a capped walk returns failure, not a truncated list.
`link_key_count()` under cap-trip reports error, not 0 (0 would falsely claim
"no keys" and invite insecure re-pairing).

#### 4.9.3 Safe reformat/rebuild path (boot window only, power-loss safe)

Trigger: ONLY §4.9.1 verdict `fail:<reason>` or a boot-path §4.9.2 cap-trip.
Runtime flush-path cap-trips NEVER reformat live — they set a RAM
`reformat_pending` flag, keep dirty bits, keep the connection intact, and defer
the reformat to the next boot (or next `WIRED_MODE` reboot); reformatting
while BT is active would reintroduce the exact hazard the worker removes.

Exact order (all inside the pre-radio boot window, Core0, still no BT/CYW43):

1. Backup first (RAM): copy every individually-valid tag payload to RAM —
   `host` (6 B), `color` (13 B), `cap` blob re-encode source, `wired` (1 B) —
   but only tags that themselves validated (in-bounds `len`, known tag,
   CRC-consistent bank position); any tag failing individual validation is
   dropped to its default, never copied. Link keys are opaque in the BTstack
   DB and cannot be backed up: reformat drops pairings BY DESIGN, and the host
   tag is dropped with them (stale host + fresh keys is worse than clean
   re-pair; next boot forces Change-Grip pairing). Log the backup set by name
   (`rfmt backup=host,color,wired drop=keys,cap:<reason>`).
2. Erase via the BTstack TLV init path only (never raw `flash_range_erase`
   outside TLV — bank-swap atomicity must be preserved: at least one bank
   stays valid at every instant).
3. Verify erased (both banks read back `0xFF` in the free region, offsets 0);
   on verify failure, refuse radio-up and sit in the boot-log loop with a boot
   log line (a device that reports its fault pre-radio is safer than one that
   brings up BT on a half-erased bank; this is an HW-fault halt, distinct from
   the power-cut cases below, which always remain bootable).
4. Rewrite survivors one tag per op (`store_tag` per tag, checking `rc == 0`
   after each, preserving the §4.3 security-first order: keys-forget is moot
   post-erase, then host-forget semantics already applied by the drop, then
   host/cap/color/wired survivors). Stop at the first `rc != 0`, keep the
   remainder dropped (defaults), and boot with the partial set — never loop
   the reformat within one boot.
5. Final verify: re-run the §4.9.1 walk (must now read `pass`, healthy-empty
   or clean-populated) before arming radio up.

Power-loss safety: a cut before step 2 leaves the old (quarantined) bank set,
which the next boot re-quarantines — still bootable, still pre-radio. A cut
during steps 2–4 leaves either one valid old bank (bank-swap atomicity) or a
healthy-empty set; the next boot sees `pass` (empty or partial) and proceeds —
worst case the user re-pairs / re-sets color / re-captures, the same safe
direction as Option A. No reformat path ever runs after radio up, so no power
cut can strand a BT-active session inside an erase.

### 4.10 Timeout ordering rule (finite lockout timeout NOT adopted unconditionally)

No finite `flash_safe_execute` / lockout timeout value is authorized by this
design. Order is binding:

1. Wrapper necessity first: await the 9b wrapper-removal A/B result. If A/B
   shows Death-B follows the outer wrapper (double-wrap post-return damage)
   and vanishes without it, the fix is wrapper removal — no timeout is added,
   this section closes with no value. A timeout is considered ONLY if a
   residual hang window remains after the wrapper decision.
2. Commit-level verification before any value: if — and only if — step 1
   leaves a timeout on the table, the implementer must verify the SDK 2.3.0
   lockout-timeout victim-side fix at commit level (exact upstream commit hash
   recorded in this doc by follow-up amendment, source-read — not
   release-note hearsay) proving the victim side honors a finite timeout
   without corrupting the lockout protocol. Until that hash is recorded, no
   timeout value may be set; there is no default, no interim value.
3. On-timeout semantics (when a verified timeout eventually exists): a timeout
   is a flush failure, never a write: never program/erase on timeout, keep the
   dirty bit(s) set per §4.5 rule 1, keep connection/pairing/HID state intact
   (persist/connection separation — the logical link behavior proceeds as if
   the flush had not been attempted), and retry at a later quiesce opening.
   Timeouts increment `w_tags_fail` and log `wflash tag=<name> rc=timeout
   attempt=<k>` like any other `rc != 0`.

Rationale (wrapper-first): a timeout wrapped around a double
`flash_safe_execute` masks the post-return damage without removing it and
adds a second timing-sensitive mechanism to the exact window under
investigation; the A/B split exists precisely to avoid that.

## 5. Verification steps

### 5.1 Host suite (must stay green)

From a `vcvars64.bat`-initialized cmd (plain PowerShell has no compiler),
`ctest.exe` by full path from the same dir as `cmake.exe` (per AGENTS.md):

```
cmake -S tests/host -B build-host
cmake --build build-host --config Debug
ctest --test-dir build-host -V
```

Single-test triage: `ctest --test-dir build-host -R <protocol|usb|config> -V`.
If the additive `test_wflash` (§4.7) is added: `ctest --test-dir build-host -R wflash -V`.
Gate: all green before any HW flash.

### 5.2 Wireless build

PowerShell, full-path cmake/ninja (cmake is NOT on PATH):

```powershell
$cmake = Join-Path $env:USERPROFILE '.pico-sdk/cmake/v4.3.4/bin/cmake.exe'
$ninja = Join-Path $env:USERPROFILE '.pico-sdk/ninja/v1.13.2/ninja.exe'
& $cmake -S . -B build -G Ninja -DCMAKE_MAKE_PROGRAM:FILEPATH=$ninja
& $cmake --build build
```

Build variants in a temp dir (never dirty `build/`); copy the UF2 to `log/`.
Wired (`-DWIRED_DEFAULT=0/1`) and derated (`-DPOC_DATA_BAUD=115200`) variants as
needed per scenario. Never commit `build/`, `build-host/`, `log/`, `*.uf2`.

### 5.3 HW scenarios (wireless boot unless stated; log = UART0 115200, data = UART1)

1. **Baseline survival (no writes).** Pair, idle 60 s, button sweep via
   `src/poc_dualcore/poc_send.py --sweep`. Expect: no scheduler death, stats
   line `iters` advancing, `FIRE` cadence steady. (Reproduces the ~14+ no-write
   survival reference.)
2. **HID-open host save (W1 → DIRTY_HOST).** Forget host (`KEY_DELETE` while
   disconnected), pair from Switch Change-Grip. Expect: `hid open. host …
   saved` becomes `host dirty (pending)` + flush at next disconnect/idle;
   `host saved (n=…)` appears only after gate opens; reconnect uses new host.
   Repeat connect with same host: expect T2 dedupe (no dirty set, no write).
3. **COLOR_SET while connected (W2 → DIRTY_COLOR).** Send `COLOR_SET` mid-session.
   Expect: immediate ACK behavior unchanged, live SPI answers new color at once,
   no death at +2 s; `wflash tag=color rc=0` at next quiesce; Switch picks up
   new color after documented re-pair (spec §12 caching).
4. **COLOR storm coalescing.** Send 10× `COLOR_SET` back-to-back while
   connected. Expect: one flush, `w_coalesced=9`, single 13 B write.
5. **KEY_DELETE while disconnected (W3 → DIRTY_KEYS_FORGET + DIRTY_HOST_FORGET).**
   Expect: logical effect immediate (keys gone per `link_key_count`, reconnect
   requires fresh pairing), DB/tag deletes persisted at quiesce; no death.
6. **Auth-fail delete (W7).** Force a stale-key auth failure (pair, delete on
   Switch side only, reconnect). Expect: `auth fail: keys dropped, re-pair`
   becomes logical-drop now + DB delete at quiesce; session survives past +2 s.
7. **Capture save (W5 → DIRTY_CAP).** `CAPTURE_START 10`, Joy-Con HOME press,
   wait for `CAP-DONE saved=…`. Expect: `saved=1` means flushed-or-pending per
   new log wording (`saved=1 pending=0/1` — implementer must split the bit);
   `BEACON_START` works after quiesce; no death at +2 s past CAP-DONE.
8. **WIRED_MODE deadline (W4 → DIRTY_WIRED).** While connected, send
   `WIRED_MODE=1`. Expect: reboot in ~500 ms regardless; log shows
   `wired flushed=0/1`; post-reboot wired boot enumerates USB; send
   `WIRED_MODE=0` from wired boot → reboot to wireless. Both directions preserve
   TLV integrity (boot line `wired=… host=… cap=…` sane).
9. **Sudden power loss during dirty (the required destructive test).** While
   connected: send `COLOR_SET` (new color X), confirm live color X over HID,
   then cut power **before** disconnect (dirty never flushed). On next boot
   expect: bootable, old color Y persisted, `wpend=1` cleared (RAM), stats sane,
   no TLV errors, re-pair not required. Repeat with `CAPTURE` pending and with
   `KEY_DELETE` pending (expect: old host/keys persisted → stale pairing must
   be explicitly re-deleted; document this as correct last-writer-loses
   semantics, never auto-delete on boot).
10. **Soak.** 30+ min connected with periodic COLOR_SET + STATUS_REQ + button
    traffic. Expect: zero deaths, `w_tags_fail=0`, WDT never fires
    (`wdt=0` on demand-reboot check), `FIRE` cadence intact across every flush
    (correlate `wflash` lines with timer logs — the core regression signal for
    the original ADD_TIMER_DONE→FIRE death).
11. **Wired-boot fast path.** In `WIRED_DEFAULT=1` boot, send `COLOR_SET` + `WIRED_MODE`
    traffic. Expect: immediate flush (gate trivially open), USB enumeration
    unaffected (radio stays down).
12. **Integrity check on healthy banks.** Boot with known-good banks (mature
    offsets, prior verified flush). Expect: epoch-linked `tinv ... verdict=pass`
    before radio up, no reformat, normal pairing; WDT-boot lineage shows only
    the committed bytes changed (`chg` matches the last `wflash` line).
13. **Tombstone-bank first touch (op-in regression).** Recreate the Phase-0
    recipe exactly: `T_KEY_DELETE` frame over data UART (tombstones, `wroff`
    unchanged) → USB power unplug/replug (NOLOAD cleared) → Switch Grip-screen
    incoming connect. Expect: no hang — either `tinv verdict=pass` (well-formed
    tombstones) followed by a capped, completing first store, or a cap-trip
    error (`tscan cap ... trips=1024`, dirty kept, connection intact, WDT never
    fires) with retry completing later; `chg0=chg1=0` + S3-stop must never
    recur silently.
14. **Corrupt-bank reformat.** Flash/burn a deliberately corrupted bank image
    (out-of-bounds `len` or unterminated walk per §4.9.1) in a temp-dir build,
    boot wireless. Expect: `tinv verdict=fail:<reason>` pre-radio, backup +
    erase-via-TLV + per-tag rewrite + final `pass` before radio up
    (`rfmt backup=... rfmt_runs=1 rfmt_ok=1`); pairings dropped by design
    (re-pair required), surviving user tags intact; power cut mid-reformat
    (repeat trial) still boots to `pass` (empty or partial, never bricked).

### 5.4 Acceptance checklist

- [ ] `tests/host` green (`protocol`, `usb`, `config`, + additive `wflash` if added).
- [ ] Wireless + wired builds clean (no new warnings).
- [ ] Scenarios 1–14 pass with UART0 logs archived to `log/COM3_*.txt` per
      `docs/history/2026-09-15-wdt/README.md` convention (UF2 ↔ log correspondence).
- [ ] No `store_tag`/`delete_tag`/`gap_delete_all_link_keys` call remains on any
      BT-active path except inside `flash_worker_run` (prove by grep).
- [ ] §4.9 evidenced: `tinv verdict=pass` on healthy boots, op-in recipe (sc. 13)
      shows no S3-stop/`chg0=chg1=0` hang, corrupt-bank boot (sc. 14) reforms to
      `pass` pre-radio with pairings cleanly dropped.
- [ ] §4.10 honored: no finite lockout-timeout value set anywhere; 9b A/B
      outcome and (if applicable) the verified SDK 2.3.0 commit hash recorded
      before any timeout amendment.
- [ ] No secret bytes in any log or doc.

## 6. Appendix (optional, SEPARATE approval — do not bundle into the worker)

**WDT composite-heartbeat + scratch-breadcrumb permanent instrumentation.**
Rationale for separation: the worker is a behavior fix (removes the death
trigger per §§1–4.8 plus the §4.9 landmine removal, both in worker scope);
this instrumentation is a diagnostic that must stand on its own review, land
independently, and never gate the worker. It adds **zero** flash writes.

- What exists today (keep): 2 s WDT (`watchdog_enable(2000,1)`,
  `watchdog_update()` at `poll_tick` tail), `watchdog_caused_reboot()` →
  STATUS bit3 (`ST_WDT_RECOVERED`), `HardFault` reporter (`fault_entry_ex`,
  raw-UART PC/LR/CFSR/HFSR, no stdio), MSP/PSP `MSPLIM` guards (Core0 + Core1),
  Core1 `boot_code`/`boot_detail`, 1 s stats line, `ADD_TIMER_DONE`/`FIRE`
  correlation logging from the WDT trials.
- Proposed composite heartbeat (all RAM, ~50 lines): per-second `stats_print`
  gains `hb={runItersDELTA, usbFires, emptyFires, aclN, hidCid, gateOpen}` —
  a single line proving run-loop liveness independently of any one timer. If
  `usbFires` stalls while `runIters` advance (or vice versa), the next WDT
  post-mortem distinguishes "timer wheel dead" from "Core0 wedged" without
  guessing.
- Proposed scratch breadcrumb (no flash, ~30 lines): on each phase transition
  (`ADD_TIMER_DONE`, `FIRE` entry/exit, worker enter/exit, gate open/close),
  write a 32-bit monotonic token + `time_us_32()` into a `__uninitialized_ram`
  noinit array (preserved across `watchdog_reboot`, not across power loss).
  Boot prints the last N tokens when `watchdog_caused_reboot()` is true. This
  is the permanent version of the trial instrumentation that localized the
  original death between `ADD_TIMER_DONE` and next `FIRE`.
- Constraints: never `printf` from the breadcrumb writer (store words only;
  print at boot); never grow `poll_tick` tail past the WDT budget; never log
  secrets; host-test impact nil (Core0-only, SDK-guarded).
