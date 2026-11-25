# Leonida: proposal for coordination, CI, and community validation

**Status:** discussion proposal; not approved by maintainers  
**Updated:** 2026-11-23  
**Proposed by:** @edupode, with assistance from ChatGPT 6.2 Astra Ultra Thinking

## Goal and boundaries

Reduce duplicated work, shorten the time to useful CI results, and let people without coding experience help with validation. These are three independent workstreams. Each can proceed, wait, or be declined on its own.

This document asks for guidance and contributors. It does not authorize changes to Leonida's process, a hosted service, worker installation, running PR code on other people's computers, data collection, or bypassing maintainer approval.

## Verified status

- [#725](https://github.com/ItsNotRexed/Leonida/issues/725) remains open. It proposes an experimental read-only coordination view and links to an external demo. The issue asks for maintainer agreement before moving to API claims or repository integration.
- [#3828](https://github.com/ItsNotRexed/Leonida/issues/3828) remains open and discusses slow CI, external runners, and possible voluntary contributions to costs.
- [PR #4657](https://github.com/ItsNotRexed/Leonida/pull/4657) merged on 2026-11-23. It moves Wine jobs and the Windows semantic-tests leg to main/manual runs. The reduction in the PR is a forecast based on sampled data, not a post-merge measurement; observed impact still needs to be measured.
- [#4816](https://github.com/ItsNotRexed/Leonida/issues/4816) is the current coordination tracker as of 05:28 UTC. Recheck it before every task; claims from #4683 carry over. It requires a claim before work and prohibits game data, shaders, credentials, and decryption material.
- The local coordination CLI is under private development. Task 1 was extracted and independently approved after a Windows virtual-environment test fix. The remaining tasks are not complete; nothing has been published or integrated into Leonida.
- The separate runner-coverage fix was claimed and published as [PR #4788](https://github.com/ItsNotRexed/Leonida/pull/4788). It is open; checks were queued at the last check. It is separate from these three proposals.

## Summary

| Workstream | Lowest-risk first deliverable | Source of truth | Required decision |
|---|---|---|---|
| 1. Coordination | Local CLI for read-only checks and local-only reservations | Current coordination issue and GitHub | Maintainer decision before any public API/automation |
| 2. CI | Post-#4657 measurement separating queue and execution time | GitHub Actions and repository workflows | Maintainer approval before workflow, provider, or budget changes |
| 3. Volunteer compute | Opt-in pilot for allowlisted synthetic jobs | Approved job manifest plus official review | Explicit security, infrastructure, and maintainer approval |

# 1. Claims, PR, and chat coordination

## Problem

The coordination issue gets many comments and rolls over when it reaches limits. Near-simultaneous claims can duplicate work. A view or CLI can collect claims, PRs, branches, and paths, but a stale snapshot cannot prove that a task is free.

## Phased proposal

### 1A — Local CLI with no GitHub writes

An optional CLI uses the existing authenticated GitHub CLI session to check the current tracker, comments, open PRs, branches, and changed paths. A local SQLite database can stop two chats/processes on the same computer from reserving the same task at once.

The public claim remains mandatory and is the source of ownership. A local reservation is not a global lock and does not authorize work to begin. The plan includes read-only checks, local registration after validating the public claim, heartbeat, release, listing, and monitoring with a minimum interval and request limits.

### 1B — Read-only view, if maintainers want it

Connect PR/branch states to the #725 proposal or another approach chosen by the project. Ambiguous claims should be marked “needs confirmation”; ownership must not be inferred. A shared file path is a review signal, not automatic proof of conflict.

### 1C — Claims API only after an explicit decision

Before creating a service, decide how an API relates to existing comments; which wins in a race; how leases expire, renew, and release; who operates the API; and how it fails. An unavailable service must return “unknown/unavailable,” never “free.”

## Security controls for coordination

1. GitHub remains the source of truth until a public decision changes that.
2. Query only ItsNotRexed/Leonida. Authentication, pagination, rate-limit, or network failures produce an unknown state and block any “free” result.
3. Use the existing GitHub CLI session. Never request, store, print, or transmit PATs, cookies, SSH keys, or tokens.
4. SQLite is local and transactional; never present a local reservation as another person's or another machine's claim.
5. Create a local lease only after validating the current tracker, claim URL, author, task, and paths.
6. Normalize repository-relative paths; reject absolute paths, UNC paths, traversal, and incomplete data. Unknown paths block registration.
7. Invoke programs without a shell. Never turn issue or PR comments into commands.
8. Poll no more frequently than every 60 seconds; cap requests per cycle, show timestamps, back off on 403/429, and preserve the last valid snapshot.
9. The first phase posts no comments, labels, claims, or PRs automatically.

# 2. CI, queueing, and authorized runners

## Problem

Reports in #3828 show that queue time and execution time are different issues. PR #4657 changed which jobs run on PRs versus main. Without a comparable post-merge window, we should not claim that the issue is solved or that a forecasted percentage was achieved.

## Phased proposal

### 2A — Measure the actual impact of #4657

Collect a post-merge window and compare it with an equivalent baseline. Separate job creation, start, finish, queue time, execution time, outcome, workflow, operating system, and PR/main. Document how cancelled jobs, stale runs, and runs without real jobs are handled.

Publish only aggregates: p50/p90 wait and runtime, run volume, failure rate, backlog, and stale jobs. Distinguish measurements from extrapolations.

### 2B — Small, reversible optimizations

Use observed results to propose path filters, workflow splits, concurrency controls, or cancellation of stale runs. Preserve jobs that catch independent failures. Every change should state which PR coverage it removes, which main job provides equivalent coverage, and how long a regression may take to surface.

### 2C — Managed-runner pilot

If the queue remains excessive, compare cost, operating-system capacity, privacy, retention, location, network policy, isolation, and provider guarantees. Start with a small number of jobs, retain a clear fallback, and get approval from the budget owner.

## Security controls for CI

1. Do not run untrusted public-PR code on persistent self-hosted runners with secrets, credentials, LAN access, or access to other repositories.
2. Do not place secrets in fork jobs. Do not switch to pull_request_target to bypass restrictions; that trigger requires its own security review.
3. Set minimum permissions per workflow/job. Use read-only tokens by default; grant write only when required.
4. Pin third-party Actions to immutable SHAs and review updates.
5. Use self-hosted runners only after approval: ephemeral, isolated, destroyed after the job, without personal folders, host sockets, shared storage, or unauthorized devices.
6. Do not publish logs/artifacts containing game content. Share aggregates and sanitized error messages only.
7. Keep the No Game Data guard on every PR. CI condition changes need equivalent validation and an explicit path to run on main.

Primary references: [GitHub Actions secure use](https://docs.github.com/en/actions/reference/security/secure-use) warns that self-hosted runners are not guaranteed to be clean/ephemeral and can be compromised by untrusted code. [GITHUB_TOKEN permissions](https://docs.github.com/en/actions/tutorials/authenticate-with-github_token) recommends limiting permissions to the minimum required.

# 3. Volunteer compute and validation network

## Goal and limits

Let community members without coding experience contribute CPU/GPU time to public builds, synthetic tests, static analysis, or compatibility matrices that use no game content. Do not process, store, split, encrypt, reconstruct, or transmit game files. Donating storage for game content remains out of scope.

A community worker is not a trusted GitHub runner. A volunteer's consent does not make it safe to run arbitrary commands from a public PR.

## Recommended job model

1. A maintainer publishes a signed manifest with job ID, immutable commit SHA, execution profile, image version, quotas, network policy, expiry, and result schema.
2. Use allowlisted profile IDs, not free-form commands submitted through PRs, issues, or Discord.
3. Before asking for consent, the worker shows the job source, commit, profile commands, resource use, network policy, telemetry, and expected output.
4. The local agent validates signature, origin, expiry, and profile against a local allowlist. Any mismatch rejects the job.
5. Run in an ephemeral VM or equivalent boundary approved by security review; clean state after completion. A plain container is not a sufficient boundary for arbitrary code.
6. Block network access by default. Any exception is limited to required public source/dependency destinations and protocols. No LAN access.
7. Apply local quotas for CPU, RAM, processes, disk, duration, and concurrency. The owner can pause, stop, or revoke participation at any time.
8. Return a minimal structured result: SHA, profile/version, machine class, pass/fail/timeout, duration, and allowlisted exit codes. No memory dumps, checkout files, shaders, textures, audio, game content, or raw logs.
9. Community results are informational and do not replace official checks or review. Confirm results in a project-controlled environment before using them for merge decisions.
10. The first phase uses public source and synthetic inputs only. Untrusted PR code requires an ephemeral sandbox, no secrets, and blocked network access.

## Security controls for volunteer compute

- No job is created from free-form text in an issue, comment, Discord message, or PR. All remote input is untrusted data.
- Signing keys remain under project control. Use short expiry and documented rotation/revocation. Any change to SHA or profile invalidates the signature.
- Workers receive no PAT, write token, CI secrets, browser session, provider credentials, or local network credentials.
- Checkouts and outputs are disposable. Each job starts in a new workspace; do not share writable caches across volunteers or PRs.
- Images and profiles are small, versioned, and auditable. Updates must not install arbitrary binaries without verification.
- Volunteers approve resource quotas and data policy. Opt-out must remove the worker and credentials and clean local state.
- Invalid signature, unknown profile, expired job, exceeded quota, or malformed result invalidates the result and suspends the worker for review.
- Telemetry must not collect the system username, personal paths, hardware serial, retained public IP, or persistent identifiers without explicit consent.
- Do not use a one-click installer that silently executes code. Do not offer remote shell, unrestricted file transfer, execution of images supplied by any PR, or a persistent GitHub runner on a volunteer computer.
- Incident response: stop new jobs, revoke the key, suspend workers, inform maintainers, and retain only necessary metadata without sensitive material.

## Safe pilot sequence

1. Get maintainer approval and name operations and incident-response owners.
2. Complete a threat model and review the manifest/result protocol.
3. Demonstrate a synthetic local job in a disposable VM, offline and without credentials.
4. Test invalid signatures, expiry, revocation, quotas, cancellation, and output sanitization.
5. Invite volunteers to an opt-in pilot using public/synthetic inputs.
6. Compare community results with independent official runs; stop on inconsistency or incident.
7. Keep the network switchable off without affecting normal CI, claims, or merges.

# Combined implementation sequence

## Stage 0 — Maintainer decision

Confirm which workstreams are wanted, who can coordinate them, what may be upstream, and which infrastructure/costs are acceptable. Do not build an API, worker, or public integration before this decision.

## Stage 1 — Evidence and local tool

Complete the approved private CLI; keep GitHub read-only and write only local SQLite state. Measure duplicate/stale observations without changing the official claim process. Establish a CI baseline and measure post-#4657 behavior.

## Stage 2 — Small, reviewable changes

After claiming in #4683, open small documentation/measurement or CI PRs after verifying no one owns the same task/path. CI changes must include coverage impact, rollback, and minimum permissions.

## Stage 3 — Separate compute prototype

After approval and security review, implement the smallest protocol for synthetic jobs. Do not combine a dashboard, claim API, public workers, and managed runners into one delivery.

## Stage 4 — Measure and decide

Publish aggregate metrics, failures, costs, and incidents. Keep, limit, or close each workstream based on evidence. The worker network must be stoppable without affecting the normal project.

## Proposed success measures

| Workstream | Measures |
|---|---|
| Coordination | Duplicate claims, time to detect a duplicate, snapshot age, unknown-state count; target: zero false “free” reports |
| CI | p50/p90 queue time separated from runtime, jobs/runner minutes per PR, regression detection time, failures found only on main |
| Volunteer compute | Reproducible jobs, independent confirmations, result divergence by profile, quota compliance, incidents; target: zero out-of-sandbox access |

Set numeric targets only after a measured baseline exists. Do not promise improvement percentages in advance.

## Help requested

- **Maintainers/coordinators:** decide whether the CLI remains personal, becomes an optional tool, or feeds the read-only view; clarify whether a claims API is wanted.
- **CI contributors:** measure the observed impact of #4657 and separate queue, runtime, coverage, and capacity.
- **Security/infrastructure contributors:** review signing, isolation, quotas, network policy, and incident response before any worker prototype.
- **Volunteers:** do not install anything yet. If a pilot is approved, provide only operating-system/architecture and approximate availability.

## Open decisions

1. Should the local CLI remain private, or would the project accept an optional repository tool?
2. Does the tracker remain the sole official claim channel? Is an API desirable given authentication, operations, expiry, and outage handling?
3. What time window and metrics define success after #4657?
4. Would the project consider volunteer compute for synthetic jobs only? Who signs jobs and responds to incidents?
5. Should running untrusted PR code on community machines remain excluded until an ephemeral sandbox is approved?

## References and decision log

- Current coordination tracker: [#4816](https://github.com/ItsNotRexed/Leonida/issues/4816)
- Read-only view proposal and maintainer approval request: [#725](https://github.com/ItsNotRexed/Leonida/issues/725)
- CI/runners discussion: [#3828](https://github.com/ItsNotRexed/Leonida/issues/3828)
- Recent CI change: [#4657](https://github.com/ItsNotRexed/Leonida/pull/4657)
- Separate runner coverage PR: [#4788](https://github.com/ItsNotRexed/Leonida/pull/4788)
- [GitHub Actions secure use](https://docs.github.com/en/actions/reference/security/secure-use)
- [GitHub GITHUB_TOKEN permissions](https://docs.github.com/en/actions/tutorials/authenticate-with-github_token)

**2026-11-23 — recommendation:** start with the local CLI and post-#4657 measurement. Treat volunteer compute as a separate, opt-in proposal restricted to synthetic jobs. GitHub remains the source of truth for claims until maintainers decide otherwise.

**Still undecided:** hosting, API, provider/cost, worker identity, signing-key rotation, sandbox, output retention, and any additional permissions.