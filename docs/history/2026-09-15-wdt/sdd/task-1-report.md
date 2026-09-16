# Task 1 (W1) report: Deferred flash write (Death-B test)

## Implementation

Work dir: `C:\pico-bcon`. Touched only `src/main.c`, `src/bt/store.c`, `src/bt/store.h`
(store.h backed up in Step 1 before first edit, restored + hash-proven).

**store.c (Step 2)** — extracted write body of `store_host` into
`static void store_host_inner(const bd_addr_t addr, bool force)`; the `memcmp`
early-return is skipped when `force` is true. `store_host` now calls inner with
`false` (byte-identical behavior for existing callers); new
`store_host_force` calls inner with `true`. Save counter + `host saved (n=)` log
stay shared.

**store.h** — added one declaration:
`void store_host_force(const bd_addr_t addr); // W1DIAG: forced write for deferred timer path`

**main.c (Step 3)** — four edits, exactly per brief:
(a) `static btstack_timer_source_t defer_host_timer; /* W1DIAG: ... */` next to
other timer declarations; (b) one-shot `defer_host_handler` after `empty_handler`
(calls `store_host_force(probe_host_addr)`, never re-arms); (c) handler
registration next to other `set_timer_handler` calls; (d) HID-OPEN success branch
now does RAM update (`memcpy(probe_host_addr, a, 6); probe_host_known = true;`),
re-arms the 3 s one-shot (remove/set/add), logs `"hid open. host %s (flash in 3s)"`.
No include change needed — `string.h` already included (main.c line 14).

## Build

- Configure: `C:/Users/moilo/.pico-sdk/cmake/v4.3.4/bin/cmake.exe -S . -B $env:TEMP/wab-w1
  -G Ninja "-DCMAKE_MAKE_PROGRAM=C:/Users/moilo/.pico-sdk/ninja/v1.13.2/ninja.exe"
  -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0` (full paths; cmake/ninja not on PATH).
  Configure exit: **0**. (First attempt without `-DCMAKE_MAKE_PROGRAM` failed with
  "unable to find a build program corresponding to Ninja"; toolchain/SDK paths taken
  from the W0 build cache `$env:TEMP/wab-w0/CMakeCache.txt`, SDK 2.3.0.)
- Build: `cmake --build $env:TEMP/wab-w1 --target pico-bcon` → exit **0** (228/228).
- BUMP check: `Select-String -Pattern "BUMP"` on `pico-bcon.elf` → **0 matches**.

## UF2

- Path: `C:\pico-bcon\log\pico-bcon-w1-defer.uf2`
- Size: **816640 bytes** (same as W0 control UF2)
- SHA256: `FFDB292F398C4A57B1FE5B68C8A08B9CC29D8C43D2FE087ECE9BC6ABC85F45FE`

## Diffs

- `C:\Users\moilo\AppData\Local\Temp\opencode\bcon-wab\diffs\w1-baseline-check.diff`
  (82802 B) — pre-existing `src/main.c` tracked modifications vs HEAD, recorded Step 1.
  NOTE: the working tree was already dirty before W1 (status-before lists M
  CMakeLists.txt, spec/protocol_v3.md, src/main.c, src/poc_dualcore/*, src/proto/*,
  src/usb/usb_wired.c, tests/host/CMakeLists.txt + untracked src/bt/ etc.), so the
  brief-prescribed `git diff -- src/main.c` artifacts below contain that baseline plus
  the W1 edits. Exact W1-only evidence is in the `-vs-backup` diffs.
- `...\diffs\w1-main.diff` (84146 B, 1025 lines) — brief Step 5 output
  (`git diff -- src/main.c`); W1 hunks verified present, remainder = pre-existing baseline.
- `...\diffs\w1-store.diff` (2294 B, 35 lines) — brief Step 5 output
  (`git diff --no-index` backup vs edited store.c); shows ONLY the Step 2 refactor.
- `...\diffs\w1-main-vs-backup.diff` (4516 B, extra) — backup-relative main.c diff;
  shows EXACTLY the 4 Step-3 edits, nothing else.
- `...\diffs\w1-store-h-vs-backup.diff` (1116 B, extra) — backup-relative store.h diff;
  shows EXACTLY the 1-line declaration addition.
- store.c vs backup (`w1-store.diff` content): `store_host` → `store_host_inner`
  (+`!force &&`), plus `store_host`/`store_host_force` wrappers. Nothing else.

## Restore evidence

Restored via `Copy-Item` from backups only (no git checkout/clean):
`src/main.c`, `src/bt/store.c`, `src/bt/store.h`.

- `(Get-FileHash src/main.c) == backup` → **True**
  (`690F3A79...C27A` both sides)
- `(Get-FileHash src/bt/store.c) == backup` → **True** (`40456D82...772C` both sides)
- `(Get-FileHash src/bt/store.h) == backup` → **True** (`FF4B5284...90FE` both sides)
- `Compare-Object status-before-w1.txt status-after-w1.txt` → **no differences**.
- Status files: `...\bcon-wab\status-before-w1.txt`, `...\bcon-wab\status-after-w1.txt`
  (identical; both show the pre-existing dirty tree, unchanged by W1).

## Files changed (net after restore)

None in the working tree — all W1 edits fully reverted. New artifacts only:
`log/pico-bcon-w1-defer.uf2`, `diffs/w1-*.diff` (5 files), `status-before/after-w1.txt`,
`backups/w1/{src_main.c.bak,store.c.bak,store.h.bak}`, build dir `$env:TEMP/wab-w1`
(left in place; `build/` never touched).

## Self-review

- [x] Steps 1–7 followed; no commits/pushes/PRs (none made).
- [x] `src/bt/store.c` never edited before its Step-1 backup; `store.h` backed up
      before first touch and restored/proven.
- [x] Backup-relative diffs prove exactly the specified Step 2–3 changes, nothing else.
- [x] `git diff`-prescribed artifacts saved verbatim per brief (baseline pollution
      documented, not edited around).
- [x] Build exit 0, no BUMP, UF2 saved with size recorded.
- [x] Restores hash-proven True ×3; status files identical.
- [x] `C:\Users\moilo\pico-wakecon` untouched.

## Concerns

1. Dirty baseline: tree was already modified vs HEAD before W1 started, so `w1-main.diff`
   (git-based) cannot show "ONLY the Step 2–3 changes". Mitigated with `w1-main-vs-backup.diff`
   which does. If the A/B harness diffs `w1-main.diff` against W0 expectations mechanically,
   it will see baseline noise — use the `-vs-backup` file as ground truth.
2. `defer_host_handler` calls `store_host_force(probe_host_addr)` unconditionally: if a
   disconnect precedes firing, the save still happens (per brief: intended and harmless),
   and since RAM was already updated at open time, `force=true` only skips the memcmp —
   the write itself is the same `tag_store_safe` path.
3. `string.h`/`memcpy` needed no new include (already present); compiler confirmed via
   successful build.
