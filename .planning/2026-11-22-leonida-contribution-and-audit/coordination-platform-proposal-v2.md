# Leonida Coordination & Validation Grid

## Status

**Draft for Ed’s review.** This is a new, fuller proposal intended to replace the narrow read-only dashboard/API pitch in issue #725. It is not approved by Leonida maintainers, is not a public commitment, and authorizes no deployment or repository changes.

## Executive proposal

Build one coordination system with three parts:

1. **A shared portal** showing tasks, owners, leases, PR state, validation jobs, and worker availability.
2. **A claim service** that atomically awards one task lease. A losing claimant receives an immediate conflict response; no race is decided by comment order or polling speed.
3. **A local worker CLI** that uses the same identity and lease, checks out an exact PR head into an isolated workspace, and runs only approved validation profiles on a machine whose capabilities match the job.

The public portal may remain static on GitHub Pages. The state-changing API must run separately. The recommended default is Cloudflare Pages Functions/Workers with one Durable Object per task; Vercel Functions plus a transactional PostgreSQL provider is a valid alternative if maintainers already own that stack. The coordinator service and the claim policy must belong to Leonida maintainers.

## Current situation

The live coordination tracker is #4421. It requires a public **claiming: <ID>** comment before starting work, and its board is manually maintained from comments, branches, and PRs. The local **coordination_watch.py** can check GitHub and uses SQLite leases to stop duplicate registration on this machine, but it requires an already-published claim and cannot reserve work across machines.

Issue #725 already documents the collision pattern, including claims seconds apart, and the first read-only dashboard. This proposal keeps that evidence and dashboard as prior work while expanding the design into a complete control plane. It avoids opening a second dashboard project.

## Goals

- Give every task exactly one globally visible active owner or an explicit parallel scope.
- Make simultaneous claims deterministic: one succeeds; the rest get a conflict.
- Prevent our own Codex chats from racing before the public comment is posted.
- Let contributors claim and release from the portal or CLI using the same account identity.
- Route validation work to workers with matching operating system, architecture, GPU, driver, and tool capabilities.
- Pin every validation to an immutable PR head SHA and named test profile.
- Show stale tasks, expired leases, missing PRs, review state, and validation evidence without hand-edited status tables.
- Make outages safe: an unavailable coordinator pauses new claims and job starts.
- Keep game files, generated or extracted data, shaders, keys, and decryption material out of the service and worker outputs.

## Non-goals

- Automatically choose or edit implementation tasks.
- Run arbitrary commands copied from comments, PR descriptions, or web pages.
- Merge PRs or grant write access to contributors.
- Turn personal PCs into general-purpose GitHub Actions runners.
- Replace CI for ordinary Windows/Linux test suites.
- Make a public service or change contribution rules without maintainer agreement.

## System shape

~~~mermaid
flowchart LR
  U[Contributor or Codex chat] --> P[Coordination portal]
  U --> C[leonida CLI]
  P --> A[Claim API]
  C --> A
  A --> T[One atomic task lease]
  A --> Q[Validation job queue]
  Q --> W[Capability-matched local worker]
  W --> X[Ephemeral worktree / sandbox]
  X --> R[Allowlisted test profile]
  R --> A
  G[GitHub App webhooks] --> A
  A --> S[Visible task, PR and test status]
  S --> P
~~~

The claim service owns lease state. GitHub remains the source for issue inventory, PR heads, reviews, and hosted CI status. Webhooks update the projection; a periodic reconciler repairs missed events. The portal is a view over these two sources, not another manually maintained board.

## User experience

### Portal

The landing page has four views:

- **Available:** eligible tasks with ID, scope, size, required skills/hardware, and a claim button.
- **My work:** active leases, expiry, heartbeat health, PR link, and release/complete controls.
- **Validation queue:** jobs waiting for Windows, Linux, GPU vendor, architecture, driver, or other capability labels.
- **Needs coordinator:** stale claims, legacy-comment conflicts, expired leases, closed PRs with active leases, missed webhook reconciliation, and jobs that lost their worker.

Task details show the exact current owner, claim start/expiry, linked branch/PR/head SHA, overlapping open PR paths as a review signal, and the latest validation results. File overlap is never presented as proof of duplicate work.

The **Set up a local worker** button starts a guided onboarding flow. It can detect the browser OS, authenticate the user, select capabilities, and provide a signed installer or package-manager command. It cannot silently execute code on the computer. The user must approve installation and worker enrollment. The installed CLI then performs a self-check and appears online in the portal.

### CLI

One cross-platform command, initially implemented as a Python CLI in the existing local coordination workspace:

~~~text
leonida auth login|status|logout
leonida task list
leonida task claim <ID> [--scope <name>]
leonida task heartbeat <lease>
leonida task release <lease>
leonida worker enroll|status|doctor|run|stop
leonida validate <PR> --sha <40-hex> --profile <approved-profile>
~~~

The first release uses the installed **gh** session and the existing local SQLite store. A later service-backed release uses GitHub App authentication and the same command names; the CLI must never accept or store a personal access token pasted into chat or a webpage.

The CLI prints a clear state for every operation: claimed, already owned locally, claimed elsewhere, stale/unknown, authentication failure, or service unavailable. It must not translate an error or incomplete response into “free”.

## Claim protocol

### Task claims

- A claim is keyed by repository and task ID, with an optional maintainer-defined scope. A whole-task lease conflicts with every scoped lease. Scoped leases may coexist only when the task explicitly defines disjoint scopes; the coordinator checks the full active-lease set before granting one.
- **POST /v1/claims** includes the task ID, scope, client idempotency key, and an optional linked PR.
- The API returns **201** with a lease ID, owner, expiry, and monotonically increasing fencing generation, or **409** with the active owner and expiry. Invalid/unlisted work returns **422**; unauthenticated and unauthorized requests return **401/403**; backend uncertainty returns **503**.
- Each lease is short and renewable. Initial local defaults can follow the existing two-hour lease, with a heartbeat every 15 minutes; tune after observing real expiry and abandoned-work rates.
- Heartbeat, release, and completion require the authenticated owner, lease ID, and current fencing generation. A stale worker cannot report success after its lease expires and another owner acquires the task.
- Claim and release events are append-only audit entries. Maintainers can pause claims or revoke a lease with a reason.
- A client idempotency key makes safe retries return the original claim result rather than create duplicate claims.

### Local race prevention before the public service exists

The local CLI obtains a SQLite **BEGIN IMMEDIATE** reservation **before** it posts the project's required **claiming: ID** comment. This blocks two Codex chats on this PC from both posting the same local claim. The CLI then posts and reads back the exact tracker comment before it permits coding.

If the post is definitively rejected, the local reservation is released. If the network drops after the post may have succeeded, the reservation is marked **unknown/pending** and blocks further local work until the CLI reconciles the tracker. It must never retry blindly. Other contributors can still race this comment-based process; this local phase is not represented as a global lock.

## Validation jobs and workers

A **task claim** gives one contributor ownership of implementation. A **validation job** is a separate lease so testers can validate a PR without stealing its implementation task.

A validation job is uniquely identified by **(repository, PR number, head SHA, profile, platform fingerprint)**. It records the requested profile, required capabilities, result, start/end time, worker ID, toolchain versions, and a short summary. Duplicate jobs for the same tuple return the existing job unless the coordinator explicitly requests a second independent run.

Workers advertise an allowlisted capability record, for example:

~~~text
os=windows; arch=x64; compiler=msvc-2022; gpu=nvidia-ada; driver=...
~~~

The worker polls the API outbound over HTTPS. The API leases only a compatible job, and every job includes the exact SHA and a named profile from a trusted catalog. The worker creates a disposable worktree or VM, verifies the SHA, runs the profile with time and resource limits, stores detailed output locally, and uploads only an approved summary. Raw logs are not uploaded by default.

The worker must not execute arbitrary shell text supplied by an issue, PR, or API record. Profiles are reviewed and versioned with the coordinator. Public-PR jobs need an isolation boundary, no repository write token, no user credentials, and no access to the host's personal files. GPU/hardware testing that cannot run in a disposable VM requires explicit local approval and a dedicated account. Ordinary CI remains on GitHub-hosted runners.

**Do not register a personal machine directly as a self-hosted runner for this public repository.** GitHub warns that untrusted pull-request code can persistently compromise self-hosted runner environments.

## API and storage

### Recommended default: Cloudflare

- Keep the existing static page on GitHub Pages, or move the portal to Cloudflare Pages if a single deployment is simpler for maintainers.
- Use a Pages Function or Worker as the API.
- Route each task key to a deterministic Durable Object. Its transactional, strongly consistent storage serializes claim/release for that task. Separate objects let unrelated tasks proceed in parallel.
- Use a small durable database/queue for worker enrollment, validation jobs, and audit indexing if needed.
- Store GitHub App credentials, webhook secrets, and signing keys only as server-side secrets.

This matches the existing #725 roadmap's “one serialized state object per item” idea and gives task-level atomicity.

### Vercel alternative

Vercel Functions can host the UI/API. Use a managed PostgreSQL provider and an atomic transaction plus a unique constraint on active **(repo, task, scope)** claims. Do not implement the lock as a read-then-write check in two separate requests. The function and database should share a nearby region, and all state-changing routes must use the same transactional path.

The platform choice belongs to the maintainers who will own billing, credentials, incident response, and deployment. Do not create a public service under a contributor's personal account as the long-term authority.

### GitHub integration

Register a GitHub App with the minimum permissions needed. Prefer read access to issues/PR metadata and write access only to the one feature actually required, such as posting checks or updating a board. Never place an App private key, client secret, or installation token in the static site or CLI package.

The portal uses the GitHub App web authorization flow; the headless CLI can use GitHub App Device Flow. Worker enrollment uses a one-time, short-lived pairing code and a device key that maintainers can revoke. GitHub webhooks are authenticated with **X-Hub-Signature-256**, deduplicated by delivery ID, and treated as hints; periodic reconciliation repairs missed deliveries.

## Cutover and outage policy

Comments and API claims cannot be made atomic across two separate channels. A migration therefore has a declared cutover:

1. **Shadow mode:** the service reads the tracker and PRs, publishes a read-only view, and compares parsed legacy claims with expected API state. It does not award claims.
2. **Pilot:** maintainers test synthetic tasks and a small set of real, low-risk tasks with volunteer contributors.
3. **Migration window:** import active comment claims, resolve ambiguous/duplicate claims with the coordinator, then announce the exact cutover time.
4. **Authoritative API:** after cutover, the API is the only channel that awards a new claim. Legacy comments remain history. The portal and tracker link to the API state.
5. **Outage behavior:** no new claims or validation jobs start while the API is uncertain. The dashboard may show the last snapshot marked stale. There is no automatic fall-back to comments. If maintainers choose an emergency manual mode, they explicitly pause API claims and announce one coordinated switch.

No 15-second polling loop should be used to approximate a lock. Use webhooks for GitHub changes, moderate polling for worker queues, and backoff on rate limits.

## Local-first rollout

1. **Improve the existing local CLI.** Add local pre-claim reservations and worker status/job records on SQLite. Keep the existing **check**, **watch**, heartbeat, release, and fail-closed behavior.
2. **Pilot locally.** Run two or three named workers on synthetic, CPU-only profiles from separate worktrees. Verify no duplicate local task claims, lease recovery, pinned SHA, and clean worker shutdown.
3. **Draft for maintainers.** Present this v2 proposal in the existing coordination proposal thread rather than opening a duplicate dashboard issue. Ask maintainers to choose source of truth, service owner, platform, and cutover policy. Do not publish without Ed's approval.
4. **Shadow service.** After maintainer sponsorship, build a read-only hosted projection and compare it with the comments/board over a trial period. Record disagreements and freshness.
5. **Claims pilot.** Test simultaneous synthetic claims, web and CLI login, lease expiry, outage, and rollback with maintainers and a small volunteer set.
6. **Public cutover.** Import active claims, announce the single authoritative channel, update contributor instructions, and enable the portal's claim button.
7. **Worker pilot and expansion.** Start with low-risk synthetic Windows/Linux profiles. Add GPU/hardware profiles only after isolation, capability reporting, and output-scrubbing are verified.

## Security, privacy, and project rules

- No game assets, archives, executable contents, extracted data, generated shaders, keys, decrypt material, or derived logs enter the portal, API, worker queue, or uploaded results.
- GitHub App permissions are least-privilege and server-side. Browser and worker tokens are short-lived, scoped, and revocable.
- Validate GitHub webhook signatures and event types; make handlers idempotent.
- Apply per-user and per-machine rate limits, payload caps, job timeouts, disk quotas, and result-size limits.
- Worker result summaries include status, exact SHA, profile, environment fingerprint, counts, and error class. Detailed logs remain local unless explicitly approved for upload.
- Retain claim/audit events only as long as maintainers need them; publish the retention policy.
- The local CLI remains private until the project accepts a contribution scope. The hosted API and onboarding flow are maintainer-owned infrastructure, not a contributor-owned dependency.

## Acceptance criteria

- In a race test with 100 simultaneous claims for one synthetic task, exactly one request gets **201**; all others get **409**; no duplicate active lease exists.
- A timed-out client retry with the same idempotency key cannot create another lease.
- Expiry and reclaim increment the fencing generation; the previous owner cannot heartbeat, release, or publish a result.
- Two local Codex sessions cannot reserve one task simultaneously; an ambiguous remote post blocks retry until reconciled.
- The portal and CLI show the same owner and expiry within a bounded refresh interval.
- A worker accepts only a registered, non-revoked identity, a compatible job, a catalog profile, and the exact assigned SHA.
- An untrusted test job cannot access repository-write tokens, the user's GitHub credential store, or personal files.
- API timeout, stale tracker data, missing pagination, webhook gaps, and permission errors all fail closed.
- Onboarding succeeds on Windows, Linux, and macOS with explicit user authorization; no browser action silently installs or starts software.
- No public cutover occurs until maintainers approve ownership, permissions, migration, and outage policy.

## Decisions needed before implementation

1. Which maintainers will own the service and its account/billing?
2. Is the API the single authority after a declared cutover, with claims paused during outages?
3. Which task types may be split by scope, and what is the initial lease/heartbeat policy?
4. Which machine profiles are allowed to run public-PR code, and which require manual approval or a disposable VM?
5. Should the portal post GitHub checks/comments, or display service state without writing to GitHub?
6. Which platform do maintainers already operate: Cloudflare (recommended by fit with the current demo roadmap) or Vercel + PostgreSQL?

## Research references

- [Leonida coordination proposal #725](https://github.com/ItsNotRexed/Leonida/issues/725)
- [Existing coordination demo roadmap](https://github.com/ttd0x/leonida-coordination/blob/main/ROADMAP.md)
- [GitHub Pages static hosting and server-side limitation](https://docs.github.com/en/pages/getting-started-with-github-pages/creating-a-github-pages-site)
- [GitHub App least-privilege permissions](https://docs.github.com/en/apps/creating-github-apps/registering-a-github-app/choosing-permissions-for-a-github-app)
- [GitHub App CLI/device flow](https://docs.github.com/en/apps/creating-github-apps/writing-code-for-a-github-app/building-a-cli-with-a-github-app)
- [Cloudflare Durable Objects storage and consistency](https://developers.cloudflare.com/durable-objects/best-practices/access-durable-objects-storage/)
- [Cloudflare Pages Functions bindings](https://developers.cloudflare.com/pages/functions/bindings/)
- [Vercel Functions and Marketplace storage](https://vercel.com/docs/functions) · [storage choices](https://vercel.com/docs/marketplace-storage)
- [GitHub Actions concurrency limits](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/control-workflow-concurrency)
- [GitHub self-hosted runner security warning](https://docs.github.com/en/actions/reference/security/secure-use)

## Alternatives and why they are not the core design

- **Pages-only dashboard:** useful for visibility and stale-data warnings, but cannot atomically reserve a task or authenticate a write.
- **GitHub Projects alone:** useful as a view and reporting surface. Assignment/labels/comments remain mutable by multiple contributors and do not provide the required atomic lease.
- **GitHub Actions concurrency as the claim lock:** rejected as the system of record. It serializes jobs, but pending-run behavior and workflow scheduling are not a persistent, per-task ownership database. Actions remains CI.
- **Vercel + PostgreSQL:** technically sound and kept as an alternative. Use it if maintainers already own Vercel and a database integration; implement the claim with a transaction and uniqueness constraint.
- **A custom GitHub App service:** selected for global claims because it can enforce a narrow permission set and retain an auditable lease state outside the comment stream. Cloudflare is the default hosting fit for this draft.

## Short maintainer summary (draft for #725; not posted)

The read-only view has exposed a coordination problem that polling cannot solve: two contributors can claim the same task between snapshots. I propose evolving it into a maintainer-owned Coordination & Validation Grid: one portal, one atomic claim API, and a local worker CLI. The API would return one winning lease and an immediate conflict to simultaneous claimants, renew short leases, and show task/PR/validation state in the portal. The CLI and portal would use GitHub App identity; workers would validate a named profile against an immutable PR SHA in an isolated worktree and publish only a small result summary.

Roll out in shadow mode first, then pilot with synthetic tasks and volunteers. Before cutover, import and resolve active comment claims; after a declared cutover, the API is the sole claim authority and outages pause new claims instead of falling back to comments. Keep GitHub Pages as the static view and place the API in a maintainer-owned Cloudflare Worker/Durable Object service, unless maintainers already operate Vercel/PostgreSQL. No public worker should run untrusted PR code on a personal self-hosted runner.

Maintainers would need to choose the service owner, canonical claim channel, outage policy, allowed worker profiles, and whether the portal writes GitHub checks/comments. This is a proposal for discussion; it should not be deployed or presented as approved until they agree.