# Plan A Task 7 Report: PLAYER_INFO dispatch + tests (host-testable)

- Date: 2026-09-15. Worker skill: test-driven-development (RED demonstrated by build failure, then GREEN).
- Scope: exactly the five Task-7 files. No commits/branches (prohibited). pico-wakecon untouched. No secrets.

## Implementation

1. `src/proto/protocol.h` — added `T_PLAYER_INFO = 0x23` after `T_RUMBLE`, comment `Pico->PC LEN=2 lamp+flags`. No renumbering.
2. `src/proto/protocol.c` — added `case T_PLAYER_INFO: return 2;` after `T_RUMBLE` in `proto_expected_len`. One line.
3. `src/proto/dispatch.h` — appended `ACT_SEND_PLAYER_INFO` to `v3_act_t` (after `ACT_SEND_HELLO_ACK`, no renumber); added the five session fields verbatim (`player_lamp, player_flags / player_valid / player_sent_lamp, player_sent_flags / player_ever_sent`); declared `void v3_player_tick(v3_session_t *s);`. Header stays pure (FX/ACT only).
4. `src/proto/dispatch.c` — `T_STATUS_REQ` now pushes `ACT_SEND_STATUS` + `ACT_SEND_PLAYER_INFO`; `v3_session_init` sets `player_valid=false, player_ever_sent=false` (explicit, in addition to memset); `v3_player_tick` implements the ever-sent-flag version: return if `!player_valid`; return if ever-sent and lamp+flags unchanged; else push `ACT_SEND_PLAYER_INFO`, update `player_sent_*`, set ever-sent. (Mid-edit I dropped a newline joining `{` to `out[0]=flags;`; repaired immediately and verified by re-read — valid C either way, formatting restored.)
5. `tests/host/test_config.c` — (a) new block `[14]`: `proto_expected_len(T_PLAYER_INFO)==2`, `0x24` still `-1`; (b) rewrote block `[10]` to expect BOTH `ACT_SEND_STATUS` and `ACT_SEND_PLAYER_INFO` in `ob` (`ob_n==2`, scan loop, `ob_n<=V3_OB_N` asserted); (c) new block `[15]` for `v3_player_tick`: invalid→`ob_n==0`; first valid (lamp 0x03/flags 0x01)→queued once; unchanged tick→quiet; lamp change→queued; flags change→queued.

Out of scope (left for Task 8): `main.c` feed/flush, `hid.c`/`hid.h` `probe_player_seen`. Tests drive session fields directly, so no hid dependency, per plan.

## Test results (TDD evidence)

Host suite via vcvars64-initialized shell, full-path tools:
`cmake -S tests/host -B build-host` → build Debug → `ctest --test-dir build-host -C Debug -V`
(helper script used for reliable quoting: `C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\run-host-tests.bat`; temp dir only, not in repo).

- Baseline (pre-change): 3/3 ALL PASS (protocol, usb, config) — reference point.
- RED (tests only, no impl): build failure, exactly the missing symbols —
  `error C2065: 'ACT_SEND_PLAYER_INFO'`, `error C2065: 'T_PLAYER_INFO'`,
  `error C2039: 'player_valid'/'player_lamp'/'player_flags'`, `warning C4013: 'v3_player_tick'` (implicit decl).
  Fails because the feature is missing, not typos. No test passed spuriously.
- GREEN (after impl): **3/3 ALL PASS, 0 failures** — protocol 9/9, usb 30/30, config 34/34 CHECKs incl. new `[10]` (2 PASS), `[14]` (2 PASS), `[15]` (5 PASS). Full output captured in this session's tool log.

## Files changed (mine only)

- M `src/proto/protocol.h` (my line: `T_PLAYER_INFO`; `PROTO_VER=0x04` and `ST_RUMBLE_SEEN` lines are pre-existing baseline)
- M `src/proto/protocol.c` (one added line; file was clean at baseline)
- `src/proto/dispatch.h`, `src/proto/dispatch.c`, `tests/host/test_config.c` (untracked files pre-existing at baseline; edited in place, no other files touched)

`git status --porcelain` after work: identical entry set to baseline except `src/proto/protocol.c` newly `M` with the single `case T_PLAYER_INFO` line. All other M/?? entries (CMakeLists.txt, spec, main.c, poc_send.py, spi, usb, AGENTS.md, docs/history, opencode.json, src/bt, etc.) are pre-existing work by others — untouched. `git diff src/proto/protocol.h src/proto/protocol.c` confirms the delta. No commit made.

## Self-review

- Completeness: all Task 7 Steps 1–3 done — (a)(b)(c) tests, enum/field/decl/helper/init/piggyback impl, suite green, status verified.
- Quality: mirrors Task 5's flag-version contract over (lamp, flags); change-only semantics; first valid sample always sends even for 0,0 (ever-sent flag, no sentinel values); `dispatch.h` purity preserved; STATUS_REQ piggyback fits `V3_OB_N=8` (2 pushes, asserted in test); existing overflow test `[12]` unaffected (PING-only).
- YAGNI: no extra helpers, no hid/main wiring, no spec edits, no behavior beyond the plan. Explicit init assignments duplicate memset zeros but document intent — kept deliberately.
- Testing: RED shown via compile failure naming every new symbol; GREEN shows all 34 config CHECKs pass plus untouched protocol/usb suites.

## Concerns

- None blocking. Note for Task 5 (if still open): it specifies appending `ACT_SEND_RUMBLE` "after ACT_SEND_HELLO_ACK" — `ACT_SEND_PLAYER_INFO` now occupies that slot, so Task 5 should append `ACT_SEND_RUMBLE` at the enum end to avoid renumbering `ACT_SEND_PLAYER_INFO`.
- Note: `src/proto/dispatch.{c,h}` and `tests/host/test_config.c` are untracked in git (pre-existing condition, not caused by this task); Task 8/9 owners should be aware the files are new vs HEAD.
