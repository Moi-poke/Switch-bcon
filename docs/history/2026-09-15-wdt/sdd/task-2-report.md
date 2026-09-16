# P3 Task 2 report: Dedupe host save

## What I implemented
- `src/bt/store.c` — replaced `store_host()` body with the brief's exact change-guarded version verbatim (early return on unchanged addr, `tag_store_safe` rc check, `s_host_save_count` + `host saved (n=%lu)` probe line).
- Includes added (2, both compiler-required): `#include <stdio.h>` (for `snprintf`; no header in store.c's chain provided it) and `#include "bt_compat.h"` (declares `probe_line`; verified `link.h`/`store.h`/`cap.h`/`spi.h` do NOT declare or include it; convention matches `hid.c`/`link_cap.c`). Note: this exceeds the task's "one include at most" guidance by one — both are strictly required for a clean compile (GCC 15 treats implicit declarations as errors).
- `src/main.c` (`handle_hid_meta`, `HID_SUBEVENT_CONNECTION_OPENED` success branch) — replaced the RAM-only diagnostic block with the brief's exact `store_host(a)` + `"hid open. host %s saved"` / `bd_addr_to_str(a)` version verbatim. No new include needed (`btstack.h` → `btstack_util.h` already declares `bd_addr_to_str`). Minor brief inaccuracy (non-blocking): `bd_addr_to_str` was NOT already used in main.c (repo-wide grep: zero hits before my edit), but the symbol is available, so the hunk compiles as specified.
- Nothing else touched. Change left in place, uncommitted.

## Build
- Configure: `"C:\Users\moilo\.pico-sdk\cmake\v4.3.4\bin\cmake.exe" -S . -B "$env:TEMP/p3t2" -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0 -DBD_ADDR_BUMP=1 "-DCMAKE_MAKE_PROGRAM=C:/Users/moilo/.pico-sdk/ninja/v1.13.2/ninja.exe"` (workdir `C:\pico-bcon`; cmake/ninja not on PATH so SDK full paths used; fresh `p3t2` dir, `build/` untouched) → exit 0, configured OK.
- Build: `cmake --build $env:TEMP/p3t2 --target pico-bcon` → exit 0, all 228 steps incl. `src/bt/store.c.obj`, `src/main.c.obj`, linked `pico-bcon.elf`. UF2: `C:\Users\moilo\AppData\Local\Temp\p3t2\pico-bcon.uf2`.
- Host unit tests not run: store.c flash path is not host-unit-testable (per brief, wireless recipe is the specified verification).

## Hunk description (mine vs pre-existing)
- Mine: (a) `src/bt/store.c` `store_host()` guard + counter + probe line (+2 includes) — note `src/bt/` is untracked in git, so this hunk does NOT appear in `git diff`; verified by direct re-read (lines 1–11, 74–98). (b) `src/main.c` open-time call-site (`store_host(a)` + `bd_addr_to_str` log) — visible in `git diff -- src/main.c` alongside pre-existing hunks.
- Pre-existing (untouched): Task 1 SNIFF hunk (`gap_set_default_link_policy_settings(...ROLE_SWITCH|SNIFF_MODE)` + comment) still present in `src/main.c`; all other `git status` entries (`CMakeLists.txt`, `spec/protocol_v3.md`, `poc_send.py`, `protocol.h`, `usb_wired.c`, `tests/host/CMakeLists.txt`, untracked docs/dispatch/test_config) predate this task and were not modified. `git diff` file set before and after my edit is identical.

## 5-line flash/test note (owner executes hardware later)
1. UF2: `C:\Users\moilo\AppData\Local\Temp\p3t2\pico-bcon.uf2` (wireless: WIRED_DEFAULT=0, BAUD=115200, BUMP=1) — BOOTSEL-drag onto Pico 2 W.
2. Switch prep: open Change-Grip/Order screen, pair once from the Pico — expect exactly one `host saved (n=1)` plus `hid open. host <addr> saved`.
3. Reboot Pico (re-plug, no Switch re-pair): boot log must show stored host (`host=1`) with NO new `host saved` line.
4. Reconnect from Switch (wake/re-page): link must come up using the stored host; repeated opens to the same console must NOT print further `host saved` lines.
5. PASS = one save on first pair, silence thereafter, reconnect works; FAIL = `host saved` count grows per open, or open-time hang/WDT return, or `hid open FAIL`.

## Files changed
- `C:\pico-bcon\src\bt\store.c` (guard + counter + 2 includes)
- `C:\pico-bcon\src\main.c` (open-time call-site only)

## Self-review findings, issues, concerns
- Re-read both hunks: byte-match to brief text confirmed; `git diff -- src/main.c` grep confirms new hunk present and Task 1 SNIFF hunk intact; no other files modified.
- Concern 1 (minor, accepted deviation): 2 includes added vs "at most one" — both compiler-required, documented above.
- Concern 2 (brief doc error, non-blocking): `bd_addr_to_str` was not previously used in main.c, but resolves via `btstack.h`; build proves it.
- Concern 3: `src/bt/` untracked means Task 2's store.c hunk is invisible to `git diff` reviewers — flag for whoever commits; do NOT commit per plan constraint.
- No commits, no reverts, no changes outside the two specified hunks. `C:\Users\moilo\pico-wakecon` untouched.
