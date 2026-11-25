# Opening Scene Rendering and FPS Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Make the opening playable scene render correctly and stably, then target 20–30 FPS using measured, repeatable improvements.

**Architecture:** Reuse VIctory's existing runtime counters, add opt-in shader/PSO timing, and build a local log analyzer that writes a sanitized report. Use the report to fix correctness blockers first, then compare single-variable performance changes.

**Tech Stack:** Rust, Windows D3D12 diagnostics, Python 3 standard library for report generation and parser tests.

**Spec:** `docs/superpowers/specs/2026-11-25-opening-scene-rendering-design.md`

## Global Constraints

- Keep the saved playable checkpoint unchanged and make source changes only in the isolated `codex/lighting-pipeline-next` worktree.
- Do not add game files, assets, saves, or unredacted logs to the repository.
- Keep ray-tracing builder execution all-or-none and opt-in; quarantine output writes for diagnostic runs.
- Keep risky descriptor-heap window access disabled until GPU correctness and livelock safety are measured.
- Use one fixed opening-scene interval and report cold-cache and warm-cache results separately.
- Treat 20–30 FPS as the target; retain only changes supported by runtime measurements and visual correctness.

## Review Focus

- Malformed, colored, or truncated logs must still produce a report with explicit missing fields.
- A cold shader cache and a warm cache must never be mixed into one FPS comparison.
- Dropped pipeline reasons must retain stage and shader hash without leaking local game paths.
- Quarantined BVH-builder output must not be presented as proof that final lighting is correct.
- GPU timing and compiler timing must be opt-in so normal runtime cost does not increase.

---

### Task 1: Build a sanitized run-log analyzer

**Files:**
- Create: `tools/analyze_victory_run.py`
- Create: `tools/tests/test_analyze_victory_run.py`
- Create: `docs/diagnostics.md`

**Interfaces:**
- Consumes a VIctory runtime log and optional compiler-timing records.
- Produces `report.md` and `metrics.json` in a caller-selected output directory.
- The report summarizes frame-time percentiles, CPU/GPU/copy timing, draw counts, pipeline drops, texture counters, and missing diagnostics; local paths are redacted.

- [x] Add fixture-driven tests for ANSI/plain logs, missing metrics, repeated summaries, pipeline failure reasons, path redaction, and optional timing rows.
- [x] Run the focused Python tests and verify malformed or empty input returns a readable report with explicit unavailable metrics.
- [x] Implement the parser using Python's standard library; keep the report schema versioned in `metrics.json`.
- [x] Document invocation, expected log sources, and what the report cannot infer.
- [x] Run the focused tests again and inspect generated reports for path/asset leakage.

### Task 2: Measure shader translation and native PSO creation

**Files:**
- Modify: `crates/victory-gpu/src/pipeline.rs`
- Modify: `crates/victory-gpu/src/pipeline/host.rs`
- Modify: `crates/victory-gpu/src/shader_cache.rs`

**Interfaces:**
- Add an opt-in `VICTORY_COMPILER_TIMING=1` diagnostic mode.
- Emit structured records containing stage, shader hash, source bytes, DXIL bytes, translation milliseconds, PSO creation milliseconds, and cache hit/miss.

- [x] Identify the shader translation/cache and native PSO creation boundaries. Baseline normal log volume with the flag unset remains pending.
- [x] Add focused parser coverage for emitted timing records and verify hashes/byte counts remain associated with the correct stage.
- [x] Implement timing only around actual translation and actual native PSO creation; report cache hits separately from cold misses.
- [x] Build the diagnostics executable and run focused Rust tests for affected modules.
- [ ] Capture one run with the flag unset and one with it enabled to confirm silence versus timing records.

### Task 3: Capture a controlled baseline and report

**Files:**
- Create: `docs/diagnostics/baseline-2026-11-25.md`
- Run artifacts: outside the repository, in the isolated diagnostic staging directory.

**Interfaces:**
- Use the built diagnostics executable, isolated copied save data, and isolated cache.
- Capture one warm-cache opening-scene run with normal runtime counters; capture detailed GPU timestamps separately if aggregate metrics leave the bottleneck unclear.

- [ ] Verify no competing VIctory/appcontainer process is running and that the staging config points only to its copied data and cache.
- [ ] Run a bounded warm-cache capture and record scene, duration, build hash, GPU model/driver, FPS/frame-time percentiles, CPU/GPU/copy components, pipeline drops, translation failures, and memory peaks.
- [ ] Run the analyzer and save a sanitized baseline summary in the repository; keep raw logs local.
- [ ] Run one cold-cache measurement only for shader/PSO warm-up, and label it separately from steady-state FPS.

### Task 4: Map rendering failures to their source and fix one correctness blocker at a time

**Files:**
- Modify only the owning source files identified by the baseline report.
- Add focused Rust regression tests beside each changed shader, texture, descriptor, mesh, or pipeline behavior.

**Interfaces:**
- For each fix, record the observed symptom, failing stage/hash or resource counter, source cause, and expected visual/log change.
- Compare current code with `ZSlayerHQ/victory-world-runtime` branches `world` and `next` as implementation references; adapt only changes whose data flow matches VIctory.

- [ ] Rank observed defects: missing mesh/walls/eyes, stale or absent textures/materials, untranslated critical shaders, refused host pipelines, or lost resource copies.
- [ ] Trace the highest-ranked defect from log counter/hash to the owning translator or resource path before editing.
- [ ] Implement one narrow fix and add a regression test for the proven source cause.
- [ ] Run focused tests/build and one bounded opening-scene capture; retain only if the corresponding visual/log symptom improves without new corruption or device loss.
- [ ] Repeat until the opening scene has no observed missing critical geometry/materials and no critical pipeline is silently dropped.

### Task 5: Validate ambient, shadow, and ray-traced lighting safely

**Files:**
- Modify: `crates/victory-gpu/src/pipeline.rs` and only the proven lighting/shadow owner files.
- Create: `docs/diagnostics/lighting-validation-2026-11-25.md`

**Interfaces:**
- Builder eligibility remains all-or-none and disabled unless explicitly enabled.
- Quarantined runs validate pipeline completeness and output placement; a separate functional run is required to validate actual lighting.

- [ ] Use diagnostics to enumerate every builder/trace/shadow pipeline the opening scene requests and classify translation, host-PSO, and execution status.
- [ ] Verify all required builder stages can be created before any functional builder run; preserve quarantine while checking output ranges and page guards.
- [ ] Validate that the trace/resolve path consumes the generated structure and produces visible indirect light/shadow output in a fixed scene capture.
- [ ] Enable functional writes only after the quarantine run confirms the complete pipeline set and safe output ranges; stop the run at the first device removal, memory corruption, or invalid output.
- [ ] Record the visual comparison and remaining unsupported programs; do not describe a quarantined builder run as complete lighting.

### Task 6: Evaluate XCL shader and compile-scheduling ideas

**Files:**
- Modify: `crates/victory-shader/src/emit/lower.rs` or adjacent lowering files only when profiling identifies a matching construct.
- Modify: `crates/victory-gpu/src/pipeline/make.rs` only if compile scheduling is measured as a warm-up bottleneck.
- Create: `docs/diagnostics/xcl-optimization-results.md`

**Interfaces:**
- Compare translation time, DXIL size, native PSO cold/warm time, correctness tests, and opening-scene frame time for each isolated experiment.

- [ ] Map XCL's direct compact branch targets and component-selection changes to equivalent constructs in VIctory; skip ideas without an equivalent code path.
- [ ] Prototype one code-generation change at a time and verify translated shader outputs with existing focused shader/GPU tests before measuring driver compile time.
- [ ] Keep direct descriptor-heap window access disabled by default; test only behind a diagnostic flag with bounded GPU execution and a safe fallback.
- [ ] Measure compile scheduling separately from steady FPS; start with the smallest change that improves total warm-up without increasing memory pressure.
- [ ] Record each experiment's before/after values and revert neutral or regressing changes.

### Task 7: Optimize steady-state opening-scene FPS

**Files:**
- Modify only the CPU/GPU subsystem ranked as the main bottleneck by the baseline report.
- Update: `docs/diagnostics/baseline-2026-11-25.md` with each measured iteration.

**Interfaces:**
- Benchmark the same scene after warm-up with the same resolution, build, cache state, and fixed-duration sample.
- Primary acceptance: at least 20 FPS median over the opening-scene interval; report p95 frame time and any scene-quality/stability changes.

- [ ] Use detailed per-command GPU timestamps only if aggregate CPU/GPU/copy measurements do not identify the main cost.
- [ ] Change one measured bottleneck at a time; compare median and p95 frame time over repeated runs.
- [ ] Keep a change only when the gain exceeds run-to-run variation and the correctness/stability checks remain satisfied.
- [ ] Continue toward 30 FPS only after the 20 FPS target is stable.

### Task 8: Review, checkpoint, and publish the validated improvement

**Files:**
- Only files listed in the completed implementation tasks.

- [ ] Review the final diff for correctness, path/asset privacy, diagnostic overhead, and safety-flag defaults.
- [ ] Re-run the focused tests, diagnostics build, and final bounded opening-scene capture.
- [ ] Commit the validated slices with their measured results and push the improvement branch to the previously authorized `edupode/anyxboxseries` repository.
- [ ] Preserve the immutable playable checkpoint and report the branch/commit plus remaining rendering or FPS gaps.
