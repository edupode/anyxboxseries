# Leonida cross-chat coordination roster

Updated: 2026-11-23 05:41 UTC

This file coordinates the Codex chats working from this local workspace. It is a temporary bridge until the local watcher is implemented. GitHub's current coordination issue remains authoritative for public claims, ownership, and project status. Discover the newest open issue titled `Coordinating who works on what` before every new task; the latest live check at 05:28 UTC shows #4816 open. Claims from #4683 carry over. The snapshot below supersedes older dated status entries; refresh GitHub before assigning work.

## Rules for every chat

1. Read this roster and refresh the current tracker, CONTRIBUTING.md, open PRs, branches, reviews, comments, and changed paths before claiming or editing.
2. Claim the exact task in the current tracker before code changes when the project asks for it. A roster entry does not replace a public claim.
3. Do not take a task already owned, under first refusal, checked by another contributor, or changed in the same hunk by an active PR. Ask the coordinator if path overlap is ambiguous.
4. Keep work synthetic and source-only. Do not access or add game data, archives, executable contents, assets, generated shaders, logs quoting them, keys, or decryption material.
5. Before editing, append the exact issue/task ID, paths, branch, and status here. After a push or PR, append the SHA and links. Never rewrite another chat's entry.
6. Do not merge while required checks/review are pending. Do not publish claims, reviews, or PRs for a different chat's task.

## Coordinator and active work

| Owner / chat | Scope | Status / reserved task |
|---|---|---|
| Main chat 01a2020d-43d5-7ec3-a9e8-d56142dd6f82 | Three-workstream proposal; private coordination CLI; PRs #4788 and #4837 | Requests posted to #725 and #3828. Current tracker #4816. Local CLI Task 1 is independently approved and copied; Tasks 2–7 remain. PR #4788 is open at 261495ccac1c62ac22fb833d5c26c565b5750fe9. PR #4837 is open at 68332df3fe16e0652274490683c9a4894cbfa1f7; CI and No Game Data queued at 05:40 UTC. New TEST claim #7092469574 is fulfilled by #4837. Do not merge with checks pending. |
| Subagent `/root/coordination_watch_implementer` | Private local coordination watcher | Round 5 list-fence case addressed by returning ambiguous when parsing is uncertain. Final independent review is clean; AST passes. Live check returned unknown because local gh auth is invalid and api.github.com DNS failed; no tests or watch loop ran. |
| Subagent `/root/ci_workflow_candidate` | Build / CI candidate | Complete: no free CI/build task. BUILD-1127 is in #3972; PROC-196 has first-refusal/overlap in #3974; TEST-873 is in #3978. No claim/edit. |
| Subagent `/root/d3d165_present` | D3D-165 Intel UHD 770 present failure | Complete/resolved by merged PR #1696; tracker no longer lists it. End-to-end UHD residual is distinct and needs separate diagnosis; no duplicate claim. |
| Subagent `/root/d3d209_occlusion` | D3D-209 occlusion issue | Resolved by merged PR #1825; no active claim/PR. Post-merge checks show failures, but logs were not inspected or attributed. No new claim/edit. |
| Subagent `/root/local_audio_scan` | CPU/synthetic PR review | SHADER-213 is owned by @mrp1no and overlaps PR #3544. Stopped scoped review of PR #3938 at new checker comment #7087190394 on the same hunk; review incomplete, no finding/no public review. |
| Subagent `/root/xma2_standards_review` | Watcher plan adversarial review | Complete: final plan approved after repo scope, claim URL, path normalization, branch unknown, and lease ID rules were fixed. |
| Subagent `/root/coordination_watch_review` | Watcher script review | Round 5: list-item fence uncertainty returns ambiguous, preventing false clearance. Final scoped review clean; watcher live --check pending. |
| Subagent `/root/xma2_spec_review` | Synthetic audio/build candidate search | No eligible audio task found; complete, no claim or edits. |

## Reactivated Codex chats

No task ID is reserved by this roster until its owner records a verified claim below.

| Thread ID | Distinct scope | Current status |
|---|---|---|
| `01a2037c-4a85-75e0-b70d-b2459c440107` | Existing unreviewed PR audit | Static pass on claimed PR #3966 / BUILD-1144 found no concrete issue; no tests or public review. Excluded #3923 after a new PR comment and #3922 due to overlap with BUILD-1032 in `viewer/serve.py`. |
| `01a2037c-682d-7d01-bb8c-7d93e30eb417` | xbox-emulator kernel / guest API compatibility | Complete: no safe new task while #3390, #3601, and #2401 overlap `runtime/shims/xmem.cpp`; original #2199 is already merged. No claim/edit. |
| `01a2037c-55bc-7461-87aa-b3d563b5169e` | AnyXboxSeries queue / fence behavior | No eligible independent task found. Excluded active claims and PRs #2951, #3789, #3841 and RT-196; no claim/code. |
| `01a2037c-5c24-7d30-b7d6-54b5de7ed51a` | Non-D3D CPU-only task audit | Complete: no eligible CPU/synthetic work. Mutation-test finding overlaps #3883; BUILD-1129 overlaps scrub() hunk in #2681. No claim/code/tests. |
| `01a2037c-5008-7670-b4ec-5215c1dee169` | AnyXboxSeries root-argument state | No new task. Main differs from WARP when setters run before a root signature; PR #3933 measures it and the tracker discussion awaits a design decision. No claim/code. |
| `01a2037c-6216-74e1-83b1-894a55f3fd3f` | AnyXboxSeries resource / descriptor lifetime | No new task. D3D-286 was already fixed by merged #1898; adjacent D3D-203/#1920/#2740/#2756 have active owners/PRs. |
| `01a2037c-6d57-7320-b199-9095f5c0776e` | xbox-emulator graphics backend | No independent task; related texture aliasing #770, queue waits #2951/#497, resource lifetime #1920, and swapchain #3551 have active work. |
| `01a2037c-7a37-73b2-90f8-0a2fd1b9d472` | Synthetic test harness / error reporting | Reactivated; candidate search in this subsystem. |
| `01a2037c-72f1-7ad2-a458-d60c7bfc1a17` | Build / tests / CI / licensing | No free task: BUILD-1034 overlaps #3515, TEST-533 overlaps #3606, TEST-869 is claimed. No edit/push. |
| `01a2037c-10b4-7311-a7fb-5a4015da09b0` | Docs / localization follow-up to #1810 | New TEST item claimed by @edupode at [#3918 comment 7087309229](https://github.com/ItsNotRexed/Leonida/issues/3918#issuecomment-7087309229), path `tests/test_i18n_index.py` only. Implement this exact synthetic index/quickstart commit-marker consistency check; local commit `52ab757f97425b524a8b45f5cac6c11b9e5de675` passed 8 focused checks and diff-check, but upstream and fork pushes are blocked by missing OAuth workflow scope in inherited workflow history. Do not bypass this scope gate; avoid shared docs index paths #3872/#3887. |
| `01a20262-17c8-70b1-91c0-80729a1ba563` | Local AnyXboxSeries + xbox-emulator cross-reference | No eligible task. D3D-286 is already fixed by #1898; TEST-875 overlaps queue/fence work; swapchain diagnostics overlap harness/error-reporting work. |
| `01a20262-100b-7761-bcbd-c60f8270cfc8` | Global tracker / PR inventory | No free CPU/synthetic implementation task. PR #4022 / TEST-881 review stopped before diff: duplicate claims unresolved, checks queued, no code review completed. Other PR audit chat excludes this path. |

## Verified task reservations

Append new reservations here in chronological order. Required fields: timestamp, thread/agent, exact tracker ID, active PR/branch links if any, changed paths, and public claim link. Do not treat `finding:` alone as an ownership claim.

### 2026-11-22 22:52 UTC — synthetic tiny-game exit-code diagnostics
- Thread: `01a2037c-7a37-73b2-90f8-0a2fd1b9d472`
- Tracker/task: #3918, new item — make the synthetic tiny-game exit-code checker report which Win32 API failed and preserve its `GetLastError()` value; cover wait and `GetExitCodeProcess` failures with synthetic tests.
- Public claim: https://github.com/ItsNotRexed/Leonida/issues/3918#issuecomment-7087141066
- Exact commit path verified: `runtime/test/tiny_game_exit_code_test.cpp` only. `.github/workflows/ci.yml` is not in the commit; the branch inherits upstream workflow history because fork/main is 4,815 commits behind upstream, and the OAuth token lacks `workflow` scope. No permitted push or PR.
- Branch: `codex/tiny-exit-code-diagnostics`
- Status: claimed; local commit `26ca9529bd3865c5d355470ee844c0381c529569`; build and two focused CTests passed. Push blocked because this commit changes `.github/workflows/ci.yml` and the gh credential lacks `workflow` scope. Await user approval; do not bypass. No PR.

## Read-only candidate scan (not a reservation)

- 2026-11-22 22:54 UTC — thread `01a20262-100b-7761-bcbd-c60f8270cfc8`; candidate **TEST-881** from current tracker [#3918](https://github.com/ItsNotRexed/Leonida/issues/3918), also references the finding on #3574. Synthetic CPU-only test-gap candidate: add a 16-bit fragment-length boundary case to `tests/test_bcpack.py`; no game data or GPU required. Current #3918 body includes it under “Up for grabs”; no `claiming: TEST-881` or matching PR found in comments after the body snapshot.
- Path/PR check: open #3094 changes `tools/bcpack.py`, `tools/bcpack_history_calib.py`, `tools/bcpack_tables_bptc.py`, `tests/test_bcpack_history_calib.py`, and `tests/test_bcpack_tables_bptc.py`; it does not change `tests/test_bcpack.py`. Open #3877 and #3816 are docs/README work, not this test path. No matching upstream branch was returned for `bcpack`.
- Status: **not reserved, no public claim, no code edit**. Potential adjacency only: if the boundary case requires changing `tools/bcpack.py`, re-check #3094 and ask the coordinator before coding. This candidate is distinct from the other CPU-only audit’s reported `tests/semantic/test_mutations.py` and `tools/progress_dashboard.py` paths.

- 2026-11-22 22:57 UTC status refresh for this thread: TEST-881 was claimed by @JavierBlanco23 at [comment](https://github.com/ItsNotRexed/Leonida/issues/3918#issuecomment-7087177482), with a duplicate claim by @ofc0urse at [comment](https://github.com/ItsNotRexed/Leonida/issues/3918#issuecomment-7087185043); Javier posted `done: TEST-881` and opened [PR #4022](https://github.com/ItsNotRexed/Leonida/pull/4022). PR #4022 is open, not merged, on `test-881-bcpack-length-boundary`, changes only `tests/test_bcpack.py`, and has no submitted reviews yet. **Do not assign this candidate.** At the latest #3918 read (22:56 UTC), the remaining up-for-grabs rows are Intel Arc GPU tasks; RT-262 has first refusal for @AAyberkU. No distinct CPU/synthetic candidate remains free in the current tracker snapshot. No claim or code edit by this thread.

### 2026-11-22 23:09 UTC — locale index sync-marker regression test
- Thread: `01a2037c-10b4-7311-a7fb-5a4015da09b0`
- Tracker/task: #3918, new TEST item — verify that the source commit recorded in each locale index row matches the commit marker in the linked quickstart, with synthetic match/mismatch cases.
- Public claim: https://github.com/ItsNotRexed/Leonida/issues/3918#issuecomment-7087309229
- Paths: `tests/test_i18n_index.py` only
- Branch: `codex/i18n-index-marker-test`, based on `main` at `aa2988cc555bad7ecd508b48a702adbb32fe7c95`
- Status: claimed; checkout and active-path checks passed; pre-edit.

### 2026-11-22 23:19 UTC — locale index marker test local result
- Thread: `01a2037c-10b4-7311-a7fb-5a4015da09b0`; task claim: #3918 comment 7087309229.
- Local commit: `52ab757f97425b524a8b45f5cac6c11b9e5de675` on `codex/i18n-index-marker-test`.
- Checkout: `C:\Users\edupo\Documents\Codex\2026-11-22\leonida-i18n-index-test`; only `tests/test_i18n_index.py` changed.
- Validation: `python -I tests/test_i18n_index.py` — 8 passed; `git diff --check` clean.
- Status: local commit prepared; no upstream branch or PR. Push to upstream was denied; fork push and fork sync were denied because the OAuth token lacks `workflow` scope for upstream `.github/workflows/ci.yml` history. No workflow files were edited.

### 2026-11-22 23:20 UTC — coordination watcher fix round 2
- Task: private local watcher review follow-up; no public tracker claim is applicable because this artifact does not modify Leonida or its contribution process.
- Implementer: `/root/coordination_watch_implementer`; scoped reviewer: `/root/coordination_watch_review`.
- Open findings: preserve distinct full free-form descriptions; recognize released free-form claims in `releasing:` commands; detect explicit task ownership in ordinary PR Markdown without treating incidental/dependency links as implementation ownership.
- Status: fix dispatched; do not run tests, watcher, or GitHub writes. Syntax/AST parse only after the agent reports the edit; independent review required before any live `--check`.


### 2026-11-22 23:40 UTC — coordination watcher fix round 3
- Round-2 review verdict: free-form description identity is addressed. Remaining findings: resolve status/release commands against the same author's prior names before interpreting suffix notes; bare PR-title mentions must stay ambiguous; ignore Markdown inline-code examples; only close a fenced block on a valid delimiter-only line.
- Implementer /root/coordination_watch_implementer is handling the changes. No tests, watcher execution, or GitHub calls; syntax AST only. Independent scoped review required before live --check.


### 2026-11-22 23:43 UTC — live PR/tracker refresh
- Current tracker rolled over to #4084, open, updated 23:40 UTC. No new task claimed.
- PR #3831 remains open/mergeable at c13f3ce1ee0168332031870b607ae6fe235f5896; CI, Runtime, and No Game Data workflows are queued.
- PR #3441 remains open/mergeable at 83a15c2b91f1ebf9b44dfa12180d441ca0fc1a9e; XMA2 decoder, Runtime, CI, No Game Data, and jactionlint workflows are queued.


### 2026-11-22 23:52 UTC — reactivated independent chat scopes
- Current tracker: #4084. Four separate read-only triage scopes were started; none may claim or edit code before a separate overlap check and coordinator confirmation.
- 1a20262-100b-7761-bcbd-c60f8270cfc8: synthetic CPU/test candidate audit only.
- 1a2037c-72f1-7ad2-a458-d60c7bfc1a17: build/CI candidate audit only.
- 1a20262-17c8-70b1-91c0-80729a1ba563: source-only AnyXboxSeries/xbox-emulator cross-reference.
- 1a2037c-6d57-7320-b199-9095f5c0776e: RT-269 graphics preflight only; local GPU reported RTX 5070 Ti Laptop, driver 621.01; do not reproduce without coordinator go-ahead.
- 1a2037c-4a85-75e0-b70d-b2459c440107 remains systemError; not counted as active. 1a2037c-10b4-7311-a7fb-5a4015da09b0 and 1a2037c-7a37-73b2-90f8-0a2fd1b9d472 have local commits but no public PR because inherited workflow history triggers missing OAuth workflow scope; do not bypass.


### 2026-11-22 23:58 UTC — coordination watcher review cap
- Round 5/5 final re-review is clean. Round 4’s three Markdown cases and round 5’s list-item fence case are addressed; no open findings remain. Preflight and watch share the same classifier. The fail-closed list-fence fallback may mark a real task declaration in that same body unknown.
- Implementation SHA-256: 5b6bb53728bdd2390b2c70f40aca4c72cca92bdf5c36e8dcb7118fc56944c9b1; review verified the hash. Previous round head: c8c50f6c5f1bc2c622f941a073a6794fba87bc9092feb8636cd6e368b22017d6. No Git HEAD exists in this private workspace.
- Verification: Python AST parse only; no tests, watcher, jq filter, or GitHub writes. One read-only check --task-id RT-269 is authorized next as a negative live conflict check.
- Task Coordination watcher: fix round 5/5 (1 open P1 addressed, 0 open; hashes c8c50f6..5b6bb53).

### 2026-11-23 00:01 UTC — live check and BUILD-1159 candidate
- coordination_watch.py check --task-id RT-269 returned unknown with gh auth failed (exit 1). Direct gh auth status reports the stored edupode session token is invalid and api.github.com DNS lookup failed. No token contents were read or exposed; no auth changes attempted. Retry only after normal gh auth login succeeds and network access is available.
- Build/CI chat found BUILD-1159 in #4084 at 23:53 UTC; its snapshot showed no claim, open PR or branch. Claim attempt was rejected with 403 Resource not accessible by integration; no comment was created. No code edits or tests. Do not start without a verified public claim.
### 2026-11-23 00:02 UTC — BUILD-1159 claim blocked
- The CI/build chat refreshed #4084 and CONTRIBUTING/AGENTS. BUILD-1159 still appeared unclaimed; no matching PR/branch or checker was found.
- GitHub MCP rejected the exact claim comment with 403 Resource not accessible by integration. The chat verified no comment was created and stopped before editing.
- No code or tests were run. The candidate is not claimed and must not be implemented until normal GitHub write access is restored and the claim is verified.
### 2026-11-23 00:05 UTC — status of reactivated chats
- Current Codex thread snapshot: the four reactivated triage chats have completed and are idle; the other prior project chats are also idle/notLoaded. Only the coordinator thread remains active. Local implementer and reviewer agents for the watcher are complete.
- Tracker/PR audit chat: no free CPU/synthetic test candidate. tools/manifest_verifier.py is claimed and overlaps open PR #4007; RT-277 has first refusal.
- Build/CI chat: BUILD-1159 was free at preflight, but its required claim was denied with 403 Resource not accessible by integration. No code edit/test/PR.
- Graphics chat: RT-269 is claimed by another contributor and PR #4185 is open; active path overlap and GPU mismatch make it unsuitable.
- Local cross-reference chat: no eligible independent idea; PR #4191 owns the nearest queue pattern, and D3D-444 has first refusal.
- PR audit chat: stopped before opening code when new review activity appeared on #4200; excluded #4078 after review activity. No finding or public post.
- No new public push or PR was made. Existing #3831 and #3441 remain open/mergeable with workflows queued. Local watcher is review-clean but the live check failed closed because local gh auth is invalid and DNS failed.
### 2026-11-23 00:11 UTC — fresh tracker and PR gate check
- Current tracker #4084 is open and was updated at 00:11 UTC. Its only TEST/BUILD/PROC up-for-grabs row found by the filtered metadata scan is TEST-57, requiring an Intel Arc GPU. No suitable local CPU/build task is available; the user's detected GPU is NVIDIA RTX 5070 Ti Laptop.
- PR #3831 remains open/mergeable at c13f3ce1ee0168332031870b607ae6fe235f5896 with an approval and independent Windows test reports. CI, Runtime and No Game Data are still queued.
- PR #3441 remains open/mergeable at 83a15c2b91f1ebf9b44dfa12180d441ca0fc1a9e. XMA2 decoder, Runtime, CI, No Game Data and jactionlint are still queued; an independent Windows ABI/Rust test report is present and a maintainer approval request is already posted.
- No safe push, merge, or new claim is available in this snapshot. Do not merge while checks remain queued.
### 2026-11-23 00:12 UTC — read-only review follow-up
- A static code review was assigned to thread 01a2037c-4a85-75e0-b70d-b2459c440107 for PR #3441 at head 83a15c2b91f1ebf9b44dfa12180d441ca0fc1a9e. Scope is only Rust/C++ implementation and synthetic tests; workflow, docs, license and pinned upstream source are excluded. No tests or public actions.
- PR #3831 and #3441 remain open/mergeable with workflows queued; no push or merge is available now.
- The latest filtered up-for-grabs scan found only TEST-57 in the synthetic test/build rows, requiring Intel Arc hardware. No eligible local task is available.
### 2026-11-23 00:32 UTC — ten resumed Leonida chats + RT-197 fix
- Current coordination tracker: #4257 (open); #4084 is closed and points to #4257. The existing RT-197 claim and PR #3441 remain the basis for the XMA2 work; no new claim was made.
- PR #3441 remains open/mergeable at `83a15c2b91f1ebf9b44dfa12180d441ca0fc1a9e`. Static review found two P2 defects in the C++ smoke under `NDEBUG` and wrapper-owned Rust FFI allocations. Implementation is assigned to `/root/rt197_boundary_fix` in the isolated `codex/xma2-audio` worktree; exact task brief is `.codex-rt197-brief.md`. Allowed files: `runtime/xma_decoder/src/lib.rs`, `runtime/xma_decoder/tests/ffi_cpp_smoke.cpp`, `.github/workflows/xma2-decoder.yml`; synthetic-only. No tests or edits completed yet; no push/review/comment.
- Ten Codex chats resumed with disjoint read-only assignments: `01a20262-100b-7761-bcbd-c60f8270cfc8` (tracker CPU/synthetic candidate scan), `01a20262-17c8-70b1-91c0-80729a1ba563` (AnyXboxSeries cross-reference), `01a2037c-4a85-75e0-b70d-b2459c440107` (PR #4250), `01a2037c-72f1-7ad2-a458-d60c7bfc1a17` (PR #3884), `01a2037c-6d57-7320-b199-9095f5c0776e` (PR #4087), `01a2037c-7a37-73b2-90f8-0a2fd1b9d472` (PR #4149), `01a2037c-5c24-7d30-b7d6-54b5de7ed51a` (PR #4252), `01a2037c-6216-74e1-83b1-894a55f3fd3f` (PR #3519), `01a2037c-5008-7670-b4ec-5215c1dee169` (PR #3534), and `01a2037c-682d-7d01-bb8c-7d93e30eb417` (PR #3537). Each must refresh #4257 and stop before diff if the target is stale, reviewed, or overlapped. They may not test, claim, edit, or post.
- The tracker audit and candidate cross-reference exclude RT-197/#3441. PR reviews have distinct PR numbers. Do not open any PR diff until its own live preflight passes.
- Local `gh` is not on PATH in this session. GitHub MCP reads work. No GitHub write, push, or merge has been attempted this turn.

### 2026-11-23 00:37 UTC — RT-197 implementation complete, review pending
- Commit `c2eb015cb0d6a3c2f31170d2f99becfb3cce4c66` (`fix: make XMA2 FFI failure handling reliable`) was created on existing branch `codex/xma2-audio`; the worktree is clean apart from the temporary independent-review package, which is untracked and must not be committed.
- Changed exactly `.github/workflows/xma2-decoder.yml`, `runtime/xma_decoder/src/lib.rs`, `runtime/xma_decoder/tests/ffi_cpp_smoke.cpp`.
- RED/GREEN evidence in `C:\Users\edupo\AppData\Local\Temp\Leonida-RT197-boundary-hardening-report.md`: two fault-injection unit tests failed before the fix and passed after; Rust 8/8 total; `cargo fmt`, strict Clippy, debug staticlib build and `git diff --check` pass. MSVC smoke with `/DNDEBUG`: deliberate invalid-rate negative case exits 1 as expected, valid synthetic silence smoke exits 0. All input is generated in memory.
- Independent task review assigned to `/root/rt197_boundary_review`; do not push before its verdict. PR #3441 is still open at the old head `83a15c2b91f1ebf9b44dfa12180d441ca0fc1a9e`, with no formal PR review. No public write/push yet. Hosted checks remain queued pending maintainer approval.

### 2026-11-23 00:40 UTC — reassignments, RT-197 review and publish blocker
- The independent review of `c2eb015cb0d6a3c2f31170d2f99becfb3cce4c66` returned Spec Compliance PASS and Code Quality PASS, with no Important/Minor findings. It confirmed the fallible global-allocation helper, preserved Box deallocation, release-active C++ checks, and `/DNDEBUG` CI wiring. Hosted CI remains unverified.
- Attempted push to existing fork branch `edupode/Leonida:codex/xma2-audio`. GitHub rejected it because the Git OAuth credential lacks `workflow` scope for the changed `.github/workflows/xma2-decoder.yml`. `gh.exe` exists at `C:\Program Files\GitHub CLI\gh.exe`, but `gh auth status` reports the saved `edupode` token invalid and DNS lookup for `api.github.com` fails. Commit remains local and worktree is preserved. Do not remove the CI change or use an alternate credential. Wait for user to reauthenticate/approve the scope normally.
- Live tracker #4257 refreshed; the current unchecked CPU code-read list includes #3634, #3884, #3954, #4289, #4299, #4302. #3884 is already checked by independent PR activity; #3634/#4289/#4299/#4302 were assigned distinct read-only preflights to existing chats. #4250 audit remains active. No review chat may claim/edit/publish.
- Four chats were reassigned: `01a2037c-72f1-7ad2-a458-d60c7bfc1a17` → #4289; `01a2037c-6d57-7320-b199-9095f5c0776e` → #4299; `01a2037c-7a37-73b2-90f8-0a2fd1b9d472` → #4302; `01a2037c-5c24-7d30-b7d6-54b5de7ed51a` → #3634. Each must repeat live preflight and stop before diff if review/checker or overlap exists.

### 2026-11-23 00:45 UTC — live review activity and next CPU candidates
- Live #4257 (updated 00:45:12Z) lists CPU code-read unchecked: #2061, #3634, #3884, #3954, #4308. The tracker entry can lag PR activity, so every chat repeats the live preflight before diff access.
- PR #4299 gained a formal COMMENTED review from @Kam471 at 00:42:32Z on `cf5c0f7f`; another independent test/code-read comment followed at 00:43:17Z. I confirmed this live and told the assigned reader to stop immediately. No finding from our chat; the author/reviewer discussion already notes minor edge cases.
- PR #4289 is open at `8479d6c0f203da75518afe730780baa45f5627fb`, but has independent Windows/Linux test and code-read comments. #4302 has a formal COMMENTED review; #3634 has checker/review comments. All stopped before additional diff review.
- Current distinct assignments: #4250/#4305 to `01a2037c-4a85-75e0-b70d-b2459c440107`; #3954 to `01a2037c-5c24-7d30-b7d6-54b5de7ed51a`; #4308 to `01a2037c-72f1-7ad2-a458-d60c7bfc1a17`; #2061 to `01a2037c-7a37-73b2-90f8-0a2fd1b9d472`. #4299 review chat `01a2037c-6d57-7320-b199-9095f5c0776e` was told to stop after the formal review arrived. AnyXbox cross-reference ended without an eligible unowned contribution; possible D3D-451 remains only a candidate, not claimed or implemented.
- No review chat ran tests, made edits, claims, commits, or public posts.
- RT-197 remains independent-review-clean locally, but push to the existing fork branch was rejected for missing OAuth `workflow` scope. The local `gh` credential is invalid and API DNS is unavailable. User has been asked to reauthenticate the CLI normally; do not retry until authentication changes.

### 2026-11-23 00:47 UTC — rechecked PR readers; tracker #4257 moved again
- Tracker #4257 now reports five unchecked CPU code-read PRs: #2061, #3634, #3884, #3954, #4310 (updated 00:46:24Z). #2061 is being preflighted by `01a2037c-7a37-73b2-90f8-0a2fd1b9d472`; #4310 by `01a2037c-4a85-75e0-b70d-b2459c440107`. #3954 is already known to have checker activity; #3634/#3884 likewise have independent checker/review comments. The now-omitted #4289 had three checker/test comments; no diff opened.
- PR #4299’s checker arrived while two readers were active. Both stopped immediately once it appeared. They had inspected code/test patches only (tools/d3d12_guid_sites.py and tests/test_d3d12_guid_sites.py); no docs, tests run, findings reported, edits, or public posts. This is recorded to make that partial review explicit and prevent duplicate work.
- PR #4308 assignment was not in the latest tracker’s unchecked CPU list; its chat completed the earlier #4289 preflight only. Do not count #4308 as checked. The newly dispatched #4308 task should be revalidated from the latest turn before any inspection; target is now stale/uncertain.
- #4250, #4289, #4299, #4302, #3634, #3884, #3954 and #4305 all show review/checker activity or are no longer eligible. Read-only audits made no claims, changes, tests, commits or public posts.
- RT-197 fix is still local and review-clean; no authentication change has been received, so no second push attempt.

### 2026-11-23 00:51 UTC — three latest CPU reviews dispatched
- Tracker #4257 refreshed at 00:50:11Z. Its seven unchecked CPU code-read entries were #2061, #3634, #3884, #3954, #4316, #4319, #4320. The older four are already checker/ambiguous. Three distinct live preflights were sent: #4316 → `01a2037c-4a85-75e0-b70d-b2459c440107`; #4319 → `01a2037c-7a37-73b2-90f8-0a2fd1b9d472`; #4320 → `01a2037c-72f1-7ad2-a458-d60c7bfc1a17`.
- These are read-only; each chat must verify current head, review/checker activity and path overlap before opening any diff. No tests, claims, edits, public reviews or comments.
- RT-197 PR #3441 hardening remains independent-review-clean locally at `c2eb015cb0d6a3c2f31170d2f99becfb3cce4c66`; push blocked by the OAuth `workflow` scope. Waiting for user's normal CLI reauthentication; no retry until credentials change.

### 2026-11-23 00:59 UTC — CPU PR review wave 2
- Tracker #4257 moved to `Updated 00:58:45Z`; earlier #4319 remains blocked by the active claim for `tools/spirv_asm.py`, and #4320 completed without a finding before a checker comment arrived.
- #4338 was stopped before diff inspection because it already had checker activity and a formal COMMENTED review.
- New read-only assignments: #4340 → `01a2037c-5c24-7d30-b7d6-54b5de7ed51a`; #4341 → `01a2037c-7a37-73b2-90f8-0a2fd1b9d472`; #4342 → `01a2037c-72f1-7ad2-a458-d60c7bfc1a17`; #4344 → `01a2037c-4a85-75e0-b70d-b2459c440107`; #4346 → `01a2037c-682d-7d01-bb8c-7d93e30eb417`; #4349 → `01a2037c-6216-74e1-83b1-894a55f3fd3f`; #4350 → `01a20262-100b-7761-bcbd-c60f8270cfc8`.
- All assignments are static/read-only: each chat must re-read the live tracker and roster, stop on any review/checker/claim/path overlap, exclude game data and generated assets, and make no tests or public posts.
- PR #3441 is still open at public head `83a15c2b91f1ebf9b44dfa12180d441ca0fc1a9e`; the independently reviewed hardening commit `c2eb015cb0d6a3c2f31170d2f99becfb3cce4c66` remains local because GitHub requires OAuth `workflow` scope. No push retry until authentication changes.

### 2026-11-23 01:00 UTC — CPU review wave 2 outcomes
- #4320 was read statically at head `0d17b7011dbda4ca87b0bd7eb1a9f9a6eefceee8`; no actionable finding in `make_game.py` and its synthetic preflight test. A checker comment arrived at final recheck, so the task stopped without posting.
- #4319 review is incomplete: its chat opened the source/test patches before noticing active tracker claim #7088698779 for `tools/spirv_asm.py`; it then stopped. No tests or public posts, and no finding is reported.
- Preflight-only stops: #4338 has a formal COMMENTED review/checker; #4340 (#BUILD-1216) has a formal COMMENTED review/checker at `565bb51424e1a82179b30933b36ba5590fb60a89`; #4341 (#BUILD-1239) has a formal COMMENTED review at `8ba1eaf544758ebd84fec77c271ac4686bb19cfd`; #4342 (#BUILD-1238) has a formal COMMENTED review/checker at `8977284838a3a75ab040616fc6c112fd189ceb00`.
- #4350 is closed/unmerged and explicitly duplicated by open #3338 and #3984 on the same paths; no diff was read. Do not re-open as an independent task.
- Still in progress: #4344 (`01a2037c-4a85-75e0-b70d-b2459c440107`), #4346 (`01a2037c-682d-7d01-bb8c-7d93e30eb417`), and #4349 (`01a2037c-6216-74e1-83b1-894a55f3fd3f`). All are read-only preflight/review assignments.


### 2026-11-23 01:04 UTC — CPU review wave 3 outcomes and queue
- #4346 stopped at preflight because the PR includes excluded documentation (`docs/saves.md`); no diff was read. #4349 stopped at preflight: independent checker activity plus exact path overlap with open #4283 (`tools/spirv_asm.py`, `tests/test_spirv_asm.py`).
- #4344: while reviewing permitted source/test hunks, a formal COMMENTED review arrived on the same head; the chat stopped and discarded its observations. #4352 already had a formal COMMENTED review. #4355 had a formal review plus checker. No further reviews/findings were posted.
- #4353 includes excluded `AGENTS.md` and `docs/gamedata.md`; #4354 includes shader/GPU/docs work outside this review scope; both stopped before inspecting patches. #4357 was no longer unchecked and already had a formal review plus a test failure report; no diff was opened.
- #4350 remains closed as a duplicate of open #3338/#3984; no diff was read.
- Live tracker #4257 last checked at 01:03:16Z lists CPU code-read #4346/#4353/#4354 among older entries and newly unchecked #4358, #4359, #4360, #4361. Revalidate each candidate before opening any patch.
- #4356 remains in progress in `01a2037c-5008-7670-b4ec-5215c1dee169`. No public claims, reviews, edits, or tests were made by these read-only chats.


### 2026-11-23 01:06 UTC — latest queue refresh
- Tracker #4257 updated at 01:06:01Z. #4358, #4360, and #4361 are no longer listed as unchecked; do not reopen them. Current newly unchecked CPU candidate is #4365; #4359 remains unchecked and its assigned review is active.
- #4356 stopped before code review: formal independent COMMENTED review and Windows test report by @Kam471 on the same head; it is no longer in the current unchecked CPU list. No code or tests were examined/run.
- Current assignments: #4359 → `01a2037c-6216-74e1-83b1-894a55f3fd3f`; #4365 → `01a2037c-5008-7670-b4ec-5215c1dee169`. Both must recheck live state before diff and remain read-only.


### 2026-11-23 01:09 UTC — review wave 4 and GitHub CLI status
- #4359 stopped at metadata preflight: open PR #4308 changes the same two paths (`scripts/run_wine_tests.py`, `tests/test_run_wine_tests.py`) and tracker comment #7088860752 is an active claim on `scripts/run_wine_tests.py`. No diff/source lines were read; no test or post.
- #4356 stopped at preflight because @Kam471 had a formal COMMENTED review and Windows test report on the same head; it had also left the unchecked CPU list. No code or tests were examined/run.
- #4365 disappeared from the live unchecked CPU list before review; the assigned chat was told to revalidate and stop if no longer eligible.
- Live tracker #4257 at 01:08:30Z lists new CPU unchecked #4367, #4368, #4369, #4370, #4372, #4374. Read-only assignments: #4367 → `01a2037c-4a85-75e0-b70d-b2459c440107`; #4368 → `01a2037c-5c24-7d30-b7d6-54b5de7ed51a`; #4369 → `01a2037c-7a37-73b2-90f8-0a2fd1b9d472`; #4370 → `01a2037c-72f1-7ad2-a458-d60c7bfc1a17`; #4372 → `01a2037c-682d-7d01-bb8c-7d93e30eb417`; #4374 → `01a2037c-6d57-7320-b199-9095f5c0776e`.
- Local GitHub CLI recheck still says the saved `edupode` token is invalid. `gh auth refresh -h github.com -s workflow` failed to connect because the host could not resolve/connect to github.com; no device code or token was exposed. No retry of push. #3441's reviewed fix remains local at `c2eb015cb0d6a3c2f31170d2f99becfb3cce4c66`; public PR head is still `83a15c2b91f1ebf9b44dfa12180d441ca0fc1a9e`.


### 2026-11-23 01:13 UTC — CPU review wave 5 outcomes
- #4365 was revalidated after leaving the unchecked list; it has COMMENTED and APPROVED reviews on the current head. No diff opened.
- #4367 was stopped before file inspection because it includes `docs/design.md`; #4368 was stopped because it includes `docs/gamedata.md` and shader/game-data work. #4369 was stopped because its paths overlap open #4007 and it has a third-party comment. #4370 and #4372 already had formal COMMENTED reviews. #4374 already had a formal review, Windows checker report, and same-path conflict with #4370. No tests or public posts.
- #4359 was stopped by overlap with #4308 and active claim #7088860752; no diff read.
- GitHub CLI transition: a local elevated `gh auth status` now succeeds as `edupode` but reports scopes `gist, read:org, repo`, lacking `workflow`. An interactive `gh auth refresh -h github.com -s workflow` is currently waiting for browser authorization after opening the device flow. Do not push until it completes and a read-only CLI status confirms the scope.


### 2026-11-23 01:35 UTC — local CLI publish batch
- User asked to stop using the GitHub connector; all GitHub read/write work in this batch used local `git`/`gh` CLI only.
- Pushed reviewed XMA2 hardening commit `c2eb015cb0d6a3c2f31170d2f99becfb3cce4c66` to `edupode/Leonida:codex/xma2-audio`. Existing PR #3441 now points to this head; it is open/mergeable and all checks are pending.
- Pushed claimed TEST-891 commit `52ab757f97425b524a8b45f5cac6c11b9e5de675` to `codex/i18n-index-marker-test` and opened PR #4430. The PR is open/mergeable, changes one file, and its checks are pending. Body includes the user-requested AI attribution.
- Pushed claimed synthetic exit-code commit `26ca9529bd3865c5d355470ee844c0381c529569` to `codex/tiny-exit-code-diagnostics` and opened PR #4451. The PR is open/mergeable, changes one file, and its checks are pending. Body includes the user-requested AI attribution.
- Remaining branch inventory: #3831 already matches local adapter head `c13f3ce1ee0168332031870b607ae6fe235f5896`; no new push. The local DXGI GetDevice commit is superseded by the same fix already open as #4068; no duplicate push/PR. XMem branch `codex/xmem-virtual-write-watch` is already on the fork at `84f86c5a7db842c9db53e4b5172277f317650ecb`, but no active XMem claim was found in #4257 or the queried #3918 tracker commands; do not create a PR before claim/overlap is resolved.
- No code, tests, claims, or public review posts were made by the recent read-only PR-audit chats. Their completed results are recorded above.
### 2026-11-23 — PR #4430 feedback fix pushed
- Addressed @shdann's finding that an unrelated backticked hash in ordinary prose could satisfy the i18n sync-marker test. The helper now limits extraction to the first nonblank opening note block after the title; added a synthetic regression test.
- Commit `2d0dbdf95a328d6f4f16c6ad0fa04c6b98a763ed` pushed to `edupode/Leonida:codex/i18n-index-marker-test`; PR #4430 now points to this head and remains open/mergeable.
- Verification: `python -I tests/test_i18n_index.py` — 9 tests passed; `git diff --check` clean. CI and No Game Data are queued. Posted a concise PR update with the verification.
- Cross-chat push audit: the other local contribution branches matched their published PR heads (#3441, #3831, #4451; #4068 belongs to another author); XMem #2199, DXGI #2343, docs #1502, and XBCH #1298 are already merged. No other unpushed contribution commit was found.
### Latest PR #4430 status recheck
- The PR remains open at head `2d0dbdf95a328d6f4f16c6ad0fa04c6b98a763ed`; CI and No Game Data are queued. GitHub's latest response is `mergeable=UNKNOWN` while it recalculates after the push.
- The earlier mergeable result was from the initial post-push query; use this later status until GitHub recomputes.



### 2026-11-23 — Coordination platform proposal v2
- A full proposal draft is available locally at .planning/2026-11-22-leonida-contribution-and-audit/coordination-platform-proposal-v2.md. It is a local draft only; no public post or service exists.
- The immediate recommended stage is a local CLI/worker pilot; the global API requires maintainer ownership and an explicit source-of-truth cutover.
- Do not start implementation until Ed approves the design draft. The short summary for issue #725 is included in the file but has not been posted.
### 2026-11-23 05:16 UTC — three-workstream proposal and PR #4788
- Live tracker #4683 is open (last updated 05:07 UTC). It requires public claims before coding and excludes game data, shaders, keys, and decryption material.
- Requested maintainer/community guidance in #725: https://github.com/ItsNotRexed/Leonida/issues/725#issuecomment-7092249312
- Requested post-#4657 CI measurement and security/infra help for a possible volunteer-compute pilot in #3828: https://github.com/ItsNotRexed/Leonida/issues/3828#issuecomment-7092337216
- Full local proposal and security baseline: .planning/2026-11-22-leonida-contribution-and-audit/three-workstreams-and-safety-policy.md. Volunteer compute remains discussion-only; no service, API, installer, or worker has been created.
- PR #4657 merged at 0db7fed551c4d48d7b97a9c4d609fc11d17ae19c; its modeled CI savings still need post-merge measurement.
- Claimed a new TEST item in #4683 comment 7092006317. PR #4788 is open at head 261495ccac1c62ac22fb833d5c26c565b5750fe9. Independent review approved; CI and No Game Data were queued at 05:09 UTC. Do not merge while required checks are pending.
- Private local CLI: Task 1 extraction commit 1604f0cc3fa027656be6ac099e6a9ac13ca7911f and Windows venv harness fix 43d375b234041793eae6aa5dfeb8c27ffc1c03b7. Independent re-review approved; the Windows venv test and diff-check passed. Tasks 2–7 remain; files have not yet been copied from the isolated scratch repo into this workspace.
- The earlier note above saying there was no public post was accurate when written and is superseded by the comments linked here.
- No game data, shader artifacts, credentials, keys, or decryption material were accessed or added.

### 2026-11-23 05:25 UTC — local coordination CLI Task 1 copied
- Copied the independently reviewed Task 1 files from scratch into .planning/2026-11-22-leonida-contribution-and-audit: compatibility launcher, five-module package, and two synthetic test modules.
- Verified both entrypoints with --help; no live GitHub reads, claims, or writes were triggered.
- The original watcher matched the scratch baseline before replacement. No existing package or same-named tests were overwritten.
- Marked the six Task 1 plan steps complete. Tasks 2–7 remain in the approved implementation plan.
- The local workspace has no Git repository, so this is a local-only change; no commit or push exists for it.

### 2026-11-23 05:30 UTC — tracker rollover and second claimed test fix
- Current tracker is #4816; it says claims from closed #4683 carry over. PR #4788 remains attached to the earlier claim.
- Claimed a separate new TEST item at https://github.com/ItsNotRexed/Leonida/issues/4816#issuecomment-7092469574.
- Scope: tests/test_docs_list_items.py only. It mishandles a shorter backtick fence inside a longer Markdown fence and can report a false duplicate-list failure.
- Live preflight found no open PR, tracker path-index entry, or same-topic claim/comment for this path.
- Planned branch: codex/docs-list-fence-guard, based on main 98fa503f8a19c3ed65df1f4f2f0e4571aa42c4f7. Use synthetic input only and avoid the whole-repository Markdown scan during local testing.

### 2026-11-23 05:41 UTC — PR #4837 published
- Current tracker remains #4816. Claim #7092469574 is now represented by PR #4837.
- Branch codex/docs-list-fence-guard, head 68332df3fe16e0652274490683c9a4894cbfa1f7; changed file tests/test_docs_list_items.py only.
- The synthetic backtick and tilde regressions failed before the parser change and passed afterward; the existing synthetic duplicate-detection test and git diff --check passed.
- The full repository Markdown scan was not run locally because it reads every tracked Markdown document. CI is queued to perform the project check.
- PR #4837 is open/mergeable; CI and No Game Data were queued at 05:40 UTC. Do not merge while checks are pending.
- PR #4788 remains open at 261495ccac1c62ac22fb833d5c26c565b5750fe9; its CI and No Game Data checks are still queued.
- No game data, shaders, assets, logs, credentials, keys, or decryption material were accessed or added.
