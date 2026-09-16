# Plan A Task 1 Report — PROTO_VER 3→4, HELLO v4-only, sender + tests

Status: DONE. No concerns blocking later tasks. No commits (repo rule).

Skills used: `executing-plans` (task-by-task execution) and `test-driven-development`
(RED before GREEN, verified both runs). No subagents (per task instructions).
Reference-only tree `C:\Users\moilo\pico-wakecon` never touched. No secret bytes anywhere.

## What was implemented (brief Steps 1–6, exact)

1. **Step 1 — Read current HELLO block.** Read `src/proto/dispatch.c` `case T_HELLO:`
   (three branches: `== PROTO_VER → OK`, `> PROTO_VER → DOWNGRADED`, else `UNSUPPORTED`)
   and `tests/host/test_config.c` blocks `[0]` (`{0x03,0x00}` → OK), `[1]` (`{0x04,0x01}` →
   DOWNGRADED), `[2]` (`{0x02,0x00}` → UNSUPPORTED). Also read spec §0 (confirms: no
   DOWNGRADED branch, non-v4 → UNSUPPORTED).
2. **Step 2 — Bump the version.** `src/proto/protocol.h:11`:
   `#define PROTO_VER 0x04` (was `0x03`).
3. **Step 3 — HELLO v4-only.** `src/proto/dispatch.c`: three-branch check replaced with the
   brief's verbatim two-branch form (`ver == PROTO_VER → OK + accept`, else
   `UNSUPPORTED + !accept`). `hello_seen/ver/flags`, `auto_status`, `ACT_SEND_HELLO_ACK`
   push untouched. `RESULT_VER_DOWNGRADED` enum kept in `protocol.h` per brief.
4. **Step 4 — Test vectors (test-first).** `tests/host/test_config.c`:
   `[0]` hello `{0x04,0x00}` → `RESULT_OK && state_accept && !auto_status` (+HELLO_ACK);
   `[1]` rewritten hello `{0x05,0x01}` → `RESULT_VER_UNSUPPORTED && !state_accept`,
   print renamed to `[1] HELLO newer -> UNSUPPORTED (no downgrade in v4)` verbatim;
   `[2]` hello `{0x03,0x00}` → `RESULT_VER_UNSUPPORTED && !state_accept` (STATE refused,
   NEUTRAL still applies).
5. **Step 5 — PC sender.** `src/poc_dualcore/poc_send.py`: `build_hello` default `ver 3→4`,
   `hello_check` log `ver=3→ver=4`. `T_PING = 0x03` deliberately untouched (frame TYPE,
   not a version byte).
6. **Step 6 — Suite + status.** Full host suite ALL PASS; `git status --porcelain` shows only
   the four Task-1 files among the modified set (rest is pre-existing Phase-3 work).
   Nothing committed.

## TDD evidence

Shell for both runs: `vcvars64.bat` (`C:\Program Files (x86)\Microsoft Visual Studio\2019\
BuildTools\VC\Auxiliary\Build\vcvars64.bat`)-initialized cmd; cmake/ctest by full path
(`C:\Users\moilo\.pico-sdk\cmake\v4.3.4\bin\`), `ctest --test-dir build-host -C Debug -V`.

### RED (vectors updated, production still v3) — command

```powershell
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" && "C:\Users\moilo\.pico-sdk\cmake\v4.3.4\bin\cmake.exe" -S tests/host -B build-host && "C:\Users\moilo\.pico-sdk\cmake\v4.3.4\bin\cmake.exe" --build build-host --config Debug && "C:\Users\moilo\.pico-sdk\cmake\v4.3.4\bin\ctest.exe" --test-dir build-host -C Debug -V'
```

Failing output (config test only; protocol + usb passed):

```text
3: [0] HELLO v4 -> OK + HELLO_ACK queued
3:   FAIL HELLO v4 OK
3: [1] HELLO newer -> UNSUPPORTED (no downgrade in v4)
3:   FAIL HELLO v5 UNSUPPORTED + auto_status
3: [2] HELLO older -> UNSUPPORTED: STATE refused, NEUTRAL passes
3:   FAIL HELLO v3 UNSUPPORTED
3:   FAIL STATE refused under UNSUPPORTED
3:   PASS NEUTRAL still applies (safe stop)
3: RESULT: HAS FAILURES (4 failures)
3/3 Test #3: config ...........................***Failed
67% tests passed, 1 tests failed out of 3
```

Why expected (feature missing, not typos): production still had `PROTO_VER=0x03` with the
three-branch gate, so `{0x04}` hit DOWNGRADED instead of OK (`[0]`), `{0x05}` hit DOWNGRADED
instead of UNSUPPORTED (`[1]`), and `{0x03}` hit OK instead of UNSUPPORTED so STATE was
accepted (`[2]`). Genuine assertion failures, zero build errors. (Note: at RED time the `[2]`
CHECK label still read "v2"; relabeled to "v3" right after — cosmetic only, assertions logic
already final.)

### GREEN (after Steps 2+3+5) — command

```powershell
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" && "C:\Users\moilo\.pico-sdk\cmake\v4.3.4\bin\cmake.exe" --build build-host --config Debug && "C:\Users\moilo\.pico-sdk\cmake\v4.3.4\bin\ctest.exe" --test-dir build-host -C Debug -V'
```

Passing output (tail):

```text
3: [0] HELLO v4 -> OK + HELLO_ACK queued
3:   PASS HELLO v4 OK
3: [1] HELLO newer -> UNSUPPORTED (no downgrade in v4)
3:   PASS HELLO v5 UNSUPPORTED + auto_status
3: [2] HELLO older -> UNSUPPORTED: STATE refused, NEUTRAL passes
3:   PASS HELLO v3 UNSUPPORTED
3:   PASS STATE refused under UNSUPPORTED
3:   PASS NEUTRAL still applies (safe stop)
3: [3]..[13] all PASS (unchanged blocks)
3: RESULT: ALL PASS (0 failures)
3/3 Test #3: config ...........................   Passed
100% tests passed, 0 tests failed out of 3
```

protocol and usb suites: ALL PASS both runs (untouched, no regressions).

### Sender desk verification

- `grep` over `src/poc_dualcore/poc_send.py` HELLO path: `build_hello(ver: int = 4, …)`,
  `ser.write(build_hello())`, `HELLO sent (ver=4 …)` — no v3 version byte left in the HELLO
  path (`T_PING = 0x03` is a frame TYPE, out of scope).
- `python src/poc_dualcore/poc_send.py --help` → usage printed, `EXIT=0`. (Used `python`;
  `python3` is not on PATH on this Windows box. First attempt piped through
  `Select-Object -First 15` reported `EXIT=-1` — a pipe-truncation artifact; clean rerun
  without truncation exits 0.)
- `--hello` against HW not run (gated to the HW batch per brief).

## Files changed (4, as specified — nothing else touched)

- `src/proto/protocol.h` — one line: `PROTO_VER 0x03 → 0x04`.
- `src/proto/dispatch.c` — HELLO branch: DOWNGRADED arm deleted, v4-only two-branch
  (brief-verbatim).
- `tests/host/test_config.c` — vectors `[0][1][2]` + labels as above; blocks `[3]–[13]`
  byte-identical.
- `src/poc_dualcore/poc_send.py` — two lines: `build_hello` default ver, hello log string.

`git status --porcelain` after work: all four present (`M src/proto/protocol.h`,
`M src/poc_dualcore/poc_send.py`, `?? src/proto/dispatch.c`, `?? tests/host/test_config.c`
— the latter two are untracked Phase-3 files, hence `??` rather than `M`). No other file
touched by this task; remaining entries are pre-existing Phase-3 work.

## Self-review findings (completeness / quality / discipline / testing)

- Completeness: all six brief steps done; exact enum/byte values verbatim
  (`0x04`, `{0x05,0x01}`, `{0x03,0x00}`, two-branch code, `[1]` print rename).
- Quality — two deliberate judgment calls, both behavior-neutral and documented:
  1. `[1]` CHECK keeps `&& s.auto_status` (flags=`0x01` → auto_status stays true; that logic
     is unchanged per Step 3, so the extra conjunct preserves coverage rather than adding
     behavior).
  2. Diagnostic labels track the new bytes (`[0]` print/message v3→v4, `[2]` message
     v2→v3); brief's "CHECK bodies unchanged" read as "assertions need no logic change".
- Discipline/YAGNI — deliberate non-changes (flagged for later tasks, not fixed here):
  `protocol.h:1` header comment still says `(PROTO_VER=3)`; `dispatch.c:9` init comment still
  mentions DOWNGRADED; both outside the specified edit sites (header comment presumably
  handled with the Task 9 spec revision).
- Downstream note: `src/main.c:448` sends `PROTO_VER` in HELLO_ACK — picks up `0x04`
  automatically, no edit needed.
- Testing: RED watched and failing for the right reason; GREEN 3/3 ALL PASS, pristine output
  (no warnings/errors); sender `--help` exit 0.

## Concerns

None blocking. Minor environment notes (not failures): `cmake`/`ctest`/`cl` are absent from
plain-PowerShell PATH — full pico-sdk paths + VS2019 `vcvars64.bat` used per AGENTS.md;
`python3` alias missing on Windows, `python` used for the `--help` check.
