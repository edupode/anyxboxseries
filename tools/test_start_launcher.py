import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LAUNCHER = ROOT / "Start-SignalRun.cmd"


class SignalRunLauncherTests(unittest.TestCase):
    def test_starts_the_sample_next_to_the_launcher(self):
        script = LAUNCHER.read_text(encoding="utf-8")
        self.assertIn('set "WSX_BOOTSTRAP=%~dp0bin\\wsx_bootstrap.exe"', script)
        self.assertIn('if not exist "%WSX_BOOTSTRAP%"', script)
        self.assertIn('start "" "%WSX_BOOTSTRAP%" --sample', script)


if __name__ == "__main__":
    unittest.main()
