# Opening Scene Rendering and Performance Design

## Intent

Make the opening playable scene render its world, characters, textures, materials, and lighting reliably, then improve the opening-scene frame rate toward 20–30 FPS. The work should explain each visible failure with runtime evidence and leave a reusable diagnostic report that other VIctory users can run.

## Goals

- Detect which pipelines or shaders fail, which draws are dropped, and whether the builder, texture uploads, or readbacks fail.
- Separate cold shader/PSO compilation time from steady-state frame time.
- Measure CPU preparation, GPU time, memory copies, texture work, pipeline waits, and frame-time percentiles.
- Validate the opening scene for missing geometry/materials, black shadowed areas, flicker, and missing character details.
- Keep fixes incremental and compare every performance change against the same scene and warm/cold-cache state.

## Scope and constraints

- Visual correctness is evaluated in the opening prologue scenes. This does not claim correctness for the entire game.
- The target is at least 20 FPS, with 20–30 FPS as the initial operating range, measured after warm-up in the same opening-scene interval.
- Keep the existing playable checkpoint immutable. Perform source changes in the isolated lighting worktree.
- Keep ray-tracing structure building opt-in and all-or-none. Use quarantine for diagnostic runs until its memory writes are verified.
- Keep title files, game assets, saves, and unredacted local logs outside the repository. A shareable report contains metrics and shader hashes only.
- Keep descriptor-heap window experiments disabled by default until GPU correctness and livelock safety are demonstrated.

## Approaches considered

1. Parse existing runtime logs only. This is low risk and can already rank frame-time, copy, pipeline, and translation symptoms, but it cannot reliably measure the cost of individual shader translation and native PSO creation.
2. Add small opt-in timing records for shader translation and native PSO creation, then produce a sanitized Markdown/JSON report from runtime logs. This is the selected approach because it covers both render correctness and the XCL compile-time proposals without adding profiling overhead to normal runs.
3. Use RenderDoc for draw-level captures. Keep it as a fallback when in-process counters cannot identify a broken render pass; GPU shared-memory pressure makes it a poor first diagnostic.

## Design

Use the existing VIctory counters as the baseline: presented FPS and frame time, CPU preparation, GPU wait, copy-back, texture work, pipeline waits, dropped draws/dispatches, and optional per-command GPU timing. Add an opt-in compiler timing mode that records shader hash/stage, input and emitted DXIL sizes, shader translation time, native PSO creation time, and whether creation came from the pipeline cache. A local analyzer groups these records, strips terminal color codes, redacts local paths, and writes a Markdown summary plus machine-readable JSON.

The report has two separate conclusions: render correctness blockers and frame-rate bottlenecks. It ranks the largest shader/PSO compiles separately from steady-state GPU/CPU costs. XCL findings are hypotheses for experiments, not code to copy wholesale: compact branch targets and static component selection are evaluated only where VIctory has equivalent generated code; descriptor-heap window access remains opt-in and safety-gated; compile scheduling is measured as warm-up throughput, not as an assumed FPS fix.

## Validation

- Parser tests cover ANSI and plain logs, absent metrics, repeated frame summaries, dropped-pipeline reasons, path redaction, and compiler timing records.
- Rust build and focused tests verify that opt-in timing leaves normal execution behavior unchanged.
- A controlled opening-scene run verifies visual output and reports a fixed warm interval; a separate cold run records shader/PSO warm-up.
- Performance changes are kept only when the same scene, cache state, and run duration show improvement without rendering or stability regressions.

## User decisions captured

- Analyze all practical sources in sequence: existing telemetry first, targeted instrumentation second, RenderDoc only where still needed.
- Correct world/assets/lighting/shader behavior before spending effort on steady-state FPS, except when profiling identifies a performance cause blocking correctness.
- Target 20–30 FPS at the opening scene.
