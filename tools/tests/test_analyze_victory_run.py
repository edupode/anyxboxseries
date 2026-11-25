import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))

from analyze_victory_run import analyze_file, parse_log, summarize


class AnalyzeVictoryRunTests(unittest.TestCase):
    def test_parses_frame_costs_and_calculates_tail_latency(self):
        log = """
2026-11-25T10:00:00Z INFO victory_gpu::host: 3.2 fps, 312.5 ms/frame (280.0 preparing: copies 10.0 ms 8.0 MB, textures 5.0 ms 12.0 MB; 245.0 on the GPU; 35.0 copying back 16.0 MB), 600 draws/frame, dropped 2.0 without a host pipeline (1 being made) and 2 indirect; heap buffers 1.0 passes 40.0 ms
2026-11-25T10:00:05Z INFO victory_gpu::host: 2.0 fps, 500.0 ms/frame (450.0 preparing: copies 20.0 ms 16.0 MB, textures 10.0 ms 20.0 MB; 400.0 on the GPU; 50.0 copying back 32.0 MB), 700 draws/frame, dropped 4.0 without a host pipeline (2 being made) and 6 indirect; heap buffers 3.0 passes 80.0 ms
"""
        parsed = parse_log(log)
        report = summarize(parsed)

        self.assertEqual(len(parsed["frame_samples"]), 2)
        self.assertEqual(parsed["frame_samples"][0]["copy_back_mb"], 16.0)
        self.assertEqual(report["median_fps"], 2.6)
        self.assertEqual(report["p95_frame_ms"], 500.0)
        self.assertEqual(report["average_dropped_pipelines_per_frame"], 3.0)
        self.assertEqual(report["average_pipelines_being_made"], 1.5)
        self.assertEqual(report["average_indirect_per_frame"], 4.0)
        self.assertEqual(report["average_heap_buffer_passes"], 2.0)
        self.assertEqual(report["average_heap_buffer_pass_ms"], 60.0)

    def test_strips_ansi_and_groups_pipeline_drop_reasons(self):
        log = "\x1b[2m2026-11-25T10:00:00Z\x1b[0m \x1b[32mINFO\x1b[0m victory_gpu::host: dropped 5.0/frame without a host pipeline: refused [mesh ff2afa5e9a460ff0, pixel 3084ebdc38f9b9ec]"
        parsed = parse_log(log)

        self.assertEqual(parsed["pipeline_drops"][0]["per_frame"], 5.0)
        self.assertIn("mesh ff2afa5e9a460ff0", parsed["pipeline_drops"][0]["reason"])

    def test_extracts_shader_failures_and_redacts_local_paths(self):
        log = '2026-11-25T10:00:00Z WARN victory_gpu::pipeline: shader does not translate; its pipelines draws are dropped stage="compute" hash=0c0120fcb5aac198 error="failed reading C:\\Users\\edupo\\private\\shader.dxil: unsupported lane mask"'
        parsed = parse_log(log)

        self.assertEqual(parsed["shader_failures"][0]["stage"], "compute")
        self.assertEqual(parsed["shader_failures"][0]["hash"], "0c0120fcb5aac198")
        self.assertNotIn("C:\\Users", parsed["shader_failures"][0]["error"])
        self.assertIn("<path>", parsed["shader_failures"][0]["error"])

    def test_parses_opt_in_shader_and_pso_timing_records(self):
        log = """
INFO victory_gpu::pipeline: shader translated stage="compute" hash=abcdef0123456789 source_bytes=16384 dxil_bytes=1249504 elapsed_ms=400000.2 cache="miss" status="ok"
INFO victory_gpu::pipeline: shader translated stage="pixel" hash=0123456789abcdef source_bytes=4096 dxil_bytes=8192 elapsed_ms=1.5 cache="hit" status="ok"
INFO victory_gpu::pipeline: host PSO created stages=compute hashes=abcdef0123456789 dxil_bytes=1249504 elapsed_ms=33160.0
"""
        parsed = parse_log(log)

        self.assertEqual(parsed["shader_compiles"][0]["stage"], "compute")
        self.assertEqual(parsed["shader_compiles"][0]["hash"], "abcdef0123456789")
        self.assertEqual(parsed["shader_compiles"][0]["dxil_bytes"], 1249504)
        self.assertEqual(parsed["shader_compiles"][0]["elapsed_ms"], 400000.2)
        self.assertEqual(parsed["shader_compiles"][0]["cache"], "miss")
        self.assertEqual(parsed["pso_compiles"][0]["stages"], "compute")
        self.assertEqual(parsed["pso_compiles"][0]["hashes"], ["abcdef0123456789"])
        self.assertEqual(parsed["pso_compiles"][0]["elapsed_ms"], 33160.0)
        report = summarize(parsed)
        self.assertEqual(report["shader_compile_count"], 1)
        self.assertEqual(report["shader_cache_hit_count"], 1)

    def test_separates_pipeline_library_hits_from_native_pso_creation(self):
        parsed = parse_log(
            'INFO victory_gpu::pipeline: host PSO timing stages=mesh,pixel '
            'hashes=abcdef0123456789,0123456789abcdef dxil_bytes=8192 '
            'elapsed_ms=2.5 cache="hit" status="loaded"'
        )
        report = summarize(parsed)

        self.assertEqual(report["pso_compile_count"], 0)
        self.assertEqual(report["pso_cache_hit_count"], 1)
        self.assertEqual(report["pso_cache_lookup_ms_total"], 2.5)

    def test_empty_log_writes_readable_outputs_with_unavailable_metrics(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            log_path = root / "empty.log"
            output_dir = root / "report"
            log_path.write_text("not a VIctory log\n", encoding="utf-8")

            markdown_path, json_path = analyze_file(log_path, output_dir)

            report = markdown_path.read_text(encoding="utf-8")
            metrics = json_path.read_text(encoding="utf-8")
            self.assertIn("Median FPS: unavailable", report)
            self.assertIn("Average CPU preparation / GPU / copy / texture / copy-back: unavailable / unavailable / unavailable / unavailable / unavailable ms", report)
            self.assertIn("Average copied / texture / copied-back data: unavailable / unavailable / unavailable MB per frame", report)
            self.assertIn("Average draws / dropped pipelines: unavailable / unavailable per frame", report)
            self.assertIn("Heap-buffer pass time: unavailable ms across unavailable passes per frame", report)
            self.assertIn('"schema_version": 1', metrics)
            self.assertNotIn(str(root), report + metrics)

    def test_fixture_parses_resource_and_pipeline_diagnostics(self):
        fixture = Path(__file__).parent / "fixtures" / "diagnostic-run.log"
        parsed = parse_log(fixture.read_text(encoding="utf-8"))
        report = summarize(parsed)

        self.assertEqual(report["latest_texture_counters"]["placed"], 23)
        self.assertEqual(report["latest_depth_texture_counters"]["refused"], 1)
        self.assertEqual(report["pipeline_drop_reason_count"], 1)
        self.assertEqual(report["shader_translation_failure_count"], 1)

    def test_generated_report_redacts_paths_from_parsed_failures(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            log_path = root / "private.log"
            output_dir = root / "report"
            log_path.write_text(
                'WARN victory_gpu::pipeline: shader does not translate; its pipelines draws are dropped stage="compute" '
                'hash=0123456789abcdef error="C:\\Users\\secret\\shader.dxil: bad opcode"\n',
                encoding="utf-8",
            )

            markdown_path, json_path = analyze_file(log_path, output_dir)
            outputs = markdown_path.read_text(encoding="utf-8") + json_path.read_text(encoding="utf-8")

            self.assertIn("<path>", outputs)
            self.assertNotIn("C:\\Users", outputs)
            self.assertNotIn("secret", outputs)

    def test_redacts_local_paths_that_contain_spaces(self):
        parsed = parse_log(
            'WARN victory_gpu::pipeline: shader does not translate; its pipelines draws are dropped '
            'stage="pixel" hash=0123456789abcdef '
            'error="failed reading C:\\Program Files\\Private Data\\shader.dxil: bad opcode"'
        )

        error = parsed["shader_failures"][0]["error"]
        self.assertNotIn("Program Files", error)
        self.assertNotIn("Private Data", error)
        self.assertIn("<path>", error)

    def test_reports_missing_metrics_instead_of_inventing_zeroes(self):
        parsed = parse_log("startup completed")
        report = summarize(parsed)

        self.assertEqual(report["frame_count"], 0)
        self.assertIsNone(report["median_fps"])
        self.assertIn("frame_timing", report["missing_metrics"])


if __name__ == "__main__":
    unittest.main()
