# Historical opening-scene log sample

This is a parsed summary of one log excerpt supplied in chat, timestamped `2026-11-24T23:00:26Z`. It is useful for prioritizing measurements, but it is **not** a controlled baseline for the current executable: the scene interval, exact build revision, resolution, GPU/driver, and cache state were not recorded with the excerpt. The raw log stays outside the repository.

## Parsed counters

- Reported rate: `0.4 FPS`, `2513.5 ms/frame`.
- Per-frame counters: `538` draws; `6351` indirect commands; `3030` draws/dispatches dropped without a host pipeline, with `1105` pipelines being made at that sample.
- CPU preparation: `593.7 ms/frame`; of that, title-buffer copies report `516.3 ms/frame` and `1406.3 MB/frame`. Texture refresh reports `26.4 ms/frame` and `54.8 MB/frame`; copy-back reports `13.2 ms/frame` and `0.1 MB/frame`.
- GPU wait/work counter: `46.7 ms/frame`. A separate card-clock line reports `2%` busy by its measurement and `51.8 ms` waiting for the card.
- Heap-buffer view preparation: `1.0` pass and `3201.4 ms` of accumulated pass time per frame. This is a separate scoped timer; do not add it to the frame categories because the instrumentation windows may overlap.
- Textures: `28,584` indexed texture slots waiting and `292` uploaded so far; `0` passed over as unmapped. Depth-texture UAV views refused: `0`.
- Detailed pipeline-drop counters in the excerpt include `8534/frame` attributed to pipelines being made, `846/frame` to the ray-tracing builder being off, and `1/frame` to a refused compute pipeline. These are separate counters from the main dropped-per-frame total and should not be summed together.
- The excerpt reports `667` live host command lists and `8,335` distinct buffer views retained.

## What this suggests

This sample is strongly CPU/copy/readiness limited: its GPU counter is far below the frame time, while buffer copies, heap-buffer view preparation, texture backlog, and pipeline creation all show substantial work. It does not yet prove which of those causes dominates a warmed current build, and it cannot establish that the whole scene's textures are missing or incorrect. The old run also had ray-tracing builder work disabled, so it says nothing about final ray-traced lighting quality.

The first controlled follow-up is to separate cold shader/PSO warm-up from a warm-cache opening-scene interval, then compare CPU preparation, copy volume, heap-buffer pass time, pipeline drops, and GPU time on the same build and scene. Avoid tuning graphics quality from this excerpt alone.
