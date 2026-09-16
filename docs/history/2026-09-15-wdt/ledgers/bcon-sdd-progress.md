# SDD ledger — plan: docs/superpowers/plans/2026-09-14-bt-stabilization-ab.md

Workspace (outside repo, git-status-clean): C:\Users\moilo\AppData\Local\Temp\opencode\bcon-sdd\
BASE (git rev-parse HEAD at dispatch start): c366bf4716d3b049d95411dd9301043e447bdaa4
Spec authority: docs/handoff_bt_20260914.md. No reachable machine spec beyond it.

## Rulings
- Ruling: workspace lives outside the repo (Temp\opencode\bcon-sdd) instead of <repo>/.superpowers/sdd/<plan>/ — .superpowers/ is not git-ignored here and would pollute status-equality checks; editing .gitignore would itself dirty the tree. Cost if wrong: ledger path must be carried manually (noted in todos + messages).
- Ruling: stay on the main working tree, no git worktree — tree is already dirty per handoff, no worktree consent, tasks self-revert with backup copies. Cost if wrong: a failed restore could mix variant code into the tree; mitigated by backup+status-equality proof per task.
- Ruling: NO commits/pushes/PRs (handoff §7 binding). Implementer template step "commit" is overridden: report "no commits per plan constraint". Reviews judge uncommitted diffs captured in diff files. Cost if wrong: none for correctness; history stays linear.
- Ruling: implementers must NEVER run git checkout/restore/clean on tracked files (would destroy pre-existing uncommitted work). Restore only from task backup copies. Cost if wrong: loss of handoff-state work.
- Ruling: Task 2 Spot A (add `extern uint32_t probe_out_report_count;`) DROPPED — src/bt/hid.h:31 already declares it; main.c includes bt/hid.h. Single-spot change only. Cost if wrong: duplicate extern is harmless anyway; verifier (compile) decides.
- Ruling: single available subagent tier ("general") used for all implementer/reviewer seats; no model selection possible in this harness. Cost if wrong: slower/cheaper mismatch; tasks are mechanical enough.
- Ruling: verification for these trial-patch tasks is wireless-build compile + status-equality proof, NOT unit tests (nothing about trial UF2s is unit-testable desk-side; host tests untouched). Reviewers must not flag absent unit tests as defects here.

## Pre-flight conflict scan
| Pair / Task | Produces vs consumes | Finding |
|---|---|---|
| T1 vs T2 (share src/main.c) | Each produces UF2+diff; neither consumes the other's output; sequential + restore | Clean, given restore discipline |
| T1 vs T4 (share src/main.c) | Same as above | Clean, given restore discipline |
| T2 vs T4 (share src/main.c) | Same as above | Clean, given restore discipline |
| T3 vs others (link_conn.c only) | No shared files | Clean |
| T5 vs others | No source change; shares temp build dirs? No — distinct $env:TEMP/bcon-abN dirs | Clean |
| T2 self-check | Brief Spot A vs hid.h:31 | Conflict found → ruled (drop Spot A) |
| T4 self-check | Flag persists across clean BT restart (noted limitation in plan) | Noted, acceptable for trial patch |
| Global constraints vs review rubric | "No unit tests" vs test-hygiene rules | Ruled: compile+restore proof is the specified verification |

## Progress
Task 1: complete (no commits; UF2 log/pico-bcon-ab1-sniff.uf2 819200B; diff diffs/ab1-sniff.diff one-line; restore SHA256 match 20CED944; review clean; controller verified HEAD==BASE, status==handoff baseline)
Task 2: complete (no commits; UF2 log/pico-bcon-ab2-slowstart.uf2 819200B; diff diffs/ab2-slowstart.diff single-spot; restore SHA256 match 20CED944; review clean with 1 deferred minor: gating var counts all outputs not SUB-only ? real but not load-bearing for trial; controller verified HEAD==BASE, UF2 present)
Task 3: complete (no commits; UF2 log/pico-bcon-ab3-passive.uf2 818688B; diff diffs/ab3-passive.diff 2-hunk via no-index after fix round 1/5; restore SHA256 match C24E0EE7; re-review verified yes; lesson: untracked-path diffs need --no-index against backup; controller verified HEAD==BASE, UF2 present, hash match)
Task 4: complete (no commits; patch-only diff diffs/ab4-reguard-patchonly.diff 27 lines exact + full diffs/ab4-reguard.diff; patch-proof UF2 log/pico-bcon-ab4-reguard-PATCH-PROOF.uf2 819200B; restore SHA256 match 20CED944; review clean with 1 inherent minor: flag never resets, trial-only by design; controller verified HEAD==BASE, UF2 present, hash match)
Task 5: complete (no commits, no source edits; UF2 log/pico-bcon-ab5-bump0.uf2 819200B; status files byte-identical; wipe-still-active note present; review clean; controller verified HEAD==BASE, UF2 present, status==handoff baseline)
Task 5: complete (no commits, no source edits; UF2 log/pico-bcon-ab5-bump0.uf2 819200B; status byte-identical; review clean; controller verified HEAD==BASE, UF2 present, status==handoff baseline)
Final review: ready for hardware phase (all 5 artifact sets single-variable, restores byte-proven, constraints held; 2 deferred minors triaged safe-to-defer; 2 Important runbook notes parked, no code defects, no second fix wave)
Ruling: finishing-a-development-branch SKIPPED ? no branch/commits exist by design and hardware phase is pending; nothing to integrate. Workspace RETAINED at Temp\opencode\bcon-sdd for the hardware phase. Cost if wrong: none; ledger persists.
Phase 1 result (COM3_2026_09_14.21.59.07.864.txt): Case C confirmed at wire level x4 cycles ? both PSM open status 0, 1-2x A1 00 empty TX, ZERO incoming ACL data post-open, Switch disc 0x13 after ~1.1s. SUB never on the wire (not a receive-path drop). Fresh SSP key each cycle, non-bonding, no LINK_KEY_REQUEST. No outgoing page fired (reconnect never armed: host unknown at boot). Next: AB1 flash per plan; consider AB5 before AB3 since wiped scenario is already effectively passive.
Phase 2 result AB1 (COM3_2026_09_14.22.32.18.050_ab1.txt): VICTORY x2 sessions ? full SUB handshake (0x02/0x08/0x10/0x03->0x30 mode/0x04/0x40/0x30/0x21/0x48), RUMBLE intake, steady A1 30 streaming to log end, zero Switch disconnects. Mid-log reboot manual (wdt=0, SCR stale, no fault). Single-variable isolation clean vs 4:24 (only link-policy line differs).
Phase 2 result AB5 (COM3_2026_09_14.22.36.22.184_ab5.txt): FAIL x4 cycles ? open, no SUB, disc 0x13 @~1.1s, ssp=4. Stable MAC alone insufficient. Conclusion: SNIFF acceptance is necessary for Switch 2 SUB progression.
