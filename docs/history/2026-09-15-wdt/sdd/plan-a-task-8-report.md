# Plan A Task 8 Report — main wiring for PLAYER_INFO + hid seen-flag + wireless build

- Date (UTC): 2026-09-15
- Worker constraints honored: no subagents, no commit/push/branch, no flash, no touch of `C:\Users\moilo\pico-wakecon`, no secret key bytes. Desk build only.

## 1. Implementation (exact Task-8 Steps 1–2, verbatim values)

**Step 1 — seen-flag (`src/bt/hid.h`, `src/bt/hid.c`)**
- `hid.h:37`: added `extern bool probe_player_seen;` immediately after `extern uint8_t probe_player_id;`.
- `hid.c:50`: added definition `bool probe_player_seen = false;` next to `probe_player_id`.
- `hid.c` `case 0x30:` (now :327–331): added `probe_player_seen = true;` directly after `probe_player_id = report[10];`, inside the existing `if (report_size > 10)` guard. `0x31` echo path unchanged.

**Step 2 — feed + tick + flush (`src/main.c`)**
- `poll_tick` (after `g_vs.cap_valid = probe_cap_valid;`, before inbox drain — the Task-6 region), verbatim per plan:
  ```c
  g_vs.player_lamp = probe_player_id;
  g_vs.player_flags = (uint8_t)((probe_imu_enabled ? 0x01u : 0u) |
                                (probe_vibration_enabled ? 0x02u : 0u));
  g_vs.player_valid = probe_player_seen;
  v3_player_tick(&g_vs);
  ```
  Placed BEFORE `exec_fx(now)` as required (telemetry queues alongside inbox drain).
- `flush_outbox`, verbatim per plan:
  ```c
  } else if (o->act == ACT_SEND_PLAYER_INFO) {
      uint8_t pi[2];
      pi[0] = g_vs.player_sent_lamp;
      pi[1] = g_vs.player_sent_flags;
      uart_tx_frame(T_PLAYER_INFO, pi, 2);
  }
  ```
- `dispatch.h`/`dispatch.c` untouched (stay pure; Task 7 already provides `ACT_SEND_PLAYER_INFO`, session fields, `v3_player_tick`, STATUS_REQ piggyback).

## 2. Wireless build evidence (Step 3a)

Fresh temp dir — `build/` never reused, nothing flashed, UF2 left in temp dir (nothing copied to `log/`).

Configure:
```
& "C:\Users\moilo\.pico-sdk\cmake\v4.3.4\bin\cmake.exe" -S . -B "C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\task8-wireless" -G Ninja -DCMAKE_MAKE_PROGRAM:FILEPATH="C:\Users\moilo\.pico-sdk\ninja\v1.13.2\ninja.exe" -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0
```
→ `CONFIGURE_EXIT=0` (board `pico2_w`, SDK 2.3.0).

Build:
```
& "C:\Users\moilo\.pico-sdk\cmake\v4.3.4\bin\cmake.exe" --build "C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\task8-wireless" --target pico-bcon
```
→ `BUILD_EXIT=0` (228/228 steps, incl. `src/bt/hid.c.obj` [66/228] and `src/main.c.obj` [78/228], linked `pico-bcon.elf`). Output `pico-bcon.uf2` (817152 bytes) remains in the temp dir.

## 3. Host suite evidence (Step 3b)

vcvars64-initialized `cmd` (VS 18 BuildTools `vcvars64.bat` → `VCVARS_OK`), full-path cmake/ctest from `C:\Users\moilo\.pico-sdk\cmake\v4.3.4\bin\`:
```
cmake -S tests/host -B build-host
cmake --build build-host --config Debug
ctest --test-dir build-host -C Debug -V
```
→ **3/3 ALL PASS, 0 failures** (`protocol`, `usb`, `config`; incl. Task-7 PLAYER_INFO blocks [10] STATUS_REQ piggyback, [14] LEN=2, [15] `v3_player_tick` change-only). `HOST_SUITE_EXIT=0`.

## 4. Files changed (by me)

- `src/bt/hid.h` (untracked Phase-3 file; +1 decl line)
- `src/bt/hid.c` (untracked Phase-3 file; +1 define, +1 set line in `case 0x30:`)
- `src/main.c` (tracked; +5 feed lines, +5 flush lines — verified via `git diff` hunk grep; the file's large pre-existing diff is prior Phase-3 work, not mine)

`git status --porcelain`: shows only pre-existing working-tree entries (prior tasks' uncommitted work: `M CMakeLists.txt`, `spec/protocol_v3.md`, `src/main.c`, `src/poc_dualcore/*`, `src/proto/protocol.*`, `src/proto/spi.*`, `src/usb/usb_wired.c`, `tests/host/*`, plus untracked `src/bt/`, `src/proto/dispatch.*`, `tests/host/test_config.c`, docs). mtime check confirms files written in the last 30 min are ONLY `src/bt/hid.c`, `src/bt/hid.h`, `src/main.c` (all 14:36:46; all other entries are 14:26–14:35 prior-task work). HEAD unchanged (`c366bf4`); no commit/branch created.

## 5. Self-review findings

- [x] Snippets match plan verbatim (field names, flag bits bit0=IMU/bit1=vibration, `pi[]` reads `player_sent_*`, `uart_tx_frame(T_PLAYER_INFO, pi, 2)`).
- [x] Seen-flag set in `case 0x30:` alongside `probe_player_id`, inside the `report_size > 10` guard (short SUB reads can't set valid).
- [x] Feed placed before `exec_fx`, tick called per poll; `dispatch.h` purity preserved.
- [x] Includes resolve: `main.c` already includes `bt/hid.h` (probe globals) and `protocol.h` (`T_PLAYER_INFO`) — proven by clean wireless compile.
- [x] `bool` available in `hid.h` (`<stdbool.h>` already included).
- [x] No `tusb.h`/`btstack.h` co-include introduced; no new timers; Core1 untouched.

## 6. Concerns

1. **RUMBLE path (Tasks 4–6) is absent from the tree** — no `probe_rumble_l/r`, no `ACT_SEND_RUMBLE`, no `src/proto/rumble.*`. Therefore the plan's anchors "next to the Task-6 feed" and "after the RUMBLE arm" do not exist. Resolution applied: feed placed right after the `cap_valid` line (the specified region), flush arm placed after the HELLO_ACK arm (last arm of the chain). Order among distinct `ACT_*` arms is semantically irrelevant. No action needed unless a later task expects a RUMBLE arm at a specific position.
2. **`probe_hid_reset()` does not clear `probe_player_seen`** — the plan specifies define + set only, so this was implemented exactly, but after a HID reset `player_valid` stays true with a stale lamp until the next `0x30`. Suggest Task 9 or a follow-up add `probe_player_seen = false;` next to `probe_player_id = 0u;` in `probe_hid_reset()`. Not changed here (exact-steps rule).
3. Host suite run: full reconfigure + Debug rebuild + `ctest -C Debug -V` — green. Nothing not-run.

## 7. Fix (review finding): clear `probe_player_seen` in `probe_hid_reset()`

- Date (UTC): 2026-09-15
- Change (`src/bt/hid.c`, in `probe_hid_reset()`, immediately after `probe_player_id = 0u;`): added `probe_player_seen = false;` — one line, nothing else changed. Verified at hid.c:105-106. Fixes spurious PLAYER_INFO(0,0) after HID reset (main-tick feed now sees `player_valid=false` until next SUB 0x30).
- Wireless build (FRESH temp dir `task8-fix-wireless`, same recipe `-DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0 --target pico-bcon`, full-path tools): `CONFIGURE_EXIT=0`, `BUILD_EXIT=0` (228/228, `pico-bcon.uf2` 817152 bytes, left in temp dir).
- Host suite: NOT re-run (no vcvars64 shell set up in this session; `hid.c` is not in the host build) — carried GREEN evidence from §3 above (3/3 PASS, `HOST_SUITE_EXIT=0`).
- `git status --porcelain`: no new entries vs §4 (tracked `M` set unchanged incl. `src/main.c`; `src/bt/` still untracked incl. fixed `hid.c`); files changed by me remain only `src/bt/hid.c`, `src/bt/hid.h`, `src/main.c`. HEAD unchanged; no commit.
