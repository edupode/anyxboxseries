# VIctory diagnostics

`tools/analyze_victory_run.py` converts a local VIctory runtime log to a shareable Markdown summary and JSON metrics file. It uses only Python's standard library.

```powershell
python tools/analyze_victory_run.py "C:\path\to\victory.log" --out-dir "C:\path\to\report"
```

The output directory contains `report.md` and `metrics.json`. Keep the raw log local: the report drops unrecognized rows and replaces detected Windows paths in parsed error/reason fields with `<path>`. The JSON includes schema version 1 and detailed parsed samples for reproducible comparisons.

The parser reads VIctory frame summaries, per-frame pipeline drop reasons, shader translation failures, texture/depth-texture counters, and optional compiler timing rows. `VICTORY_COMPILER_TIMING=1` enables per-shader translation timing and native pipeline cache/creation timing; leave it unset for ordinary runs. Compiler cache hits are reported separately from native PSO creation misses.

For useful performance comparisons, record the exact scene interval, executable revision, GPU/driver, resolution, and whether the shader and host pipeline caches are cold or warm. Do not compare a cold compile/warm-up interval against steady-state FPS. The analyzer cannot infer that a rendered output is visually correct, identify the current scene from counters alone, prove ray-tracing builder writes are safe, or attribute GPU time to individual draw calls. Missing measurements appear as `unavailable`; they are not treated as zero.
