# Task Plan: Contribute safely to Leonida

## Goal
Continuously identify useful, non-duplicative improvements for Leonida, review or implement them under its coordination rules, and deliver confirmed findings or pull requests while preserving the project's data and licensing boundaries.

## Next Step
Review the Phase 17 local CLI/worker plan; after approval, implement its slices in order. In parallel, monitor PR #3441 and other open PR checks/reviews.

## Current Phase
Phase 17

## Phases

### Phase 1: Requirements & Discovery
- [x] Understand user intent: contribute to Leonida and compare local AnyXboxSeries and xbox-emulator for missing capabilities
- [x] Identify constraints: claim gated tasks, check live PRs/branches, synthetic inputs only, no code copying, no game/shader/key/decryption material
- [x] Document in findings.md
- **Status:** complete

### Phase 2: Coordination & De-duplication
- [x] Read the universal coordination ledger and current tracker reference
- [x] Record distinct assignments and completed review outcomes
- [x] Create persistent plan for follow-on contribution work
- **Status:** complete

### Phase 3: Contribution Work
- [x] Revalidate and close the #1840 candidate as pre-existing and unreachable through the supported producer
- [ ] Collect remaining reports from active chats and assign only non-overlapping follow-up work
- [x] Finish the claimed XMemVirtualAlloc and write-watch exports on the isolated source-only branch
- [x] Implement the claimed DXGI GetDevice parent-device follow-up with a synthetic regression case and a separate PR
- [x] Compare local AnyXboxSeries and xbox-emulator behavior, check current Leonida coverage/licensing, and select one non-overlapping XMem feature
- **Status:** in_progress

### Phase 4: Verification
- [x] Recheck current PR heads, issue/tracker status, and review activity before public actions
- [x] Use static analysis and existing CI status; do not run or add tests unless the user explicitly asks
- [x] Record what was and was not verified
- **Status:** in_progress (static review complete; awaiting upstream CI/reviewer)

### Phase 5: Delivery
- [x] Update the universal coordination ledger after milestones
- [x] Link posted review comments or pull requests and report remaining blockers
- **Status:** in_progress (PR open; awaiting upstream review/CI)

## Decisions Made
| Decision | Rationale |
|----------|-----------|
| Keep one coordinator as sole writer of the universal ledger | Avoid conflicting task assignments between chats |
| Treat reviews separately from claimed implementation tasks | Project claim gate applies before code changes; avoid duplicating existing work |
| Do not copy code from AnyXboxSeries or xbox-emulator | Use only independently validated ideas and respect license boundaries |
| Local C++ test/build unavailable | Synthetic regression added for the requested validation; this environment has no cl.exe, CMake, or Ninja, so CI is the remaining execution gate |

## Errors Encountered
| Error | Resolution |
|-------|------------|
| check-complete validation failed | Existing phase headings/status markers are not in the native format; left the prior plan structure intact for owner review instead of rewriting its history. |
| GitHub connector comment endpoint returned 403 | Verified gh auth through the installed absolute path and posted the authorized comment; verified it through GitHub. |
| Exact-text planning patch failed to match the XMA2 status line | Re-read current plan state and used a guarded prefix-based update; the task plan write was preserved and the other files were updated separately. |
| Focused PR #3486 test command initially had no checkout | Fetched the confirmed PR head, created an isolated worktree, and reran the tests. |
| Full PR #3437 test file had one failure in the existing extensionless-path test on this Python version | Reproduced the same failure on base f847748c; did not attribute it to PR #3437. |


### Phase 6: Review and continued contribution
- [x] Revalidate and release RT-167 after documenting the SDL video/window-thread blocker
- [x] Review #2370 at the current head; run focused synthetic tests and submit an approval
- [ ] Follow #2199, #2343, and #2370 through CI and maintainer review; respond to any concrete feedback
- [ ] Find the next unclaimed, non-overlapping synthetic-only task in current tracker #2300
- **Status:** in progress; all three PRs are open and their checks are queued

### Phase 7 — close XMem test coverage feedback
- [x] Add permanent synthetic coverage for the three XMem virtual-memory/write-watch exports
- [x] Build and run the registered shim test with MSVC; 35 checks passed
- [x] Push the follow-up commit and update PR #2199 verification/feedback
- [ ] Monitor CI and maintainer review on #2199, #2343, and #2370; handle concrete feedback
- [ ] Continue from the live tracker only after claim and overlap checks

### Phase 8 — independent review while CI is queued
- [x] Re-read rollover tracker #2516 and confirm no eligible code task without hardware/first-refusal constraints
- [x] Review the claimed BUILD-643 PR #2549; run its synthetic suite and a negative control; submit an approval
- [ ] Monitor checks and maintainer decisions on #2199, #2343, #2370, and #2549
- [ ] Resume implementation after a newly verified claimable synthetic-only task appears

### Phase 9 — current tracker and independent review
- [x] Follow the rollover from #2516 to current tracker #2716 and recheck the unclaimed list.
- [x] Review claimed PR #2699 / BUILD item `psin_index` at head `e82833328f2284ead938db102294b7f9698ad7dd`; standards/spec passes found no actionable defect and an APPROVED review was submitted.
- [x] Recheck #2199, #2343, #2370, #2549 and #2566; all remain open and `UNSTABLE`.
- [ ] Follow checks and maintainer feedback on the open PRs; do not merge while checks are pending or absent.
- [ ] Select a newly eligible, synthetic-only task only after re-reading #2716 and verifying no claim, PR, branch, checker or review overlap.
- **Status:** active; #2716 currently lists only GPU/hardware, first-refusal, own-package, or texture-dependent unclaimed work. No new code task was claimed.

### Phase 10 — independent review of synthetic BVH fix
- [x] Verify PR #2760 has no formal reviewer/checker and compare active same-file PR hunks; the `triangle_test` hunk is isolated.
- [x] Review #2760 against the #2716 claim and project standards; submit APPROVED review at the exact current head.
- [x] Recheck tracker #2716 and all seven monitored PRs; retain the no-merge rule while checks are pending or absent.
- [ ] Continue monitoring PR checks/reviews and reselect only after fresh claim/overlap checks.
- **Status:** active; #2716 currently has 8 unclaimed rows, none eligible for synthetic-only coding under this scope (hardware, first-refusal, own-package, or texture-dependent).

### Phase 11 — refresh claims and PR readiness
- [x] Recheck tracker #2716; avoid BUILD-823 because @tupacfan04 claimed it and opened #2890.
- [x] Review #2890's changed source/test hunks statically; no actionable issue, no public review or duplicate code change.
- [x] Confirm #2199 local branch equals public head `84f86c5a7db842c9db53e4b5172277f317650ecb`; no push pending.
- [ ] Monitor required checks and maintainer decisions on #2199 and #2343; do not merge while checks are queued.
- [ ] Select implementation work only after fresh claim, branch, PR, checker and overlap checks.
- **Status:** active; no new code update was ready to push in this pass.

### Phase 12: RT-197 XMA2 decoder boundary and review follow-through
- [x] Reconfirm the live claim and tracker rollover to #3381 before continuing.
- [x] Push the isolated decoder boundary branch and open PR #3441 at head 83a15c2b91f1ebf9b44dfa12180d441ca0fc1a9e.
- [x] Verify the local branch is clean and matches the PR head; prior local Rust, Clippy, formatting, and MSVC ABI smoke checks passed.
- [x] Recheck the six current PR checks, tracker, comments, and formal reviews.
- [x] Request maintainer approval for the queued fork workflows in PR comment #7085434474.
- [ ] Monitor the six checks until they start and finish; fix any concrete failures and push a follow-up if needed.
- [ ] Respond to reviewer feedback and do not merge before required checks and maintainer review are complete.
- [ ] Continue the claimed RT-197 scope with the dependent XAudio2 source-voice adapter after validating the phase-1 interface and active overlap.
- **Status:** in_progress

### Phase 13: Current tracker refresh and independent synthetic review
- [x] Carry RT-197 progress forward to current tracker #3574 without duplicating the existing claim.
- [x] Recheck PR #3441: exact head 83a15c2b91f1ebf9b44dfa12180d441ca0fc1a9e; six checks remain queued; @kapitche's independent Windows ABI validation is recorded.
- [x] Post and verify tracker status comment #7086036830.
- [x] Preflight BUILD-970 and code-read candidates; avoid BUILD-970 after fix #2453 and another contributor's claim; avoid PRs with active checkers.
- [x] Run the three new synthetic tests from PR #3437 and 50,000 deterministic list2cmdline round trips; stop public review after an independent test report appeared.
- [x] Review PR #3486 at head 327746cc; affected modules and full semantic discovery passed with no ResourceWarning. During the long run, the PR advanced to 6b7833b9 after another checker found unbounded atexit callback registration; the author fixed it and multiple independent checks followed. No duplicate review was posted.
- [ ] Continue monitoring PR #3441 and advance the RT-197 adapter after the boundary review/CI gate; refresh the tracker before any new task.
- **Status:** in_progress

### Phase 14: Cross-chat coordination and current contribution queue
- [x] Discover tracker rollover to #3918 and revalidate PR #3831 at head `c13f3ce1ee0168332031870b607ae6fe235f5896`; formal approval is present and required checks remain queued.
- [x] Reactivate 12 Leonida/AnyXboxSeries Codex chats with separate subsystem scopes and establish the shared local roster at `coordination-roster.md`.
- [x] Close D3D-165 and D3D-209 as already resolved; avoid SHADER-213 (owned/overlapping), BUILD-1127, PROC-196, and PR #3938 after checker activity.
- [x] Revise the watcher design to stay private/local in response to existing proposal #725 and document fail-closed, SQLite, claim verification, path, branch, rate-limit, and lease rules.
- [x] Verify `tests/test_i18n_index.py` has no open PR/branch claim, publish a new TEST claim in #3918 comment #7087309229, and assign that single synthetic check to the localization chat.
- [x] Finish independent review of the revised watcher plan; delegate local-only implementation to a fresh agent.
- [x] Resolve the local watcher review findings (1 P1, 4 P2) with fix round 1; final script AST syntax passes.
- [x] Complete round-5 list-item fence ownership fix and final scoped review; all findings are resolved and the reviewer cleared the changes.
- [x] Attempt the approved read-only live check; it failed closed with unknown because gh auth is invalid and api.github.com DNS is unavailable. Resume only after normal authentication/network recovery; keep the utility private.
- [ ] Follow the claimed i18n consistency check through implementation, validation, push, and PR.
- [ ] Record results from the four reactivated chats; all returned with no eligible independent work or a 403 claim blocker. Resume triage after write access or a new distinct candidate appears.
- **Status:** in_progress; watcher review is clean; live check correctly failed closed on invalid gh auth/DNS. BUILD-1159 claim attempt was rejected with 403; no claim or code change. Four separate chats were reactivated; #3831 and #3441 remain open.
### Phase 15: Publish PR #4430 reviewer fix
- **Status:** in progress; commit `2d0dbdf95a328d6f4f16c6ad0fa04c6b98a763ed` is pushed to the existing PR #4430 branch; the PR is open and mergeable.
- **Verification:** `python -I tests/test_i18n_index.py` passed (9 tests); `git diff --check` clean. CI and No Game Data are queued.
- **Next:** follow the checks and reviewer response; no other unpushed commits were found in the audited Leonida worktrees.
### Latest PR #4430 status recheck
- The PR remains open at head `2d0dbdf95a328d6f4f16c6ad0fa04c6b98a763ed`; CI and No Game Data are queued. GitHub's latest response is `mergeable=UNKNOWN` while it recalculates after the push.
- The earlier mergeable result was from the initial post-push query; use this later status until GitHub recomputes.



### Phase 16: Coordination & Validation Grid proposal v2
- **Status:** design v2 approved by Ed; implementation plan v2 created, awaiting plan review.
- Covers task leases, local pre-claim locking, validation-job queue, local worker enrollment, one-button setup flow, Cloudflare default/Vercel alternative, security/isolation, migration, outage behavior, ownership, and acceptance criteria.
- No code, public issue comment, deployment, or GitHub configuration was changed. The short maintainer-facing summary is included in the draft but has not been posted.
### Phase 17: Local Coordination CLI and safe worker
- **Status:** design v2 approved by Ed; implementation plan written at .planning/2026-11-22-leonida-contribution-and-audit/coordination-cli-worker-implementation-plan-v2.md; awaiting plan review before implementation.
- Scope: local CLI, pre-reserved tracker claims, SQLite reconciliation, real-time watch, immutable validation profiles, and sandboxed local workers. Public API/site and any tracker post remain separate and are not authorized by plan approval alone.
- No code, tests, GitHub writes, deployment, push, or PR was made in this planning step.