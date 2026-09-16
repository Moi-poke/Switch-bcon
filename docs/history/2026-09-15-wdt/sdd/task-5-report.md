# W5 report: Timed flash store (stall-vs-corruption discriminator)

Status: COMPLETE. No commits, no pushes, no PRs. HEAD still `c366bf4`. Tree restored byte-identical (hash-proven). No subagents used. `C:\Users\moilo\pico-wakecon` never touched.

## 1. Change 1 — W1 deferral applied verbatim (Steps 2–3 of task-1-brief)

Reproduced exactly from `briefs/task-1-brief.md` Steps 2–3 and `diffs/w1-main-vs-backup.diff`, `w1-store-h-vs-backup.diff`, `w1-store.diff` (cross-checked against the W4 application `w4-main-vs-backup.diff` / `w4-store.diff` / `w4-store-h.diff`):

- `src/bt/store.c`: `store_host` body extracted into `static void store_host_inner(const bd_addr_t addr, bool force)` (`memcmp` early-return skipped when `force`); `store_host` → inner(false); new `store_host_force` → inner(true). Save counter + `host saved (n=)` log stay shared.
- `src/bt/store.h`: added `void store_host_force(const bd_addr_t addr); // W1DIAG: forced write for deferred timer path`.
- `src/main.c`: (a) `static btstack_timer_source_t defer_host_timer; /* W1DIAG: ... */` next to the other timer declarations; (b) one-shot `defer_host_handler` (calls `store_host_force(probe_host_addr)`, never re-arms) after `empty_handler`; (c) `btstack_run_loop_set_timer_handler(&defer_host_timer, &defer_host_handler)` next to the other registrations; (d) `handle_hid_meta` OPEN branch: `store_host(a)` replaced with RAM update (`memcpy(probe_host_addr, a, 6)` + `probe_host_known = true`), remove/re-arm 3 s one-shot, log `hid open. host %s (flash in 3s)`. No `string.h` addition needed (`main.c` already includes it, line 14).
- Proof of verbatim: `w5-main-vs-backup.diff` reproduces the W1 blob hash `efc6a8d..032f3bd` exactly; `w5-store-h.diff` reproduces `b974221..7dc2a26` exactly.

## 2. Change 2 — W5 timing around the store call (W5DIAG-marked)

- Choke point: the single `tag_store_safe(tlv, ctx, TAG_HOST, addr, 6)` call inside the shared `store_host_inner` — covers the deferred write (`store_host_force` → inner) and any direct `store_host` path. No W3 content in this variant.
- Wrapped exactly per brief (variable names adapted to `store_host_inner` scope), error semantics preserved (`w5_rc != 0 → return`, counter/log untouched, no write counted on failure):
  - `probe_line("store begin");` before, `time_us_32()` delta after, `store dt_us=%lu rc=%d` line after. Every added line carries `/* W5DIAG */` (11 lines: comment, begin, t0, rc, dt, brace pair, buffer, snprintf, probe, closing, `if (w5_rc != 0) {`); the pre-existing `return;` line is retained unmarked.
- `pico/time.h` include: ADDED (`#include "pico/time.h" /* W5DIAG: ... */` after `pico/flash.h`), because the compiler requires it. Evidence: `time_us_32()` is declared in SDK `src/rp2_common/hardware_timer/include/hardware/timer.h`, reachable via `pico/time.h` (which includes it, verified line 11); walked the full transitive include chain of `store.c` (`stdio.h`, `string.h`, `pico/flash.h`→`pico.h`→types/version/config/platform/error only, `btstack_tlv.h`, `cap.h`, `spi.h`, `link.h`, `store.h`, `bt_compat.h`) — none provides it. Build with the include: exit 0.

## 3. Build (Step 4)

- Configure: `cmake -S . -B $env:TEMP/wab-w5 -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0` → done (SDK 2.3.0, toolchain 15_2_Rel1 auto-detected; cmake v4.3.4 + ninja v1.13.2 via `.pico-sdk` full paths; ninja dir prepended to PATH; `build/` never touched). First attempt without ninja on PATH failed at configure (`CMAKE_MAKE_PROGRAM is not set`); re-ran with PATH fixed — environment issue only, no source change.
- Build: `cmake --build $env:TEMP/wab-w5 --target pico-bcon` → **exit 0 (228/228, incl. `src/bt/store.c.obj` [62/228] + `src/main.c.obj` [73/228])**. No new warnings/errors observed; no BUMP flag.

## 4. UF2 (Step 5)

- `C:\pico-bcon\log\pico-bcon-w5-timed-store.uf2`, **816640 bytes**, SHA256 `42002B53C821816115E60B58BD6B73C92DECFBEF6B7EB260904499CE2E9E62B4`.

## 5. Diff inventory (all non-empty)

| File | Bytes | Content |
|---|---|---|
| `diffs/w5-main.diff` | 84146 | `git diff -- src/main.c` (per brief; includes the pre-existing wireless-vs-HEAD baseline diff, exactly as `w1-main.diff` did — see self-review) |
| `diffs/w5-main-vs-backup.diff` | 4516 | vs-backup (W1 precedent): ONLY the 4 W1 main.c hunks, blob `efc6a8d..032f3bd` identical to W1 |
| `diffs/w5-store.diff` | 4224 | vs-backup: ONLY W1 store.c hunks + `pico/time.h` include + timing block |
| `diffs/w5-store-h.diff` | 1116 | vs-backup: ONLY the W1 `store_host_force` decl, blob `b974221..7dc2a26` identical to W1 |

## 6. Restore evidence (Step 6)

- Restored all 3 files from `backups/w5/*` via `Copy-Item` (no git checkout/restore/clean used).
- `status-after-w5.txt` identical to `status-before-w5.txt` (`Compare-Object` empty). (Note: both contain two extra `M` entries vs the W4 status — `src/proto/spi.c`, `tests/host/test_usb.c` — pre-existing work-in-progress already present before this task; this task added/left nothing.)
- Hashes, all True:
  - `src/main.c` `690F3A7937571F7240BDC2922E5F4BCB7DAEB1A83963EECCC3A9C7034E10C27A` == backup
  - `src/bt/store.c` `40456D8201792366F838FDC2121CC5370E61E11ED15B5D9D21CF45D772C87726` == backup
  - `src/bt/store.h` `FF4B52843FB98C9E545350660A0D0F9B3554143BE19C09CE695FF308490FE340` == backup
  - (These equal the pre-edit baseline hashes captured in Step 1 and the W1/W4 backup hashes.)
- Marker grep `W1DIAG|W3DIAG|W5DIAG|defer_host|store_host_force|store begin|scratch` over `src/*.c,*.h` → zero hits.
- Line endings preserved throughout (main.c all-LF before/after; store.c/store.h all-CRLF before/after; verified by CRLF/lone-LF counts, no whole-file rewrites).

## 7. Files changed

- `src/`: none (post-restore). New files only: `log/pico-bcon-w5-timed-store.uf2` + workspace artifacts (`status-before/after-w5.txt`, `diffs/w5-{main,main-vs-backup,store,store-h}.diff`, `backups/w5/*`, this report).

## 8. Self-review

- Vs-backup diffs show exactly W1 hunks + timing lines, nothing else (verified by reading all three; main.c/store.h blob hashes reproduce W1 exactly). `w5-main.diff` additionally contains the pre-existing wireless baseline diff vs HEAD — unavoidable with `git diff --` and identical in character to the archived `w1-main.diff`; the vs-backup diff is the authoritative minimal record.
- Every added W5 line is W5DIAG-marked; every W1 line matches the W1 brief/diffs (W1DIAG markers where W1 put them, nowhere else).
- Error semantics unchanged; counter/log lines intact; `string.h`/`snprintf`/`probe_line`/`uint32_t` all pre-available in `store.c`; only addition to the include chain is the proven-required `pico/time.h`.
- Backup/restore discipline followed exactly; no commits; no subagents; wakecon untouched.

## 9. Concerns

- None blocking. Minor: `probe_line("store begin")` uses `printf` over UART0 — if the WDT death wedge is inside UART TX itself this could perturb timing, but W4 already rules out poll-tick wedges and the probe path is the established one.
- `w5m[48]` fits `store dt_us=4294967295 rc=-1` (26 chars + NUL) with margin.

## 10. Reading guide (Step 7) — how to interpret the W5 build on the bench

Flash `log/pico-bcon-w5-timed-store.uf2`, connect a second host, watch UART0 (115200) from `hid open` through the `flash in 3s` window into the death. Expected line sequence on a Death-B repro:

1. `hid open. host <bdaddr> (flash in 3s)` (immediate, RAM update only)
2. ~3 s later: `store begin`
3. then either `store dt_us=<N> rc=<R>` or nothing before the WDT fires (~2 s watchdog).

Decision table:

- **`store begin` prints, `store dt_us` NEVER prints, WDT death follows** → **stall INSIDE the write** (e.g. bank erase / flash op never returns; WDT fires mid-`tag_store_safe`). Next boot banner shows `wdt=1` with no `dt_us` line above it.
- **Both lines print, WDT death follows ~2 s after the `dt_us` line** → **post-write corruption** (the flash op returned; something the write changed — TLV metadata, XIP window, BTstack state — kills the system later). The ~2 s gap is the discriminator: it matches the established 4/4 hard-evidence delay.
- **`store begin` + `dt_us` print and NO death** → deferred write is innocent in this instance; Death-B needs another trigger (compare against W1 outcome).

`dt_us` magnitude classes (single-shot `time_us_32` delta around `tag_store_safe`):

- **<1 ms (`dt_us` < 1000)** — program-like: a clean page program with no erase (fast path, TLV found a pre-erased slot).
- **tens of ms (roughly 10,000–100,000)** — erase-like: a 4 KB sector erase plus program (normal TLV bank maintenance).
- **hundreds of ms and up** — pathological: flash op struggling (contention with XIP fetch / Core1 lockout delay / repeated erase). Note the ceiling: any in-write stall longer than the 2 s WDT window resets the chip *before* the `dt_us` line can print, so a printed `dt_us` near/above ~2,000,000 still means "returned, then died" → post-write bucket, while a missing `dt_us` line is the stall-in-write signature regardless of cause.
- **`rc`**: expect `rc=0`. Any non-zero `rc` means the store failed (early return, no `host saved (n=)` line, no counter bump) — do not interpret that run as either bucket; report the `rc` value.
