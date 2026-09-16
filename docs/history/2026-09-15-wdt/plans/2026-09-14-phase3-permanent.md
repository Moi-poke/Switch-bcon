# Phase 3 Permanent Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convert the victorious AB1 diagnostic state into a permanent, clean wireless Pro Controller firmware: SNIFF normalized, host-save deduped, stale-key auto-drop, all Task 4 diagnostics removed, stable MAC finalized, authentic grip colors, guarded reconnect, verified on hardware.

**Architecture:** Eight sequential tasks, each a minimal diff on `C:\pico-bcon` with its own verification (host unit tests where coverable, wireless-build compile always, hardware confirmation where behavioral). AB2 slow-start is dropped (AB1 victory used 100ms empties — proven unnecessary). The `0x02` reply tail `01 02` is left untouched (proven working on Switch 2). No task commits without the explicit approval gate in Task 8.

**Tech Stack:** Raspberry Pi Pico SDK 2.3.0, BTstack (bundled), CMake + Ninja, PowerShell 5.1, RP2350 / pico2_w, Nintendo Switch 2 target.

**Spec:** `docs/handoff_bt_20260914.md` (procedures, constraints, log reading) plus AB1 victory log `log/COM3_2026_09_14.22.32.18.050_ab1.txt` (proof that SNIFF-on + 100ms empties + current replies complete the handshake). Executors read the handoff before starting.

## Global Constraints

- No commits, pushes, PRs, or branch operations until Task 8's explicit approval gate — and even there, stop and ask.
- `C:\Users\moilo\pico-wakecon` is reference-only: never modify, never build into.
- Never paste secret key bytes (link keys, LTK) into reports, logs, or code. HCI dumps stay in local files only.
- `log/*.uf2`, `build/`, `build-host/`, `docs/superpowers/`, `.superpowers/` are git-ignored scratch.
- Wireless build recipe: `cmake -S . -B <fresh-tempdir> -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0 [-DBD_ADDR_BUMP=<n>]`, then build `--target pico-bcon`. Never reuse `build/` (wired build lives there). Toolchain if missing from PATH: cmake `C:\Users\moilo\.pico-sdk\cmake\v4.3.4\bin\cmake.exe`, ninja `C:\Users\moilo\.pico-sdk\ninja\v1.13.2\ninja.exe`.
- Host tests: in a `vcvars64`-ready shell, `cmake -S tests/host -B build-host` → build Debug → `ctest -C Debug -V`. Must stay 3/3 ALL PASS.
- Each behavioral task ends with a hardware confirmation on Switch 2 (Change-Grip pairing, SUB arrival, no `0x13`).

---

### Task 1: Normalize SNIFF (AB1 value made permanent)

**Files:**
- Modify: `src/main.c` (link-policy block)
- Test: wireless build compile + hardware SUB arrival

**Interfaces:**
- Consumes: nothing (AB1 UF2 already proved this value wins).
- Produces: permanent SNIFF-on baseline for all later tasks.

- [ ] **Step 1: Read the current block**

Read `src/main.c` around the `gap_set_default_link_policy_settings` call. Current text (diagnostic):

```c
    // Task 4診断用の一時措置: SNIFF受容を外す (open後SNIFF突入とWDT死の因果切り分け)。
    // Switch主導のSNIFF突入直後に共有バス転送が止まる疑い。ROLE_SWITCHは維持。
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH);
    gap_set_allow_role_switch(true);
```

- [ ] **Step 2: Replace with the permanent form**

```c
    // Switch 2 requires SNIFF acceptance: without it the console never sends
    // SUB after HID open and drops the link with 0x13 after ~1s (AB1 proven).
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH |
                                         LM_LINK_POLICY_ENABLE_SNIFF_MODE);
    gap_set_allow_role_switch(true);
```

- [ ] **Step 3: Compile the wireless build**

Run: recipe from Global Constraints with `-DBD_ADDR_BUMP=1`
Expected: exit 0, UF2 produced.

- [ ] **Step 4: Hardware confirmation**

Flash, Switch-side unpair + power cycle + Change-Grip. Expected: `SUB=0x02` arrives, full handshake, no `0x13` (same as AB1 victory log).

- [ ] **Step 5: Report** (no commit)

### Task 2: Dedupe host save (replaces open-time write storm)

**Files:**
- Modify: `src/bt/store.c` (`store_host`), `src/main.c` (re-enable the call)
- Test: wireless build compile + hardware (pair → reboot → `host=1` at boot with exactly one save)

**Interfaces:**
- Consumes: Task 1 baseline.
- Produces: `store_host()` that writes flash only on change; `probe_host_known` RAM semantics unchanged.

- [ ] **Step 1: Read current `store_host`**

`src/bt/store.c` currently:

```c
void store_host(bd_addr_t addr)
{
    const btstack_tlv_t *tlv = NULL;
    void *ctx = NULL;
    if (!get_tlv(&tlv, &ctx)) {
        return;
    }
    (void)tag_store_safe(tlv, ctx, TAG_HOST, addr, 6);
    memcpy(probe_host_addr, addr, 6);
    probe_host_known = true;
}
```

- [ ] **Step 2: Add change-guard plus save counter**

```c
static uint32_t s_host_save_count;

void store_host(bd_addr_t addr)
{
    const btstack_tlv_t *tlv = NULL;
    void *ctx = NULL;
    if (probe_host_known && memcmp(probe_host_addr, addr, 6) == 0) {
        return; /* unchanged: no flash wear, no timer risk */
    }
    if (!get_tlv(&tlv, &ctx)) {
        return;
    }
    if (tag_store_safe(tlv, ctx, TAG_HOST, addr, 6) != 0) {
        return;
    }
    memcpy(probe_host_addr, addr, 6);
    probe_host_known = true;
    s_host_save_count++;
    {
        char msg[48];
        snprintf(msg, sizeof(msg), "host saved (n=%lu)",
                 (unsigned long)s_host_save_count);
        probe_line(msg);
    }
}
```

`string.h` (memcpy) and `stdio.h` (snprintf) availability: `store.c` already includes `string.h`; confirm `probe_line` is visible via `link.h`/`bt_compat.h` (both already included). If `snprintf` needs `stdio.h`, add the include and say so in the report.

- [ ] **Step 3: Re-enable the open-time call**

In `src/main.c` `handle_hid_meta`, `HID_SUBEVENT_CONNECTION_OPENED` success branch, replace:

```c
                // Task 4診断用の一時措置: flash保存を止めRAMのみ。
                // open時TLV書込とタイマ死亡の因果切り分け (書込嵐の停止も兼ねる)。
                // store_host(a);
                memcpy(probe_host_addr, a, 6);
                probe_host_known = true;
                snprintf(msg, sizeof(msg), "hid open. host cached (no flash)");
                probe_line(msg);
```

with:

```c
                store_host(a);
                snprintf(msg, sizeof(msg), "hid open. host %s saved",
                         bd_addr_to_str(a));
                probe_line(msg);
```

(`bd_addr_to_str` is already used elsewhere in `main.c`; the `memcpy` fallback is deleted because `store_host` does it.)

- [ ] **Step 4: Compile the wireless build**

Run: recipe with `-DBD_ADDR_BUMP=1`
Expected: exit 0.

- [ ] **Step 5: Hardware confirmation**

Pair once → `host saved (n=1)` exactly once → reboot Pico → boot log shows stored host (`host=1`-equivalent, no `no host` line) with NO new save line → reconnect path uses the stored host.

- [ ] **Step 6: Report** (no commit)

### Task 3: Auth-fail stale-key drop (replaces startup wipe)

**Files:**
- Modify: `src/main.c` (AUTH_COMPLETE handler + remove wipe block)
- Test: wireless build compile + hardware stale-key recovery without manual key delete

**Interfaces:**
- Consumes: Task 2 (host persists; keys must now self-heal the same way).
- Produces: no startup wipe; automatic key discard on authentication failure.

- [ ] **Step 1: Read the two spots**

Spot A — `handle_bt_ready`: the Task 4 wipe block:

```c
    // Task 4診断用の一時措置: 起動時に鍵・hostを全消去 (stale鍵毒の排除)。
    // 恒久対応 (auth失敗時破棄/dedupe) までの繋ぎ。revert予定。
    // BT稼働後 (wakeconのK操作と同条件) の初回のみ実行。
    {
        static bool wiped;
        if (!wiped) {
            wiped = true;
            gap_delete_all_link_keys();
            store_host_forget();
            probe_line("keys+host wiped (temp)");
        }
    }
```

Spot B — `packet_handler`'s `HCI_EVENT_AUTHENTICATION_COMPLETE` case (currently status display only; read exact text before editing).

- [ ] **Step 2: Delete the wipe block, keep the key-count display**

Remove Spot A entirely. Keep the following `link keys=%d` display and the `probe_host_known` branch as-is.

- [ ] **Step 3: Drop the key on authentication failure**

In the AUTH_COMPLETE case, after the existing status logging, add:

```c
    if (auth_status != 0) {
        /* Stale key: forget it so the next attempt re-pairs cleanly. */
        gap_delete_all_link_keys();
        probe_line("auth fail: keys dropped, re-pair");
    }
```

Use the exact status variable name from the read in Step 1. Preferred: targeted per-peer drop if BTstack exposes it (check `gap.h` for `gap_drop_link_key_for_bd_addr`); if present, use it with the event's address instead of delete-all and document the choice. If absent, delete-all as above.

- [ ] **Step 4: Compile the wireless build**

Run: recipe with `-DBD_ADDR_BUMP=1`
Expected: exit 0.

- [ ] **Step 5: Hardware confirmation**

With a Pico paired to the Switch, deregister the controller Switch-side only (stale key on Pico), then connect. Expected: one `auth fail` cycle with automatic key drop, followed by clean re-pair with SUB arrival — no manual key delete, no reboot.

- [ ] **Step 6: Report** (no commit)

### Task 4: Remove hci_dump and Task 4 diagnostics (keep safety infra)

**Files:**
- Modify: `src/main.c`, `src/bt/hid.c`
- Test: wireless build compile + host suite 3/3 + hardware (clean log, SUB arrival, no key bytes anywhere)

**Interfaces:**
- Consumes: Tasks 1–3.
- Produces: diagnostic-free firmware; safety infra (WDT, HardFault reporter, MSPLIM) retained.

Ruling (recorded, not optional): the HardFault reporter, MSPLIM stack guard, and WDT enable are safety infrastructure and stay permanently (comments normalized, `Task 4診断用` wording removed). Everything marked as a temporary diagnostic goes, except behavior-neutral one-line observation logs (connection request peer, SSP counters, auth/encryption status), which stay.

- [ ] **Step 1: Remove hci_dump**

Delete `#include hci_dump.h` / `#include hci_dump_embedded_stdout.h`, the `hci_dump_init` + `hci_dump_enable_packet_log` calls, and the dormant `log_hci_packet` definition plus its commented-out call site.

- [ ] **Step 2: Remove flight-recorder and heartbeat diagnostics**

Delete: SCR enum/defines/`scr_dump_and_clear` and its boot call, `SCR_TICK`/`SCR_EMPTY`/`SCR_EV`/`SCR_EVMS` increments, `SCR_SEND` increment in `probe_can_send_now`, `dbg_hb_alarm` + `add_repeating_timer_ms` registration, `watchdog_hw` include in `src/bt/hid.c` (only used for the removed increment — verify no other use before deleting).

- [ ] **Step 3: Remove chatty markers, keep milestones**

Delete: `empty tick` + `cansend`/`cansent` 5-shot markers, `runloop EXIT` probe (keep the `while (1)` guard loop itself), `hid open done` arrival-marker comment (keep a plain `hid open` line). Strip `Task 4診断用` wording from retained safety code comments.

- [ ] **Step 4: Compile + host tests**

Run: wireless recipe (`-DBD_ADDR_BUMP=1`), exit 0. Run: host suite, expected 3/3 ALL PASS.

- [ ] **Step 5: Hardware confirmation + secrecy check**

Flash, pair, confirm SUB arrival. Then grep the captured log for key-length hex runs from any `LINK_KEY_NOTIFICATION`-adjacent output — expected: no key bytes (only statuses/counters). Note: with hci_dump gone, HCI packet bytes no longer appear at all.

- [ ] **Step 6: Report** (no commit; list every removed block with file:line)

### Task 5: Finalize the Bluetooth identity (remove BUMP knob)

**Files:**
- Modify: `CMakeLists.txt` (remove `BD_ADDR_BUMP` option/define), `src/bt/link_conn.c` (remove bump addition)
- Test: wireless build compile + hardware SUB arrival on the finalized address

**Interfaces:**
- Consumes: Tasks 1–4.
- Produces: board-ID-derived stable MAC with no diagnostic knob.

- [ ] **Step 1: Verification build first (do not delete anything yet)**

Build with `-DBD_ADDR_BUMP=0` on the Task 4 tree. Flash, Switch-side unpair + power cycle + Change-Grip. Expected: SUB arrival on the `...:ca` identity with SNIFF on.
  - If it wins: the bump was never load-bearing. Proceed to Step 2.
  - If it loses: keep the `...:cb` address as a baked constant (replace the addition with a fixed last-byte increment and document why: SNIFF-on + `...:cb` is the proven combination), still deleting the CMake knob. Record the outcome.

- [ ] **Step 2: Delete the mechanism**

Remove the `set(BD_ADDR_BUMP ...)` cache option and its compile definition from `CMakeLists.txt`; remove the `#ifndef BD_ADDR_BUMP` block and the `probe_addr[5] += ...` line plus its diagnostic comment from `link_conn.c` (or bake the constant per the Step 1 outcome).

- [ ] **Step 3: Compile the wireless build (no `-DBD_ADDR_BUMP` flag at all)**

Expected: exit 0. Confirm boot log MAC equals the decided permanent address.

- [ ] **Step 4: Hardware confirmation**

Re-pair from clean Switch state on the final address. Expected: SUB arrival; reboot Pico → same address, host remembered (Task 2), no re-pair needed.

- [ ] **Step 5: Report** (no commit; state the final MAC derivation rule)

### Task 6: Authentic grip colors (Joy-Con Toolkit conformance)

**Files:**
- Modify: `src/proto/spi.c` (default table), `tests/host/test_usb.c` (expectation)
- Test: host suite 3/3 (TDD: test first) + wireless build compile + hardware SPI-read check

**Interfaces:**
- Consumes: none (independent of Tasks 1–5; shared table serves USB and BT paths).
- Produces: stock black Pro Controller colors as defaults; PC-driven COLOR_SET path unchanged.

Evidence (do not re-derive without new data): `0x6050` layout is body 3B + buttons 3B + left grip 3B + right grip 3B + spec byte (13B total; Switch 2 reads all 13, proven accepted in the AB1 victory log). Authentic stock values: body `323232` + buttons `FFFFFF` (jc_toolkit `retail_colors.xml`: "Pro Black" / "Pro Black Buttons"); grips `464646` (jc_toolkit issue #28: stock Pro Con grips; Switch FW changed the color-decision algorithm, so authentic values matter). Byte 12 stays `0x00` (proven accepted; NXBT-equivalence not required).

- [ ] **Step 1: Update the test first (RED)**

In `tests/host/test_usb.c`, change the SPI color expectation:

```c
         CHECK(n == 64 && out[13] == 0x90 && out[14] == 0x10 && out[19] == 12 &&
               out[20] == 0x82 && out[21] == 0x82, "10 SPI color 6050");
```

to expect the new body bytes:

```c
         CHECK(n == 64 && out[13] == 0x90 && out[14] == 0x10 && out[19] == 12 &&
               out[20] == 0x32 && out[21] == 0x32 && out[22] == 0x32 &&
               out[23] == 0xFF && out[24] == 0xFF && out[25] == 0xFF &&
               out[26] == 0x46 && out[27] == 0x46 && out[28] == 0x46,
               "10 SPI color 6050 (stock black)");
```

Run: host suite
Expected: FAIL on `10 SPI color 6050` only (proves the test guards the defaults).

- [ ] **Step 2: Change the defaults (GREEN)**

In `src/proto/spi.c`, replace:

```c
uint8_t spi_color_6050[13] = {
    0x82, 0x82, 0x82, 0x0F, 0x0F, 0x0F,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00
};
```

with:

```c
/* Stock black Pro Controller (jc_toolkit conformance):
 * body #323232 (retail "Pro Black"), buttons #FFFFFF ("Pro Black Buttons"),
 * grips #464646 (stock grips per jc_toolkit#28; newer Switch FW applies a
 * strict color-decision algorithm). Byte 12 is a spec value, never restored. */
uint8_t spi_color_6050[13] = {
    0x32, 0x32, 0x32, 0xFF, 0xFF, 0xFF,
    0x46, 0x46, 0x46, 0x46, 0x46, 0x46, 0x00
};
```

Do NOT touch: the 13th byte, `store_color`/`store_color_load` (13B save / 12B restore stays consistent), the 12B PC-driven COLOR_SET path, the `0x02` reply tail `01 02` (proven working), `0x601B = 0x01`.

- [ ] **Step 3: Run the host suite**

Run: full host suite
Expected: 3/3 ALL PASS.

- [ ] **Step 4: Compile the wireless build**

Run: recipe (current BUMP default)
Expected: exit 0.

- [ ] **Step 5: Hardware confirmation**

Pair, trigger the `0x6050` SPI read (happens in every handshake). Expected: reply carries the new bytes; Switch UI shows black controller. No `0x13`, handshake completes as before.

- [ ] **Step 6: Commit-ready report** (no commit — see Task 8 gate)

### Task 7: Final reconnect design (guard with reset + outgoing verdict)

**Files:**
- Modify: `src/main.c` and/or `src/bt/link_conn.c` + `src/bt/link.h` (guard flag with reset)
- Test: wireless build compile + hardware auto-reconnect without Grip menu

**Interfaces:**
- Consumes: Tasks 2–3 (stored host + self-healing keys make outgoing live for the first time).
- Produces: race-free reconnect arming; documented outgoing policy.

Context: the AB4 trial guard is superseded here by a resettable design. The AB3 passive-only diff stays on file as fallback.

- [ ] **Step 1: Implement the resettable guard**

Add a shared armed flag (define in `link_conn.c`, declare in `link.h`), set it wherever `reconnect_timer` is added, clear it in `link_note_disconnected()` (the single funnel for disc/close/fail paths). In `handle_bt_ready`, arm only when host is known AND the flag is clear. Keep intervals (`2000` first, `5000` retry, 15s giveup) unchanged.

- [ ] **Step 2: Compile the wireless build**

Run: recipe. Expected: exit 0.

- [ ] **Step 3: Hardware auto-reconnect test**

Pair, then reboot ONLY the Pico with the Switch idle (not in Grip menu). Expected: outgoing page fires within ~7s and the Switch reconnects without Grip interaction. Then test Switch-side deregister → next connect must self-heal via Task 3 (not hang on retry).

- [ ] **Step 4: Outgoing verdict**

If Step 3 misbehaves (collisions, `0x66` storms, no reconnect): apply the on-file AB3 passive-only change as the permanent policy and re-verify. Otherwise keep active reconnect. Record the verdict with log evidence either way.

- [ ] **Step 5: Report** (no commit)

### Task 8: Verification summary and commit gate (no code)

**Files:** none. **Test:** full host suite + final wireless build.

- [ ] **Step 1: Run everything once more**

Host suite 3/3 ALL PASS; wireless build exit 0; `git status` shows only intended files.

- [ ] **Step 2: Write the completion summary**

Per-task outcomes, hardware evidence log names, deferred items (if any), and the exact `git add` list for a Step 4 commit.

- [ ] **Step 3: STOP and ask for explicit commit approval**

Per handoff §7, do not commit, amend, push, or open PRs on your own. End with the approval request.

## Explicitly dropped (do not re-propose without new evidence)

- AB2 slow-start: AB1 victory used 100ms empties — unnecessary.
- `0x02` reply tail `01 02`: proven working on Switch 2; NXBT-conformance alone is not a reason to touch it.
- Pokémon Automation code comparison: Pico firmware is closed-binary; HOJA lib BT is a stub; debugprobe is a debug-probe fork. Reference stack stays BTstack source + retro-pico-switch origin + wakecon.
- WDT root mechanism beyond the open-time-write trigger: worked around by Task 2's dedupe (no hot-path flash writes remain); the H1 guard in Task 7 closes the remaining race.
