# Local Coordination CLI and Safe Worker Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven development to implement this plan task by task, with an independent review after each task.

**Goal:** Upgrade the private local watcher into a cross-chat CLI that reserves a task before posting its required GitHub claim, monitors the current tracker, and validates an exact PR SHA through approved local worker profiles.

**Architecture:** GitHub remains the source for tracker, PR, review, and hosted-check state. A local SQLite state machine serializes chats before a public claim write; only an explicit CLI command uses the authenticated gh session and reconciles the result. A local worker accepts immutable profiles and runs code only inside an isolated OCI container; the public claim API and portal are a separate maintainer-owned phase.

**Tech Stack:** Python 3.10+ standard library, GitHub CLI, Git, SQLite, Docker or Podman OCI runtime.

**Spec:** [coordination-platform-proposal-v2.md](coordination-platform-proposal-v2.md); retain the safety rules in [coordination-watch-design.md](coordination-watch-design.md).

## Global Constraints

- Keep this tool private; do not add it to Leonida or change contribution rules without maintainer approval.
- Watch/check/worker commands never post claims. Only an explicit task claim invocation may write the required claiming: ID tracker comment.
- Use the existing authenticated gh session; never accept or store a pasted PAT, GitHub credential, or browser session.
- Discover the current tracker on each claim/check/watch operation; fail closed unless exactly one open tracker matches.
- Only claim a task listed as up for grabs or a valid new-item claim. Stale tracker data, incomplete pagination, 403/429, or ambiguous ownership means unknown and blocks a claim.
- SQLite transactions prevent two local chats from reserving the same task or exact path concurrently. An uncertain public-write result blocks until reconciled.
- Local leases last two hours and use an explicit heartbeat; an unresolved public-write outcome never expires automatically.
- Validation jobs are keyed by repository, PR number, exact 40-character head SHA, profile, and worker capability fingerprint.
- Profiles are trusted, versioned argv arrays; never evaluate shell text from issues, PRs, API responses, or profile input.
- Workers receive no GitHub credentials, personal files, game files, repository secrets, or network access. Source is read-only and scratch space is bounded.
- Untrusted PR code runs only in a disposable OCI container with a pinned image digest, no network, dropped capabilities, read-only root, CPU/memory/process limits, and a hard timeout. If isolation is unavailable, report unsupported and never fall back to host execution.
- Do not read, copy, generate, or upload game data, archives, textures, models, audio/video, generated shaders, keys, decryption material, or logs containing them.
- Do not deploy a public API, GitHub App, website, worker enrollment service, or installer in this plan. Those need a separate maintainer-approved plan.
- GitHub writes are limited to explicitly requested claim/release comments; no branch, push, PR, merge, or review operations.

## Review Focus

- Stale tracker, incomplete pagination, permission errors, or rate limits must never appear as an available task; test with a fake GitHub adapter and assert no comment write occurs.
- Two local claims for one task yield exactly one pending reservation; test concurrent SQLite writers.
- A comment POST timeout preserves an unknown reservation and prevents a second POST; test zero, one, and multiple matching synthetic comments.
- Wrong owner, stale lease ID, or expired lease cannot heartbeat or release another reservation; test each transition.
- A worker uses only the requested SHA/profile and exposes no host credentials, home directories, writable source, or network; inspect full OCI argv, environment, and mounts with a fake runtime.

## Files

- Refactor coordination_watch.py into a compatibility launcher preserving check, register, watch, heartbeat, release, and list-local.
- Create leonida_coordination/__init__.py, __main__.py, cli.py, github.py, state.py, claims.py, watch.py, profiles.py, worker.py, and sandbox.py.
- Create tests/test_coordination_state.py, test_coordination_claims.py, test_coordination_github.py, test_coordination_watch.py, and test_coordination_worker.py. Use synthetic IDs/comments/PR metadata and temporary repositories only.
- Create README.md and explicit Windows/Unix launchers; no silent PATH edits, installs, or privilege elevation.

## Interfaces

- GitHubClient.current_tracker() returns one tracker ID/URL, update time, body, and complete comments; incomplete results raise UnknownState.
- GitHubClient.post_tracker_comment(tracker_id, body) is called only by ClaimService after a local reservation.
- LocalState.reserve(task_id, owner, paths, attempt_id, now) uses BEGIN IMMEDIATE and raises LocalConflict on active task/path overlap.
- ClaimService.claim(task_id, paths, owner) returns claimed, conflict, or unknown; only claimed lets the caller begin work.
- ClaimService.reconcile(attempt_id) confirms one matching public claim; otherwise unknown remains blocking unless explicitly resolved after a fresh tracker read.
- ValidationJob identity is (repo, PR number, exact head SHA, profile ID, capability fingerprint) and stores status, timestamps, exit code, and short local summary.
- Worker.run(pr_number, head_sha, profile_id) rejects changed PR head, unknown profile, unsupported capability, or unavailable sandbox before execution.
- Profiles provide immutable image digest, argv, timeout, CPU/memory/process limits, required capabilities, a vetted source-file manifest, and allowed summary fields; no profile accepts shell text.

---

### Task 1: Characterize current behavior and establish package boundaries

**Files:** Modify coordination_watch.py. Create leonida_coordination/__init__.py, __main__.py, cli.py, github.py, watch.py. Test tests/test_coordination_watch.py and tests/test_coordination_github.py.

**Interfaces:** The old script delegates to leonida_coordination.cli.main() and preserves command names/exit codes. github.py owns gh subprocess calls; watch.py owns remote snapshots and polling.

- [x] Step 1: Write synthetic compatibility tests for current check/register/watch/heartbeat/release/list-local parser contracts using a fake executable and temporary SQLite path.
- [x] Step 2: Run focused tests and confirm the new package entry point fails before implementation.
- [x] Step 3: Extract the parser, GitHub adapter, and watcher without changing pagination, request limits, fail-closed behavior, or unknown output.
- [x] Step 4: Keep coordination_watch.py as a thin compatibility launcher and expose python -m leonida_coordination from the tool directory.
- [x] Step 5: Run compatibility tests and compare synthetic clear/conflict/unknown JSON results.
- [x] Step 6: Record the completed slice in progress.md; the current workspace is not a Git repository, so do not create commits or pushes here.

### Task 2: Add versioned SQLite reservations and safe migration

**Files:** Create leonida_coordination/state.py. Modify cli.py. Test tests/test_coordination_state.py.

**Interfaces:** LocalState(path) owns migration and transactional methods. States are claim_pending, claim_unknown, active, release_pending, and released. Existing leases migrate to active without data loss.

- [ ] Step 1: Test migration from the existing schema with a synthetic active lease and assert owner, claim URL, paths, overrides, and expiry survive.
- [ ] Step 2: Run migration tests and confirm they fail before versioned transitions exist.
- [ ] Step 3: Implement schema versioning and migration in one transaction with uniqueness on task, lease UUID, and attempt UUID.
- [ ] Step 4: Test pending/active/unknown/release transitions, expiry, wrong owner, and stale lease IDs.
- [ ] Step 5: Implement reserve, confirm, mark_unknown, renew, begin_release, finish_release, and list_active with BEGIN IMMEDIATE.
- [ ] Step 6: Race two synthetic processes for one task; assert exactly one reservation succeeds.
- [ ] Step 7: Run the focused state suite and verify old local state remains readable.
- [ ] Step 8: Record the tested state slice in progress.md; do not create commits or pushes in this non-Git workspace.

### Task 3: Implement pre-reserved claim and reconciliation

**Files:** Create claims.py. Modify github.py, state.py, cli.py. Test test_coordination_claims.py and test_coordination_github.py.

**Interfaces:** Commands are task claim ID --paths PATH..., task reconcile ATTEMPT_ID, task status, task heartbeat LEASE_ID, and task release LEASE_ID. Public comment bodies remain exactly claiming: ID and releasing: ID; attempt UUID stays local.

- [ ] Step 1: Test successful claim, already claimed/not-listed task, stale tracker, 403/429, truncated comments, and denied POST with a fake GitHub adapter.
- [ ] Step 2: Test ambiguous POST outcomes with zero, one, and multiple matching comments; assert no automatic retry.
- [ ] Step 3: Run the tests and confirm they fail before ClaimService exists.
- [ ] Step 4: Implement claim order: authenticate, discover tracker, validate eligibility, reserve locally, repeat remote preflight, post once, read back, then mark active.
- [ ] Step 5: Implement reconcile; an uncertain result remains blocking until the user resolves it after a fresh complete tracker read.
- [ ] Step 6: Implement release through release_pending, one public releasing comment, read-back, then local release.
- [ ] Step 7: Verify check/watch never invoke a write method and failed preflight never posts.
- [ ] Step 8: Record the tested claim slice in progress.md; do not create commits or pushes in this non-Git workspace.

### Task 4: Preserve real-time monitoring and legacy compatibility

**Files:** Modify watch.py, cli.py, coordination_watch.py. Test test_coordination_watch.py.

- [ ] Step 1: Test tracker rollover, zero/multiple trackers, pagination truncation, rate-limit backoff, PR head changes, and expired leases.
- [ ] Step 2: Run tests against the fake adapter; every incomplete observation must return unknown.
- [ ] Step 3: Share the bounded pagination implementation across claims and watch.
- [ ] Step 4: Run legacy command contract tests; watch interval stays at least 60 seconds and gh events remain advisory.
- [ ] Step 5: Record the tested monitoring slice in progress.md; do not create commits or pushes in this non-Git workspace.

### Task 5: Add immutable validation profiles and job records

**Files:** Create profiles.py. Modify state.py and cli.py. Test test_coordination_state.py and test_coordination_worker.py.

**Interfaces:** Commands are worker profiles, worker doctor, and validate PR_URL --sha SHA --profile PROFILE. Initial profiles are diff-check and python-unittest; python-unittest runs python -I -B -m unittest discover -s /src/tests -t /src. Pin the OCI image to python:3.12.16-slim@sha256:5e2bdb1b0d542d5039cffb8181c65930093949194a65c365dfb1b0fe13a4fa5b. Each profile includes an explicit source/test path manifest. Duplicate job tuples reuse prior results unless the user explicitly requests --rerun.

- [ ] Step 1: Reject mutable image tags, shell strings, unknown profile, malformed SHA, unsupported capability, and PR head mismatch in tests.
- [ ] Step 2: Test duplicate job reuse and a new job on a new SHA.
- [ ] Step 3: Implement the immutable profile schema and validation-job table.
- [ ] Step 4: Implement worker doctor for Python, gh, Git, SQLite, and OCI runtime without printing secrets.
- [ ] Step 5: Run focused profile/job tests and record the result in progress.md.

### Task 6: Implement exact-SHA checkout and OCI worker

**Files:** Create worker.py and sandbox.py. Modify github.py and profiles.py. Test test_coordination_worker.py.

- [ ] Step 1: With a fake runtime, assert only a read-only /src mount and bounded scratch are available; require no network, read-only root, dropped capabilities, no-new-privileges, resource caps, and hard timeout.
- [ ] Step 2: Assert no host command executes PR code and no token, home path, personal/game path, or Git credential appears in child argv/environment.
- [ ] Step 3: Query only changed-path metadata first; reject paths disallowed by the current No Game Data policy/profile before fetching file blobs.
- [ ] Step 4: Require an approved profile-specific source/test file manifest, show its paths and hash for user approval, and sparse-checkout only those files. Exclude .git and prohibited asset/shader/key/fixture paths.
- [ ] Step 5: Implement credentialless exact-SHA checkout with system/global Git config, hooks, filters, submodules, and credential helpers disabled; reject private/unavailable forks instead of reusing host credentials.
- [ ] Step 6: Implement fixed diff-check and python-unittest profiles; the latter runs only in the pinned OCI image with no network and read-only source.
- [ ] Step 7: Add timeouts, CPU/memory/PID caps, container cleanup, and sanitized local summaries.
- [ ] Step 8: Run worker tests with fake GitHub/runtime plus a synthetic temporary repository; confirm no source mutation or credential/network access.

### Task 7: Document recovery and provide explicit launchers

**Files:** Create README.md, leonida.cmd, leonida.ps1, and leonida. Test test_coordination_cli.py.

- [ ] Step 1: Test command help, recovery steps, exit codes, and safety warnings.
- [ ] Step 2: Run launcher help/doctor with a fake environment; issue no GitHub writes.
- [ ] Step 3: Document prerequisites, claim/reconcile/release, two-hour lease and 15-minute heartbeat, profiles, exact SHA, and unsupported sandbox behavior.
- [ ] Step 4: Explain that a clear snapshot is not a global lock and cross-machine atomic claims require the separate maintainer-approved API.
- [ ] Step 5: Run the local synthetic suite and git diff --check inside the tool directory.
- [ ] Step 6: Review all five Review Focus items and document supported operating systems and Python profile limits.
- [ ] Step 7: Keep the tool local; do not post the #725 summary or open a Leonida PR until the user separately asks and maintainers accept a contribution path.

## References

- Docker Official Image metadata for the Python 3.12 slim image and its multi-platform digest: https://hub.docker.com/layers/library/python/3.12-slim/?tab=layers
- The profile pins the immutable digest; the tag is informational only.

## Delivery boundary

This plan delivers same-machine claim/monitoring and a limited local validation worker. It does not make claims atomic across different computers. The public portal, API, GitHub App, OAuth/device flow, global worker queue, enrollment wizard, Vercel/Cloudflare deployment, and migration/cutover remain out of scope until maintainers approve ownership and policy.
