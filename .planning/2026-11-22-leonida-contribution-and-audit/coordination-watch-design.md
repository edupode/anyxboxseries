# Design: local Leonida coordination watcher

## Problem

The user's Leonida-related Codex chats inspect a rapidly moving public tracker independently. This has produced stale tracker numbers, duplicate task candidates, and confusion about open PR/path overlap. The GitHub coordination issue remains the project source of truth; the local chats need a shared same-machine view.

## Existing project work

Leonida issue #725 proposes an external, read-only dashboard. Its follow-up claim API would require maintainer agreement and a remote service. This design deliberately stays private and local: it neither duplicates that public dashboard nor changes the repository workflow.

## Decision

Build a private Python standard-library CLI in this local workspace at `.planning/2026-11-22-leonida-contribution-and-audit/coordination_watch.py`. It invokes the installed, authenticated GitHub CLI without reading or storing credentials. A per-user SQLite database with transactional writes coordinates active work between chats on this machine.

## Behavior

- `check --task-id ID [--paths ...]` discovers the one current open coordination issue whose title is `Coordinating who works on what (<number>)`, checks its up-for-grabs list and explicit claim/status comments, matching open PRs/branches and their changed paths, then checks the local active-task database.
- The check is an observation at a timestamp, not a reservation. It never says a task is globally safe solely because it saw no conflict. The contributor still posts the required public claim manually before editing.
- `register` repeats the remote preflight, verifies the caller-supplied public claim link, and writes a local lease in one SQLite transaction. A second local chat cannot register the same active task at the same time. Heartbeat, release, and list commands manage these leases.
- `watch --task-id ID --interval 60 [--pr URL ...]` polls the current tracker and explicitly selected PRs, showing state changes. It retains the last valid snapshot and reports unknown on API/auth/pagination/rate-limit failures.
- `gh event listen` is optional for an explicitly selected PR's review/head/state/check events. Polling remains required for tracker comments and missed events.

## Boundaries

- No GitHub write, claim, review, branch, push, merge, or comment is performed by this tool.
- Do not add the tool to Leonida or modify `AGENTS.md`/`CONTRIBUTING.md` without maintainer approval.
- Require exactly one open tracker matching `^Coordinating who works on what \([0-9]+\)$`; fail closed on zero/multiple trackers or incomplete pagination.
- Every GitHub query is explicitly scoped to `ItsNotRexed/Leonida`. A claim URL must identify a comment on the discovered current tracker issue; its author must match the authenticated `gh` user and the comment must name the same task ID.
- Accept only repository-relative paths. Normalize `\\` to `/`, reject drive-rooted, absolute, and `..` paths, then compare the canonical path exactly with GitHub's case-sensitive path.
- Detect branches from the current tracker branch index and repository branch list. A same-task branch with no PR is a conflict. If a relevant branch has no PR and its changed paths cannot be retrieved, report unknown and block registration by default; never infer non-overlap.
- By default, any exact changed-path overlap blocks local registration. Only `--allow-overlap` can override a known overlap, with the PR URL and reason stored in the local lease. Unknown branch paths cannot be overridden by this option.
- Each local registration creates a random `lease_id`; `heartbeat` and `release` require that ID and the same local owner label.
- Use a conservative minimum 60-second polling interval and a per-cycle request ceiling with backoff on rate limits.
- The SQLite lease coordinates only chats sharing this local user account. Public claims/PRs remain authoritative across machines.
- No game data, archives, executable contents, assets, audio, generated shaders, logs quoting them, keys, or decryption material are read or written.
