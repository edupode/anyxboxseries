# XAudio2/XMA2 contribution plan

**Status:** RT-197 is claimed by @edupode. Original claim: #7084056596. Current tracker: #3574; current status comment: [#7086036830](https://github.com/ItsNotRexed/Leonida/issues/3574#issuecomment-7086036830). Phase 1 is open as [PR #3441](https://github.com/ItsNotRexed/Leonida/pull/3441), branch edupode:codex/xma2-audio, head 83a15c2b91f1ebf9b44dfa12180d441ca0fc1a9e, based on main f847748cc76bd3777f9c070c4034eb2fc1c04e64.

## Goal
Add a Windows XAudio2_9 path that forwards ordinary voices to host XAudio2 and decodes Xbox XMA2 source buffers to PCM, while preserving format checks, stream routing, loop ranges, callbacks, and buffer ownership. All fixtures remain generated in memory; never use game or console audio.

## Decision
Use the upstream xma crate as a pinned external Cargo dependency, not copied or vendored source. Its current main commit is 27122f7dfcceb9a899555362ee0dde2d530e033c, and its package declares LGPL-2.1-or-later. Preserve the FFmpeg/XMA copyright notices and license in Leonida's third-party notices. Keep the XAudio2 wrapper independently implemented. The existing repo uses Rust for an optional tool, but the C++ runtime does not yet; do not make Cargo a mandatory runtime build dependency until the isolated FFI prototype and build experiment pass.

## Implementation slices
1. **Decoder boundary prototype:** create a small Rust C-ABI crate for one mono/stereo XMA stream. Validate nulls, channel/rate initialization, packet-size multiples, checked output sizing, panic containment, and owned PCM buffer lifetime. Add only generated synthetic packet tests. No source voice or host-engine changes yet.
2. **C++ interoperability gate:** call the FFI from a synthetic runtime test, verify errors and PCM ownership/freeing, and exercise the exact CMake/Windows toolchain path. If cargo/git dependency integration makes builds brittle, stop and revise the dependency design before widening it.
3. **XAudio2 host adapter:** add the narrow Windows ABI wrapper; forward ordinary source voices to host XAudio2, intercept XMA2, group multi-stream channels, route packets, preserve callback contexts, and keep decoded PCM alive through buffer-end callbacks. Cover malformed formats, buffers, play/loop bounds, flush/EOS, and callback order with synthetic tests.
4. **Delivery:** run the focused synthetic tests and runtime build/CI, inspect the final diff for forbidden inputs and license notices, push after each coherent PR slice, open PRs with tracker and AI attribution, then monitor checks/reviews before merge.

## Review gates and limits
- Never run upstream tests that open audio/reference fixtures; run only new synthetic tests.
- Silent synthetic decoding validates framing/routing and memory behavior, not audible quality.
- Do not inspect or use game archives, audio, executables, shaders, textures, logs/reports, keys, or decryption data.
- Separate a large implementation into independently reviewable PRs; each later PR names its dependency.
- Do not report the XMA2 feature complete until the host adapter, callback lifetime, loops, CI, and review are all handled.

## Current delivery checkpoint — 2026-11-22
- Phase 1 decoder boundary and C++ ABI smoke are pushed in PR #3441. The branch is clean locally and matches the remote head.
- Previous local validation passed: six Rust tests in debug and release, rustfmt check, strict Clippy, and MSVC C++ ABI smoke against debug and release static libraries.
- All six GitHub checks remain queued: change-scope, jactionlint, linux-gpu-tools, no-game-data, windows, and windows-ffi. There are no formal PR reviews yet.
- @kapitche independently passed the Windows 11 Rust/C++ ABI smoke on this head. A maintainer approval request is posted on PR comment #7085434474.
- The current tracker rolled to #3574; RT-197 status is carried forward in comment #7086036830. Do not merge while checks are queued.
- The host XAudio2 source-voice adapter remains outstanding. No game/audio fixtures, shader artifacts, textures, keys, or decryption material were used.

## Follow-up review — 2026-11-23 00:32 UTC
- Static review of PR #3441 at head `83a15c2b91f1ebf9b44dfa12180d441ca0fc1a9e` found two P2 issues: the C++ ABI smoke puts calls/checks inside `assert`, so `NDEBUG` removes test execution and validation; Rust wrapper-owned decoder and PCM `Box::new`/`into_boxed_slice` allocations can abort instead of returning the declared OOM status.
- Fix is assigned to a fresh implementer on the existing isolated `codex/xma2-audio` branch. Scope: the FFI Rust implementation, synthetic C++ smoke, and the XMA2 decoder workflow only. RED/GREEN evidence and an independent scoped review are required before push.
- No test, edit, or push has been completed yet. Do not merge while required CI checks are queued.
- Tracker moved to #4257; RT-197 remains an existing claimed PR, so no duplicate claim was posted.

## Implementation result — 2026-11-23 00:37 UTC
- Commit `c2eb015cb0d6a3c2f31170d2f99becfb3cce4c66` fixes the two review findings. Rust wrapper handle allocations now use fallible allocation; PCM retains its `Vec` to avoid a shrink conversion; C++ smoke checks remain active under `NDEBUG`, which the workflow now sets.
- Synthetic TDD evidence: the two allocation fault-injection tests failed before the fix and pass after; Rust suite 8/8; format, strict Clippy, build, and diff check pass; MSVC negative and positive controls under `/DNDEBUG` behaved as expected.
- The worktree is isolated and clean. Independent review is running; do not push until it passes. The PR remains open and CI workflows are still waiting for maintainer approval.

## Publication status — 2026-11-23 00:45 UTC
- Independent review passed with no findings. Push to `edupode/Leonida:codex/xma2-audio` was attempted and rejected by GitHub: the OAuth credential lacks `workflow` scope for `.github/workflows/xma2-decoder.yml`.
- Local `gh.exe` is present at `C:\Program Files\GitHub CLI\gh.exe`, but the saved `edupode` CLI token is invalid and `api.github.com` DNS lookup failed. No workflow change was removed or bypassed; the reviewed commit remains local at `c2eb015cb0d6a3c2f31170d2f99becfb3cce4c66`.
- User was asked to complete normal GitHub CLI reauthentication with `gh auth refresh -h github.com -s workflow`; do not ask for or accept a pasted token. Retry the push only after normal auth succeeds.
