# Local Leonida Coordination Watcher Implementation Plan

> **For agentic workers:** Use subagent-driven development for the implementation and independent review.

**Goal:** Give the user's Leonida-related Codex chats a shared local preflight that spots duplicate claims, active PRs, and path overlap before work begins.

**Architecture:** A private Python standard-library CLI in this local workspace reads GitHub through the installed, authenticated `gh` CLI and stores same-machine leases in SQLite. GitHub remains authoritative; the tool never posts claims or changes repository state. Local registration is allowed only after the remote claim is verified, and the output clearly says that a remote snapshot is not a global reservation.

**Tech stack:** Python standard library, GitHub CLI, SQLite.

**Spec:** [coordination-watch-design.md](coordination-watch-design.md)

## Global constraints

- This first version is a private local utility; do not add it to Leonida or change `AGENTS.md` / `CONTRIBUTING.md` without maintainer agreement.
- Discover the current open coordination issue by title each time; require exactly one matching issue and fail closed on zero or multiple matches.
- A task is eligible only if the current tracker explicitly lists it as up for grabs, or its exact new-item claim has been verified in current tracker comments.
- Check current tracker claims, open PRs/branches, changed paths, and local active leases before registration; unreadable or incomplete GitHub data means conflict/unknown, never “free”.
- Never store GitHub credentials; invoke `gh` without a shell and accept its authenticated session only.
- Scope every GitHub call explicitly to `ItsNotRexed/Leonida`; obtain the authenticated login via `gh api user`.
- SQLite transactions must prevent same-machine chats from registering the same task concurrently; file overlap is reported before a local lease is granted.
- `--check` reports a timestamped observation, not ownership or a cross-machine lock. The contributor must still post the project-required public claim before editing.
- Accept only repository-relative GitHub paths; convert `\\` to `/`, reject absolute/drive/UNC paths and `..`, and compare normalized paths case-sensitively.
- An exact path overlap blocks local registration by default. `--allow-overlap` may override only known PR overlap with a supplied reason; unknown branch paths remain blocked.
- Use at least a 60-second polling interval, cap API work per cycle, preserve the last good snapshot on API errors, and back off on rate limits/forbidden responses.
- Do not access or write game data, archives, executable contents, assets, audio, generated shaders, logs quoting them, keys, or decryption material.
- Do not add or run a test suite for this private utility unless the user asks for that verification; keep inputs and any manual examples synthetic.

## Files

- Create `.planning/2026-11-22-leonida-contribution-and-audit/coordination_watch.py` in this local workspace.
- Do not change the Leonida checkout or public repository.

## Tasks

### 1. Preflight environment and current tracker

- Read the design and this plan; inspect the current roster before starting.
- Resolve `gh` using `PATH`, then the standard Windows install path; run `gh auth status` and `gh api user`; stop on failure.
- Scope every repository query to `ItsNotRexed/Leonida` using explicit `--repo` or the equivalent fixed API path.
- Find open issues in that repository whose title matches `^Coordinating who works on what \([0-9]+\)$`; continue only when exactly one exists.
- Fetch its body and paginated comments. If pagination is incomplete or the response exceeds the configured request/page cap, report unknown and fail closed. Require the claimed item in its up-for-grabs table or a verified exact new-item claim in its comments.
- Parse task IDs only from explicit command-at-line-start forms (`claiming:`, `status:`, `releasing:`, `done:`) and the current up-for-grabs table; never infer ownership from free text.

### 2. Implement read-only remote check

- Provide `check --task-id ID [--paths PATH ...] [--pr URL ...]`.
- Fetch current tracker claim/status comments, matching open PRs and their changed paths, plus branches listed in the current tracker branch index and matching repository branches, within a documented API-call cap. For a branch without a PR, compare its base/head where GitHub exposes both refs; if changed paths remain unknown, report unknown and block registration.
- Return distinct exit states for clear-snapshot, conflict, and unknown/error. Print the tracker URL and observation time; state that a clear snapshot does not claim/reserve work.
- If the task is absent from the current up-for-grabs table and has no verified exact new-item claim, return unknown rather than clear.
- Treat another user's matching active claim/PR/branch or any same-task local lease as conflict. An exact claim by the authenticated user is shown as `claimed by you` so `register` can proceed after verifying the link. Report file overlap with exact PR links; do not silently decide that same-file overlap is harmless.

### 3. Implement local SQLite leases

- Store state under `%LOCALAPPDATA%/Leonida/coordination.sqlite3` on Windows and `$XDG_STATE_HOME/leonida/coordination.sqlite3` (or the platform user-state directory) elsewhere.
- Provide `register --task-id ID --agent LABEL --claim-url URL --paths PATH ...`; before writing, re-run the remote check and verify the claim URL points to a comment on the discovered current tracker, the comment author equals `gh api user`, and the comment names the same task.
- Generate a random `lease_id` at registration. Use `BEGIN IMMEDIATE` (or equivalent transactional locking) so two same-machine chats cannot both register one task. Reject duplicate active task IDs and block on exact path overlap unless a known-overlap override supplies the PR URL and reason.
- Provide `heartbeat --lease-id ID`, `release --lease-id ID`, and `list-local`; require the lease ID and original owner label to renew/release. Leases expire after a documented interval.

### 4. Implement watch and optional PR events

- Provide `watch --task-id ID --interval 60 [--pr URL ...]`; reject intervals below 60 seconds.
- Normalize task paths as repository-relative POSIX paths; reject absolute Windows/Unix paths, UNC paths, and parent traversal before any comparison.
- Poll only the current tracker, requested task, and explicitly selected PRs. Keep a per-cycle API request ceiling; on 403/429 or incomplete pagination, retain the last valid state, report unknown, and use backoff rather than repeatedly polling.
- Print only changes to tracker ID, claim/status, matching PR head/review/check state, and local lease state.
- Offer `gh event listen` only for an explicitly selected PR. Continue polling because the event stream omits tracker comments/rollover and can miss events while disconnected.

### 5. Manual review and handoff

- Inspect the finished script for shell invocation, credential handling, pagination, concurrency, fail-closed behavior, and clear output.
- Do not run tests or touch protected game material. Provide usage examples that use synthetic task IDs/paths and do not create GitHub writes.
- Keep delivery local. A separate Leonida contribution requires a fresh maintainer-approved scope, a tracker claim, and its own repository branch/PR.

## Review focus

- Zero/multiple trackers, pagination truncation, and tracker rollover must never produce a “free” result.
- Concurrent local `register` calls must not both succeed for one task.
- A mismatched, missing, or unverified public claim URL must not create a lease.
- `403`, `429`, authentication expiry, and network failures must preserve the last valid snapshot and report unknown.
- PR rebases and changed-file pagination must refresh path overlap before new local registration.
