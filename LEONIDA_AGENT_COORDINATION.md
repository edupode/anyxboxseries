# Leonida — shared agent coordination

Last updated: 2026-11-22 (Europe/Lisbon)
Coordinator: @edupode + ChatGPT 6.2 Astra (Ultra Thinking)
Coordinator is the sole writer of this file.

## Working rules

1. Read this ledger before researching, claiming, editing, branching, reviewing, or posting. Re-read before every follow-on task and milestone.
2. Work from the live coordination issue: #2078 is current; #1902 closed and moved here at 2026-11-22 15:53 UTC. Re-read #2078's live body and comments, #313 where relevant, PRs, branches, and review activity before choosing work.
3. For claim-gated implementation, post and verify claiming: <ID> in the current tracker before editing. PR review does not require a claim. Never duplicate a claimed item or an active PR.
4. Keep all work synthetic. Never access, quote, or upload game archives/executables/assets, extracted files, shaders or generated shader files, game-derived logs, keys, or decryption material.
5. Keep code work in its own branch/worktree. Do not copy code from the user's anyxboxseries project. For ideas from that project or omonkeyz/xbox-emulator, check licenses and existing Leonida support, then implement independently where appropriate.
6. GitHub connector writes to public repositories previously returned 403. Local GitHub CLI is now authenticated as @edupode (verified 2026-11-22); use gh for authorized public writes, confirm each result, and never paste a token into chat.
7. Attribute any PR as: @edupode, with assistance from ChatGPT 6.2 Astra (Ultra Thinking). Never forge commit identity.
8. Do not post a review until its finding is concrete, verified against the current diff, and checked against existing comments/reviews. Send findings to the coordinator first.

## De-duplication

The coordinator checks threads after milestones and periodically while work is active. Each agent owns one distinct assignment at a time. Before another task, report the current PR/issue, head SHA, work done, and blockers. The coordinator alone updates this ledger. Threads are bounded workers and may stop when their assignment completes; they are not 24/7 processes.

## Current tracker and coordinated assignments

Live tracker: [#2078](https://github.com/ItsNotRexed/Leonida/issues/2078), open, updated 2026-11-22 16:02 UTC. #1902 closed and moved here; #1745 remains historical. The coordinator alone updates this ledger. Current assignments are intentionally split by PR, issue, or repository area; they do not authorize access to game-derived inputs or public changes unless the assignment says so.

| Chat | Thread ID | Current assignment | State |
|---|---|---|---|
| Review 1 | 01a2037c-10b4-7311-a7fb-5a4015da09b0 | No new work: previous #1810 pt-BR quickstart review found no issue; follow-up failed at account usage limit | System error; retry only after quota recovers |
| Review 2 | 01a2037c-4a85-75e0-b70d-b2459c440107 | Select one non-D3D claimed open PR without checker/reviewer/overlap; static review only and report before posting | #1972 and #1957 skipped after new activity; #2040 / BUILD-402 statically reviewed at current head with no finding |
| Review 3 | 01a2037c-5008-7670-b4ec-5215c1dee169 | Local AnyXboxSeries — compare PE loader/bootstrap/import resolution with Leonida; exclude D3D state, resources, queues, swapchain, harness, textures | Follow-up sent; previous root-state audit found no missing behavior |
| Review 4 | 01a2037c-55bc-7461-87aa-b3d563b5169e | Select one D3D runtime claimed open PR without checker/reviewer/overlap; static review only, report before posting | No eligible D3D runtime PR found after live preflight; no code opened |
| Review 5 | 01a2037c-5c24-7d30-b7d6-54b5de7ed51a | Read-only audit of stale #1902 coordination states vs live PRs/branches/comments; report mismatches only, no code/public edits | Follow-up sent after no unclaimed non-D3D CPU task found |
| Review 6 | 01a2037c-6216-74e1-83b1-894a55f3fd3f | Local AnyXboxSeries — compare device capability negotiation and feature enablement with Leonida; exclude resources, command state, queues, swapchain, harness, textures, GPU identification | Follow-up sent after resource audit found no useful local pattern |
| Review 7 | 01a2037c-682d-7d01-bb8c-7d93e30eb417 | omonkeyz/xbox-emulator — verify exact XMem virtual-memory/write-watch API semantics against the claimed shim slice; no code copying | Follow-up in progress; root claimed XMemVirtualAlloc/GetWriteWatch/ResetWriteWatch in #2078 |
| Review 8 | 01a2037c-6d57-7320-b199-9095f5c0776e | omonkeyz/xbox-emulator — compare graphics/backend and resource/view support with Leonida; no shader files or outputs | Follow-up sent; no code copying or edits |
| Review 9 | 01a2037c-72f1-7ad2-a458-d60c7bfc1a17 | omonkeyz/xbox-emulator — compare build, tests, CI, and licensing/contribution workflow with Leonida; no code copying | Follow-up sent |
| Review 10 | 01a2037c-7a37-73b2-90f8-0a2fd1b9d472 | Local AnyXboxSeries — audit test harness and error-reporting patterns; exclude textures, GPU-identification, runtime feature audits | Follow-up sent; no edits or code copying |

### Latest review outcomes

- #2013 / TEST-323: skipped before diff inspection after the refreshed #1902 tracker marked it checked by @borislisandro.
- #1937 / RT-129: Chat 4 stopped before source inspection; current tracker marks it checked by @Flyander, and PR has a formal CHANGES_REQUESTED review on the GameInput shim.

- AnyXboxSeries command-state audit: local code only forwards root-signature serialization and has no command-list state persistence; Leonida already tracks graphics/compute root state and snapshots arguments for draws/dispatches.
- AnyXboxSeries queue/fence audit: local source showed a one-shot graphics probe using a CPU event wait; Leonida already supports GPU-side queue wait dependencies. No new fix indicated; #1933 (event dispatcher) and #1161 (CPU fence signaling) remain active and excluded from duplicate work.

- AnyXboxSeries resource/descriptor/heap audit: no runtime pattern from the local project to add; Chat 6 found no resource lifetime or placement implementation in the inspected scope. PR #1920 / D3D-255 is open but already has an APPROVED review by Serrambi and a comment by tumbione; not a review candidate.

- #1840 / BUILD-510: merged at b0b0500d4ebe201bf17a4b5e34fef29b7a6f86fd on 2026-11-22 15:22:56Z. A possible OpConstant type-order concern was reviewed but is pre-existing; the supported rdna2spirv producer declares uint before descriptor pointers, making it non-actionable for the current caller. No review or issue posted.

- #1806: current head `023b82257aaf6ac4ca807a3e5ab1adb9c80a2d19`; review posted by @edupode with ChatGPT 6.2 Astra (Ultra Thinking), P2 Russian quickstart status contradiction and P3 missing em dashes. Review URL: https://github.com/ItsNotRexed/Leonida/pull/1806#pullrequestreview-6012897507.
- #1822: current head `dd12fb296af4fb7df08fe93ff1f4d083048952fe`; review posted for a case-insensitive POSIX output-path collision that can truncate the lights JSONL. Tracker #1902 now lists `checked: @edupode`. Review URL: https://github.com/ItsNotRexed/Leonida/pull/1822#pullrequestreview-6012893217.
- #1742: review posted for dropping `MOVEFILE_WRITE_THROUGH` on package-to-overlay copies. PR is open and needs a rebase; review URL: https://github.com/ItsNotRexed/Leonida/pull/1742#pullrequestreview-6012895354.
- #1868 / D3D-247: static review of only the identifier-renaming hunks completed at head `fb343370156a8e76f574da0a109a57f25f6697a0`; no finding, no overlap, no tests or public comments.
- #1785 is merged; its earlier P2 is no longer actionable and was not reposted. #1863 is now checked by @tumbione and @Bj0rnis and shares active files; review stopped. #1835 has no finding in the unique `docs/testing.md` hunk and remains in the rebase list.
- BUILD-139 is already fixed on `main` by merged #821 (`c2df6ab4588bb8ef7786c4d6704c93daf5ed8e03`); the live script wraps temp cleanup in a warning-only `try`/`catch`, and the current #1902 comments contain `done: BUILD-139` (#7081046004). Do not claim or reimplement it; the table row is stale.
- #313 triage found no free CPU/Windows D3D item: the tracker says none are up for grabs; recent D3D work is claimed or has open PRs (#1935, #1940, DRED proposal). No source/tests were read and no claim was posted.

Previous review history and stopped targets remain below; revalidate live PR state before any follow-up.

## Previous batch

All previous primary review assignments (#1618, #1632, #1636, #1638, #1642, #1662, #1663, #1670, #1676, and #1682) and their assigned child reviews are complete. Their findings remain in their parent chat reports; re-validate the live diff and duplicate comments before posting any of them.

## Implementation fallback

When no suitable unclaimed tracker task or unreviewed PR remains, analyze the user's local anyxboxseries project and omonkeyz/xbox-emulator for useful capabilities that Leonida lacks or only partially implements. Check license and contribution rules first. Claim tracker work before implementation; use synthetic tests only and never port code from anyxboxseries.







- #2078 is the live tracker (open, updated 16:02 UTC); #1902 closed after reaching its comment cap. Root posted claim #7081720244 for a new runtime item covering `XMemVirtualAlloc`, `XMemGetWriteWatch`, and `XMemResetWriteWatch` in `runtime/shims/xmem.cpp`; the exact path has no active PR or branch entry, and #1912 only touches registry/docs.
- Chat 2 reviewed #2040 / BUILD-402 at head `f8e5d476c89768085912463c36bbaefcc5867745` with no finding; it skipped #1972 and #1957 after new checker/review activity. Chat 4 found no D3D runtime PR eligible for an unreviewed static pass.
- The XMem implementation is in isolated clone `C:\Users\edupo\Documents\Codex\2026-11-22\leonida-xmem-apis`, branch `codex/xmem-virtual-write-watch`, based on main `4eb6155d3ac4f9d3fadb0d095c4ce13e76783615`; only `runtime/shims/xmem.cpp` is checked out. No tests have been added or run.

### XMem PR #2199 published — 16:20 UTC
- Live tracker is #2078, open and updated 16:18 UTC. Claim #7081720244 and completion comment #7081963575 are present.
- PR #2199 is open, ready for review, unmerged, and mergeable; base main 4eb6155d3ac4f9d3fadb0d095c4ce13e76783615; head 7c2fbe1544e5efd5a4d07a971bbd14d343d29fc0. Changed path: runtime/shims/xmem.cpp only. It is published from edupode/Leonida.
- Chat 2 independently reviewed the updated head. The earlier P2 (nonzero attributes silently ignored) is closed: lines 111–114 reject nonzero attributes with ERROR_NOT_SUPPORTED before allocation; no new concrete defect found.
- Current PR discussion/reviews are empty. GitHub Actions runs CI, Runtime, and No Game Data are queued. The tracker explains that fork PR checks wait for maintainer approval; no check is reported as passing yet.
- Local worktree is clean and git diff --check passed. No tests were added/run; cl.exe is unavailable in this environment. No prohibited game data, shader artifacts, keys, or decryption material were accessed.
- Next: monitor external checks and review, address feedback, and continue only with a live-checked, claimed, non-overlapping task. Do not merge until required checks/review allow it.

### Fresh PR and D3D candidate check — 16:24 UTC
- Fresh git diff --check for the XMem branch against base 4eb6155d3ac4f9d3fadb0d095c4ce13e76783615 exited 0. No tests/build were run.
- PR #2199 remains open, ready for review, unmerged; CI, Runtime, and No Game Data Actions runs are queued. No formal review or PR discussion has been posted.
- Chat d3d209_occlusion's PR status was stale: live #1625 is closed/unmerged as duplicate, #1634 is closed, and #1602 is merged. Current #313 lists D3D-291 as the available item; @LloydNicholson has first refusal and another contributor's claim is conditional. No claim made.
- Possible independent follow-up, not yet a confirmed finding: current main file runtime/shims/dxgi_com.cpp at blob SHA caf09cee711d5bed4cc0a27d0cb08026530e109a still implements SwapChain::GetDevice by calling QueryInterface on the stored object passed to CreateSwapChainForHwnd (the D3D12 command queue). Verify native ID3D12Device retrieval semantics and tracker/issue de-duplication before public report or code change. No post made.

### DXGI GetDevice follow-up claim — 16:29 UTC
- Corrected earlier stale agent status: #1625 and #1634 are closed, #1602 is merged. Current main runtime/shims/dxgi_com.cpp at blob SHA caf09cee711d5bed4cc0a27d0cb08026530e109a stores the D3D12 command queue and directly QueryInterfaces it in SwapChain::GetDevice. No active GetDevice PR or open issue found beyond coordination.
- Official Microsoft contracts: CreateSwapChainForHwnd receives an ID3D12CommandQueue for D3D12; ID3D12CommandQueue inherits ID3D12Pageable, which inherits ID3D12DeviceChild; ID3D12DeviceChild::GetDevice returns the creating device. Thus main does not resolve the parent ID3D12Device for this D3D12 path.
- Posted and verified claim #7082135485 in #2078 for a focused follow-up to D3D-193. New sparse branch is codex/dxgi-getdevice-device, based on main 4eb6155d3ac4f9d3fadb0d095c4ce13e76783615; only runtime/shims/dxgi_com.cpp and runtime/test/dxgi_swapchain_test.cpp are intended to change.
- No patch, test, or PR yet. The XMem PR #2199 remains separate and open with three queued workflows.

### DXGI GetDevice follow-up PR #2343 — 16:44 UTC
- Claim #7082135485 and completion #7082302137 are verified in #2078.
- PR #2343 is open, not draft, unmerged, mergeable, base main 49475796c226c8511b9470bf97a4daf57b8af323, head bec58c9aacaaaf0f4fbd6192a4cb3c410493392b from edupode/Leonida. Only runtime/shims/dxgi_com.cpp and runtime/test/dxgi_swapchain_test.cpp changed.
- Current main implements SwapChain::GetDevice via QueryInterface on the stored D3D12 command queue. Microsoft documents the D3D12 creation argument as an ID3D12CommandQueue; queue inherits ID3D12DeviceChild, whose GetDevice returns its parent. The patch follows that path and adds synthetic coverage for parent device and IUnknown identity. Non-D3D12 objects retain direct QI fallback.
- Upstream main advanced during work; its changes in these files were unrelated to GetDevice. Rebased and pushed the patch on the latest fetched main.
- CI, Runtime, and No Game Data workflows are queued; no formal review/comment yet. Local git diff --check passed; test/build not run because no cl.exe/CMake/Ninja are available. No prohibited game/shader data accessed.
- PR #2199 remains open/mergeable at head 7c2fbe1544e5efd5a4d07a971bbd14d343d29fc0 with the same three queued workflows and no reviewer activity.

## Live update — 2026-11-22 17:11 UTC
- Current tracker: #2300. RT-167 was claimed in #7082448917, then released in #7082573853 after preflight showed the current SDL build disables SDL_VIDEO and its GameInput shim has no SDL touch event/window route; implementing only the ABI methods would return no contacts. No code branch or push for this item.
- PR #2370 / spirv_metal_set0: declare the array-length type before use: reviewed current head 67a8fff93255953714ad0fd60ac98b4c4caa7443, no findings; formal approval by @edupode at 17:11 UTC. Focused synthetic suite 33/33 passed; mutation check failed seven tests as expected, then source was restored and the full focused suite passed again. No game data accessed.
- PR #2199 (head 7c2fbe1544e5efd5a4d07a971bbd14d343d29fc0) and #2343 (head bec58c9aacaaaf0f4fbd6192a4cb3c410493392b) remain open; their CI/Runtime/No Game Data checks are queued and neither has a submitted review.
- #2370 also remains open with CI checks queued; do not treat it as merge-ready until required checks and maintainer action complete.
- Next coordinator action: follow all three PRs and choose the next live-checked, non-overlapping, synthetic-only item. Textures remain out of this chat's scope.

## Live update — 2026-11-22 17:28 UTC
- PR #2199 received follow-up commit 84f86c5a7db842c9db53e4b5172277f317650ecb on edupode/Leonida branch codex/xmem-virtual-write-watch. Permanent registry-backed synthetic tests now cover XMemVirtualAlloc, GetWriteWatch, ResetWriteWatch, query-only state, reset/re-arm, and unsupported large-page/nonzero-attribute refusals. Built and ran with MSVC 19.51: 35/35 checks passed; diff-check passed.
- PR #2199 verification text was updated and the follow-up was posted as comment #7082850130. Its four GitHub checks were queued at the last read; merge state UNSTABLE.
- PR #2343 and #2370 remain open with checks queued. #2343 has independent Windows test reports; #2370 has a submitted approval and 33/33 focused synthetic tests. Do not merge while required checks remain queued.
- RT-167 was claimed then released after confirming the SDL_VIDEO-off build and missing host-window bridge make a touch-only shim implementation return no readings.
- Next: monitor all three PRs and select any new work only after re-reading tracker #2300, confirming no claim/PR/review overlap, then claim before editing. Keep game data and texture work excluded.

## Live update — 2026-11-22 17:45 UTC
- Current coordination issue is #2516; #2300 rolled over after 300 comments. The current unclaimed table offers only hardware-dependent work and first-refusal tasks, so no new code claim was made.
- PR #2549 / BUILD-643 reviewed at exact head ec9a99c69e9aaabfb2842cb1866699d77c25d5cb. Windows synthetic suite 10/10 passed; in-memory removal of the awc_opus cap caused the expected 62-vs-61 test failure. Submitted APPROVED review without findings as @edupode, attributed to ChatGPT 6.2 Astra (Ultra Thinking). PR is still open and not merge-ready.
- PR #2199 head 84f86c5a7db842c9db53e4b5172277f317650ecb, #2343 head bec58c9aacaaaf0f4fbd6192a4cb3c410493392b, and #2370 head 67a8fff93255953714ad0fd60ac98b4c4caa7443 are open; their CI/Windows/No Game Data workflows remain queued. PR #2199's CI run 45048953751 stayed queued for 24 minutes. #2370 has an approval recorded; required checks are still pending.
- Continue watching PR checks/reviews. Do not merge while required checks are pending. Keep game data and texture work out of scope.

## Live update — 2026-11-22 18:09 UTC
- Current coordination tracker: #2716 (rollover from #2516). Its ten unclaimed items require hardware/GPU, first refusal, an own game package, or texture work; none fit the synthetic-only/no-game-data scope, so no new code claim was made.
- PR #2699 / BUILD `psin_index` reviewed at head `e82833328f2284ead938db102294b7f9698ad7dd` against base `49475796c226c8511b9470bf97a4daf57b8af323`. Standards/spec review found no actionable issue. Two independent test reports already exist; I did not rerun tests. APPROVED review submitted by @edupode at 18:08 UTC with ChatGPT 6.2 Astra (Ultra Thinking) attribution. PR remains open, UNSTABLE, and has no checks reported.
- #2199, #2343 and #2370 remain open with four checks pending each. #2549 and #2566 remain open with approvals from @edupode but no checks reported. All remain UNSTABLE; do not merge pending/unchecked PRs.
- Next: monitor reviewer/check state and re-read #2716 before any new claim or code change. Keep game data, generated shaders and texture work out of scope.

## Live update — 2026-11-22 18:19 UTC
- Tracker #2716 remains current and has 8 unclaimed rows. They require GPU/hardware, first-refusal, a user's own game package, or texture work; no claimable synthetic-only implementation was identified.
- PR #2760 (synthetic `triangle_test` barycentric fix) reviewed at head `6ccd86dab102571418af7aa6cb8313804d40fa13`. Same-file active PRs touch other hunks. Standards/spec review found no actionable issue; I left a non-blocking note that the tests call the helper directly rather than a complete TLAS traversal. Submitted APPROVED review as @edupode at 18:18 UTC with ChatGPT 6.2 Astra (Ultra Thinking) attribution. It remains open/UNSTABLE with no checks reported.
- #2199, #2343 and #2370 remain open with four checks pending each. #2549, #2566, #2699 and #2760 remain open with no checks reported; approvals are from @edupode and do not imply maintainer approval.
- No code was edited/pushed in this pass. Continue monitoring and re-read #2716 before new claims; keep game data and texture work out of scope.
- Scope note: one unrelated candidate's public repository text included aggregate corpus-derived validation statements. I stopped inspecting that candidate and did not retain/reuse/quote them. No underlying game package/archive or asset files were opened.

## Live update — 2026-11-22, after 18:37 UTC
- Rechecked tracker #2716 and discovered BUILD-823 was already claimed by @tupacfan04 in comment #7083723755; PR #2890 is open at head `d5e40a55b72e545e80af38eab74c809c711e139c`. No claim or code change was made by this pass.
- Read only the synthetic test and `tools/rpf9dump/Program.cs` diff for #2890. The mode check precedes path resolution/output creation, and the tests check nonzero exit, usage modes, and no output side effect. No actionable static finding; no tests or public review/comment were made. PR #2890 had no checks or reviews at the last live read.
- PR #2199 is open at head `84f86c5a7db842c9db53e4b5172277f317650ecb`, matching the local branch; its four checks are queued. The XMem follow-up is already pushed, so there is no pending push from that branch.
- PR #2343 remains open/UNSTABLE with four checks queued. No merge is appropriate while checks are pending.
- Continue from fresh tracker/claim checks. Keep game data, generated shaders, texture work, keys, and decryption material out of scope.

### Live update - XMA2 claim and implementation plan
- Claimed the new XAudio2_9/XMA2 compatibility path in tracker #2891 as comment #7084056596; current tracker is #3047. Live search found no open XAudio/XMA PR or named branch.
- Created an isolated branch/worktree from main 162821017ea3c286c8bec860f3ff94f037912061: codex/xma2-audio.
- The local AnyXboxSeries projects contain no audio implementation; Leonida main has no XAudio shim beyond import classification. The xma decoder dependency in xbox-emulator is LGPL-2.1-or-later, so the implementation plan now separates its FFI boundary, Windows build integration, and host voice adapter. No code has been changed or pushed yet.
- All validation will use generated synthetic packets only. No game/console audio, shaders, executables, assets, keys, or decryption material will be used.
