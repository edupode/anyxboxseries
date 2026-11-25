#!/usr/bin/env python3
"""Turn a VIctory runtime log into a sanitized, machine-readable report."""

from __future__ import annotations

import argparse
import json
import math
import re
import statistics
from pathlib import Path
from typing import Any


ANSI_RE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
# Stop at common diagnostic delimiters rather than whitespace so paths such as
# `C:\Program Files\...` are redacted as one value. If a message has no
# delimiter after its path, the remaining text is conservatively redacted too.
WINDOWS_PATH_RE = re.compile(r"(?<![\w])(?:[A-Za-z]:[\\/]|\\\\)[^:;,|<>\"'\r\n]*(?=[:;,|<>\"']|$)")
FLOAT = r"-?(?:\d+(?:\.\d*)?|\.\d+)"

FRAME_RE = re.compile(
    rf"(?P<fps>{FLOAT})\s+fps,\s*(?P<frame_ms>{FLOAT})\s+ms/frame\s+"
    rf"\((?P<cpu_ms>{FLOAT})\s+preparing:\s*copies\s+(?P<copy_ms>{FLOAT})\s+ms\s+"
    rf"(?P<copy_mb>{FLOAT})\s+MB,\s*textures\s+(?P<texture_ms>{FLOAT})\s+ms\s+"
    rf"(?P<texture_mb>{FLOAT})\s+MB;\s*(?P<gpu_ms>{FLOAT})\s+on the GPU;\s*"
    rf"(?P<copy_back_ms>{FLOAT})\s+copying back\s+(?P<copy_back_mb>{FLOAT})\s+MB\),\s*"
    rf"(?P<draws>{FLOAT})\s+draws/frame,\s*dropped\s+(?P<dropped>{FLOAT})\s+"
    rf"without a host pipeline\s+\((?P<being_made>{FLOAT})\s+being made\)\s+and\s+"
    rf"(?P<indirect>{FLOAT})\s+indirect;\s*heap buffers\s+(?P<heap_buffers>{FLOAT})\s+"
    rf"passes\s+(?P<heap_pass_ms>{FLOAT})\s+ms"
)
DROP_RE = re.compile(rf"dropped\s+(?P<count>{FLOAT})/frame without a host pipeline:\s*(?P<reason>.*)$")
SHADER_FAIL_RE = re.compile(
    r"shader does not translate;.*?stage[=\s]+[\"']?(?P<stage>[\w-]+)[\"']?\s+"
    r"hash[=\s]+[\"']?(?P<hash>[0-9a-fA-F]+)[\"']?\s+error[=\s]+(?P<error>.*)$"
)
SHADER_COMPILE_RE = re.compile(
    rf"shader translated\s+stage=[\"']?(?P<stage>[\w-]+)[\"']?\s+"
    rf"hash=[\"']?(?P<hash>[0-9a-fA-F]+)[\"']?\s+"
    rf"source_bytes=(?P<source_bytes>\d+)\s+dxil_bytes=(?P<dxil_bytes>\d+)\s+"
    rf"elapsed_ms=(?P<elapsed_ms>{FLOAT})"
    rf"(?:\s+cache=[\"']?(?P<cache>hit|miss|unknown)[\"']?)?"
    rf"(?:\s+status=[\"']?(?P<status>[\w-]+)[\"']?)?"
)
PSO_COMPILE_RE = re.compile(
    rf"host PSO (?:created|ready|timing)\s+stages=[\"']?(?P<stages>[\w,+-]+)[\"']?\s+"
    rf"hashes=[\"']?(?P<hashes>[0-9a-fA-F,]+)[\"']?\s+"
    rf"dxil_bytes=(?P<dxil_bytes>\d+)\s+elapsed_ms=(?P<elapsed_ms>{FLOAT})"
    rf"(?:\s+cache=[\"']?(?P<cache>hit|miss)[\"']?)?"
    rf"(?:\s+status=[\"']?(?P<status>[\w-]+)[\"']?)?"
)
TEXTURE_COUNTER_RE = re.compile(
    r"texture slots written so far:\s*(?P<placed>\d+) placed,\s*(?P<objects>\d+) of objects;\s*"
    r"textures picked by index:\s*(?P<uploaded>\d+) uploaded so far\s*\((?P<uploaded_mb>\d+) MB\),\s*"
    r"(?P<waiting>\d+) waiting,\s*(?P<unmapped>\d+) passed over unmapped"
)
DEPTH_COUNTER_RE = re.compile(
    r"depth textures with\s+unordered access:\s*(?P<made>\d+) made,\s*"
    r"bound as depth\s*(?P<bound>\d+)x,\s*cleared as depth\s*(?P<cleared>\d+)x,\s*"
    r"unordered views refused\s*(?P<refused>\d+)x"
)


def _clean(line: str) -> str:
    return ANSI_RE.sub("", line).replace("\r", "")


def _redact(value: str) -> str:
    value = WINDOWS_PATH_RE.sub("<path>", value)
    # A UNC path can be followed by punctuation that should remain in the log.
    return value


def _number_fields(match: re.Match[str], integer_fields: set[str] | None = None) -> dict[str, Any]:
    integer_fields = integer_fields or set()
    fields: dict[str, Any] = {}
    for key, value in match.groupdict().items():
        if value is None:
            fields[key] = None
        elif key in integer_fields:
            fields[key] = int(value)
        else:
            try:
                fields[key] = float(value)
            except ValueError:
                fields[key] = value
    return fields


def parse_log(text: str) -> dict[str, Any]:
    """Parse relevant runtime diagnostics; unknown/malformed rows are ignored."""
    parsed: dict[str, Any] = {
        "frame_samples": [],
        "pipeline_drops": [],
        "shader_failures": [],
        "shader_compiles": [],
        "pso_compiles": [],
        "texture_counters": [],
        "depth_texture_counters": [],
    }
    for raw_line in text.splitlines():
        line = _clean(raw_line)
        if match := FRAME_RE.search(line):
            parsed["frame_samples"].append(_number_fields(match))
        if match := DROP_RE.search(line):
            parsed["pipeline_drops"].append(
                {"per_frame": float(match.group("count")), "reason": _redact(match.group("reason").strip())}
            )
        if match := SHADER_FAIL_RE.search(line):
            error = match.group("error").strip().strip("\"")
            parsed["shader_failures"].append(
                {
                    "stage": match.group("stage"),
                    "hash": match.group("hash").lower(),
                    "error": _redact(error),
                }
            )
        if match := SHADER_COMPILE_RE.search(line):
            parsed["shader_compiles"].append(
                _number_fields(match, {"source_bytes", "dxil_bytes"})
            )
        if match := PSO_COMPILE_RE.search(line):
            parsed["pso_compiles"].append(
                {
                    "stages": match.group("stages"),
                    "hashes": match.group("hashes").lower().split(","),
                    "dxil_bytes": int(match.group("dxil_bytes")),
                    "elapsed_ms": float(match.group("elapsed_ms")),
                    "cache": match.group("cache"),
                    "status": match.group("status"),
                }
            )
        if match := TEXTURE_COUNTER_RE.search(line):
            parsed["texture_counters"].append(_number_fields(match, set(match.groupdict())))
        if match := DEPTH_COUNTER_RE.search(line):
            parsed["depth_texture_counters"].append(_number_fields(match, set(match.groupdict())))
    return parsed


def _percentile(values: list[float], percentile: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    index = max(0, math.ceil(percentile * len(ordered)) - 1)
    return ordered[index]


def _optional_average(items: list[dict[str, Any]], key: str) -> float | None:
    if not items:
        return None
    return round(statistics.fmean(item[key] for item in items), 3)


def summarize(parsed: dict[str, Any]) -> dict[str, Any]:
    frames = parsed["frame_samples"]
    missing: list[str] = []
    if not frames:
        missing.append("frame_timing")
        missing.append("copy_gpu_breakdown")
    if not parsed["texture_counters"]:
        missing.append("texture_counters")
    if not parsed["depth_texture_counters"]:
        missing.append("depth_texture_counters")
    if not parsed["pipeline_drops"]:
        missing.append("pipeline_drop_reasons")
    if not parsed["shader_compiles"]:
        missing.append("shader_compile_timing")
    if not parsed["pso_compiles"]:
        missing.append("pso_compile_timing")
    if not parsed["shader_failures"]:
        missing.append("shader_translation_failures")

    texture_latest = parsed["texture_counters"][-1] if parsed["texture_counters"] else None
    depth_latest = parsed["depth_texture_counters"][-1] if parsed["depth_texture_counters"] else None
    frame_metrics = {
        "frame_count": len(frames),
        "median_fps": round(statistics.median(row["fps"] for row in frames), 3) if frames else None,
        "median_frame_ms": round(statistics.median(row["frame_ms"] for row in frames), 3) if frames else None,
        "p95_frame_ms": _percentile([row["frame_ms"] for row in frames], 0.95),
        "average_cpu_prepare_ms": _optional_average(frames, "cpu_ms"),
        "average_gpu_ms": _optional_average(frames, "gpu_ms"),
        "average_copy_ms": _optional_average(frames, "copy_ms"),
        "average_copy_mb": _optional_average(frames, "copy_mb"),
        "average_texture_ms": _optional_average(frames, "texture_ms"),
        "average_texture_mb": _optional_average(frames, "texture_mb"),
        "average_copy_back_ms": _optional_average(frames, "copy_back_ms"),
        "average_copy_back_mb": _optional_average(frames, "copy_back_mb"),
        "average_draws_per_frame": _optional_average(frames, "draws"),
        "average_dropped_pipelines_per_frame": _optional_average(frames, "dropped"),
        "average_pipelines_being_made": _optional_average(frames, "being_made"),
        "average_indirect_per_frame": _optional_average(frames, "indirect"),
        "average_heap_buffer_passes": _optional_average(frames, "heap_buffers"),
        "average_heap_buffer_pass_ms": _optional_average(frames, "heap_pass_ms"),
    }
    shader_misses = [row for row in parsed["shader_compiles"] if row.get("cache") not in {"hit", "unknown"}]
    shader_hits = [row for row in parsed["shader_compiles"] if row.get("cache") == "hit"]
    shader_unknown = [row for row in parsed["shader_compiles"] if row.get("cache") == "unknown"]
    pso_misses = [row for row in parsed["pso_compiles"] if row["cache"] != "hit"]
    pso_hits = [row for row in parsed["pso_compiles"] if row["cache"] == "hit"]
    compile_metrics = {
        "shader_compile_count": len(shader_misses),
        "shader_translation_ms_total": round(sum(row["elapsed_ms"] for row in shader_misses), 3),
        "shader_dxil_bytes_total": sum(row["dxil_bytes"] for row in shader_misses),
        "shader_timing_row_count": len(parsed["shader_compiles"]),
        "shader_cache_hit_count": len(shader_hits),
        "shader_cache_lookup_ms_total": round(sum(row["elapsed_ms"] for row in shader_hits), 3),
        "shader_cache_unknown_count": len(shader_unknown),
        "pso_compile_count": len(pso_misses),
        "pso_creation_ms_total": round(sum(row["elapsed_ms"] for row in pso_misses), 3),
        "pso_cache_hit_count": len(pso_hits),
        "pso_cache_lookup_ms_total": round(sum(row["elapsed_ms"] for row in pso_hits), 3),
    }
    return {
        "schema_version": 1,
        **frame_metrics,
        **compile_metrics,
        "pipeline_drop_reason_count": len(parsed["pipeline_drops"]),
        "shader_translation_failure_count": len(parsed["shader_failures"]),
        "latest_texture_counters": texture_latest,
        "latest_depth_texture_counters": depth_latest,
        "missing_metrics": missing,
    }


def make_report(parsed: dict[str, Any], report: dict[str, Any]) -> str:
    def display(value: Any) -> str:
        return "unavailable" if value is None else str(value)

    lines = [
        "# VIctory run diagnostics",
        "",
        "This report contains parsed runtime counters only. Missing fields are marked unavailable; no values are inferred.",
        "",
        "## Frame and copy timing",
        "",
        f"- Samples: {report['frame_count']}",
        f"- Median FPS: {report['median_fps'] if report['median_fps'] is not None else 'unavailable'}",
        f"- Median frame time: {report['median_frame_ms'] if report['median_frame_ms'] is not None else 'unavailable'} ms",
        f"- P95 frame time: {report['p95_frame_ms'] if report['p95_frame_ms'] is not None else 'unavailable'} ms",
        f"- Average CPU preparation / GPU / copy / texture / copy-back: {display(report['average_cpu_prepare_ms'])} / {display(report['average_gpu_ms'])} / {display(report['average_copy_ms'])} / {display(report['average_texture_ms'])} / {display(report['average_copy_back_ms'])} ms",
        f"- Average copied / texture / copied-back data: {display(report['average_copy_mb'])} / {display(report['average_texture_mb'])} / {display(report['average_copy_back_mb'])} MB per frame",
        f"- Average draws / dropped pipelines: {display(report['average_draws_per_frame'])} / {display(report['average_dropped_pipelines_per_frame'])} per frame",
        f"- Average indirect draws / pipelines being made: {display(report['average_indirect_per_frame'])} / {display(report['average_pipelines_being_made'])} per frame",
        f"- Heap-buffer pass time: {display(report['average_heap_buffer_pass_ms'])} ms across {display(report['average_heap_buffer_passes'])} passes per frame (separate timer; may overlap frame components)",
        "",
        "## Pipeline and shader issues",
        "",
        f"- Detailed pipeline drop rows: {report['pipeline_drop_reason_count']}",
        f"- Shader translation failures: {report['shader_translation_failure_count']}",
        f"- Shader cache misses / translation time: {report['shader_compile_count']} / {report['shader_translation_ms_total']} ms",
        f"- Shader cache hits / lookup time: {report['shader_cache_hit_count']} / {report['shader_cache_lookup_ms_total']} ms",
        f"- Native PSO cache misses / creation time: {report['pso_compile_count']} / {report['pso_creation_ms_total']} ms",
        f"- Native PSO cache hits / lookup time: {report['pso_cache_hit_count']} / {report['pso_cache_lookup_ms_total']} ms",
        "",
        "### Pipeline drop reasons",
        "",
    ]
    if parsed["pipeline_drops"]:
        lines.extend(f"- {row['per_frame']:.1f}/frame: {_redact(row['reason'])}" for row in parsed["pipeline_drops"][:40])
    else:
        lines.append("- unavailable")
    lines.extend(["", "### Shader translation failures", ""])
    if parsed["shader_failures"]:
        lines.extend(
            f"- `{row['stage']}` `{row['hash']}`: {_redact(row['error'])}"
            for row in parsed["shader_failures"][:100]
        )
    else:
        lines.append("- unavailable")
    lines.extend(["", "## Latest texture counters", ""])
    if report["latest_texture_counters"] is None:
        lines.append("- unavailable")
    else:
        lines.append("- " + ", ".join(f"{key}: {value}" for key, value in report["latest_texture_counters"].items()))
    lines.extend(["", "## Latest depth-texture counters", ""])
    if report["latest_depth_texture_counters"] is None:
        lines.append("- unavailable")
    else:
        lines.append("- " + ", ".join(f"{key}: {value}" for key, value in report["latest_depth_texture_counters"].items()))
    lines.extend(["", "## Missing diagnostics", ""])
    lines.extend(f"- {name}" for name in report["missing_metrics"])
    if not report["missing_metrics"]:
        lines.append("- none detected")
    lines.append("")
    return "\n".join(lines)


def analyze_file(log_path: Path, output_dir: Path) -> tuple[Path, Path]:
    text = log_path.read_text(encoding="utf-8", errors="replace")
    parsed = parse_log(text)
    report = summarize(parsed)
    output_dir.mkdir(parents=True, exist_ok=True)
    json_path = output_dir / "metrics.json"
    markdown_path = output_dir / "report.md"
    json_path.write_text(
        json.dumps({"summary": report, "details": parsed}, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    markdown_path.write_text(make_report(parsed, report), encoding="utf-8")
    return markdown_path, json_path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="VIctory runtime log (kept local; paths are redacted from reports)")
    parser.add_argument("--out-dir", type=Path, required=True, help="directory for report.md and metrics.json")
    args = parser.parse_args()
    markdown_path, json_path = analyze_file(args.log, args.out_dir)
    print(f"Wrote {markdown_path.name} and {json_path.name} to the selected output directory.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
