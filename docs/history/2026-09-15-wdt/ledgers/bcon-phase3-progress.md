# SDD ledger — plan: docs/superpowers/plans/2026-09-14-phase3-permanent.md

Workspace (outside repo, git-status-clean): C:\Users\moilo\AppData\Local\Temp\opencode\bcon-phase3\
BASE (HEAD at start, no commits expected until T8 gate): c366bf4716d3b049d95411dd9301043e447bdaa4
Spec authority: docs/handoff_bt_20260914.md + AB1 victory log (log/COM3_2026_09_14.22.32.18.050_ab1.txt).
Prior work: Temp\opencode\bcon-sdd\ (A/B variants; AB1 victory, AB5 defeat; AB3/AB4 diffs on file).

## Rulings
- Ruling: workspace outside repo (Temp\opencode\bcon-phase3), same as A/B execution — .superpowers/ is not git-ignored. Cost if wrong: manual path carrying.
- Ruling: stay on main working tree, no worktree — tree dirty per handoff, no consent; Phase 3 changes ACCUMULATE (permanent, no per-task revert unlike A/B). Cost if wrong: task interference; mitigated by sequential dispatch + hunk-discipline reviews.
- Ruling: NO commits until Task 8's explicit approval gate (handoff §7). Reviews judge cumulative uncommitted diffs scoped to brief-specified hunks. Cost if wrong: none for correctness.
- Ruling: single available subagent tier ("general") for all seats; no model choice in this harness.
- Ruling: HARDWARE confirmations are OWNER-executed in one batch after T1–T7 code completion — subagents cannot flash/test physical hardware. Implementer briefs require code + compile (+host tests where specified) + a 5-line flash/test note instead. Behavioral verification therefore lands late by design; risk mitigated by single-variable minimal diffs on the AB1-proven baseline. After user returns HW results, T8 runs as summarizer + commit gate. Stopping for the HW batch is an external dependency, not a plan defect.
- Ruling: T6 test-first edit (test_usb.c expectation) precedes the spi.c default change within the same task; RED proof is the task's own failing run.

## Pre-flight conflict scan
| Pair / Task | Produces vs consumes | Finding |
|---|---|---|
| T1 vs T2 (share main.c) | T1: link-policy hunk; T2: open-handler call-site hunk. Distinct regions | Clean |
| T1 vs T3 (share main.c) | T1: policy; T3: wipe-block deletion + auth-case addition. Distinct | Clean |
| T1/T2/T3 vs T4 (main.c + hid.c) | T4 removes diagnostic lines incl. near open-handler; T2 rewrites that call site first. T4 brief keeps "a plain hid open line" = T2's new line | Clean, T4 implementer must read post-T1–T3 tree (dispatched sequentially, always current) |
| T2 (store.c) vs others | store_host touched only by T2 | Clean |
| T3 (auth case) vs T7 (reconnect) | T3: key drop on auth-fail; T7: armed-flag lifecycle. Both touch disconnect-adjacent logic but different functions | Clean; T7 brief carries pointer to T3's handler |
| T5 (CMakeLists/link_conn.c) vs others | No shared files | Clean |
| T6 (spi.c/test_usb.c) vs others | No shared files; shared table read-only for others | Clean |
| T7 vs T3 | T7 adds flag cleared in link_note_disconnected; T3's auth-drop calls gap_delete (no interaction with flag) | Clean |
| T5 self-check | Step 1 outcome branches (win→delete knob; lose→bake ...:cb). Both delete the knob | Clean |
| T4 self-check | Keep-list (WDT/Fault/MSPLIM + neutral obs logs) vs remove-list — no overlap by construction | Clean |
| T6 self-check | Test RED then GREEN within one task; USB+BT share table so one change covers both | Clean |
| Rubric vs plan | T8 "no code + ask approval" is process, not a defect. Trial-patch tasks verify via compile+HW note, not unit tests (except T6) | Noted as lens, not findings |

## Progress
Task 1: complete (no commits; src/main.c link-policy hunk verbatim; build exit 0; review clean; controller verified HEAD==BASE, status==handoff set + T1 hunk, wakecon untouched)
Task 2: complete (no commits; store.c guard+counter, main.c call-site re-enabled; 2 includes compiler-required accepted; brief bd_addr_to_str claim corrected with no impact; build exit 0; review clean; controller verified UF2 present, HEAD==BASE, status==handoff set)
Ruling: T3 targeted-drop replaced by delete-all (option A) ? AUTH_COMPLETE carries no peer address; reaching into hci internals not worth it for a 1-key DB. Cost if wrong: drops all keys on any auth fail (identical behavior in practice).
Task 3: complete (no commits; wipe deleted, auth-fail drop added verbatim; 1 existing line touched behavior-identically, disclosed; build exit 0; review clean; controller verified disk presence, wipe absence, UF2 fresh, HEAD==BASE, status==handoff set)
Task 4: complete (no commits; hci_dump+SCR+heartbeat+markers removed, safety infra kept+reworded, banner neutralized, 5-file wording rewords; build exit 0 + host 3/3; review clean; controller ran Step-6 proof: ZERO hits src-wide, link_conn.c mechanism intact with Task-4-free comment)
Ruling: T5 deletes the BUMP mechanism now (pure board-ID MAC = BUMP=0 behavior) without waiting for HW ? AB5's failure is fully explained by SNIFF-off since AB1 isolated SNIFF as the variable; HW batch acceptance covers ...:ca SUB arrival, with a scoped bake-...:cb fallback if it fails. Cost if wrong: one 2-line fix task.
Task 5: complete (no commits; BUMP mechanism fully removed ? controller grep zero hits; both builds exit 0; verify UF2 log/pico-bcon-p3t5-bump0-verify.uf2 816640B; review clean; controller verified UF2 present, HEAD==BASE)
Ruling: untracked opencode.json (MCP pyserial config, created 23:14, likely harness tooling for COM capture) is EXTERNAL ? leave in place, exclude from any commit list. Not attributed to task subagents (firmware briefs could not produce it). Cost if wrong: a stray config file committed; mitigated by explicit exclusion.
Task 6: complete (no commits; test-first RED 1-fail then GREEN 3/3; spi.c stock-black table byte-exact, byte12 preserved; wireless exit 0; review clean with 2 deferred documentary minors: report hunk line-counts, missing run timestamps)
Task 6: minor (deferred): report hunk line-counts inconsistent; missing RED/GREEN timestamps ? documentary only, no functional impact
Ruling: T7 uses remove-before-add at all 4 arm sites instead of the plan's armed-flag ? flag lifecycle cannot be made airtight (pending-timer + BT-restart interleavings); remove is a safe no-op when unscheduled, making double-add structurally impossible with no new state. Implementer must confirm btstack_run_loop_remove_timer exists in btstack_run_loop.h, else BLOCKED. Cost if wrong: none (compile + review verify; HW covers behavior).
Task 7: complete (no commits; remove-before-add at all 4 sites, intervals/conditions identical, no new state, API btstack_run_loop.h:253 confirmed; build exit 0; review clean; controller verified HEAD==BASE, status==intended set, isolated temp dirs, build/ untouched)
Final review: code-ready for hardware batch (all 7 hunks coherent, untracked changes narrow+verified, no secrets, no silent behavior changes; 1 new pre-existing minor noted: test_usb.c:129 comment swallows memset line ? host-test only, not gating)
Phase3 test UF2 staged: log/pico-bcon-phase3-test.uf2 (from TEMP\p3t7 build, post-T7 tree). Awaiting owner HW batch; T8 summary+gate after results.
Task 6R: complete (no commits; exact reverse of T6 ? spi.c table + test expectation back to originals, jc_toolkit comment removed; RED 1-fail then GREEN 3/3; wireless exit 0; git diff for both files EMPTY = byte-exact restore, controller-verified values on disk; review-by-evidence per ruling: empty diff is the proof)
Ruling: T6R reviewed by controller evidence instead of subagent review ? the deliverable is byte-identity with HEAD, proven by empty git diff + on-disk grep + reported suite. A reviewer would only re-read the same bytes. Cost if wrong: minimal (host suite + wireless build both green per report).
T6V (owner-directed, controller-executed): L/R grips made distinct for verifiability ? L #464646 (stock), R #FFFFFF; body/buttons unchanged from originals. Host suite 3/3 PASS with extended L/R assertions; wireless build exit 0; UF2 log/pico-bcon-colors-verify.uf2. Rationale: identical L/R values make byte-order bugs invisible.
