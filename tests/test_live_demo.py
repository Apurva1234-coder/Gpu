import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from webui.app import _configured_solver_path, final_benchmark_reports


class LiveDemoTests(unittest.TestCase):
    def test_solver_executable_can_be_configured(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "sovereign.exe"
            executable.write_bytes(b"demo")
            with patch.dict(os.environ, {"SOVEREIGN_SOLVER_EXECUTABLE": str(executable)}):
                selected = _configured_solver_path("SOVEREIGN_SOLVER_EXECUTABLE", [], "missing")
            self.assertEqual(selected, executable.resolve())

    def test_solver_executable_configuration_reports_missing_path(self):
        with patch.dict(os.environ, {"SOVEREIGN_SOLVER_EXECUTABLE": "does-not-exist.exe"}):
            with self.assertRaisesRegex(FileNotFoundError, "SOVEREIGN_SOLVER_EXECUTABLE"):
                _configured_solver_path("SOVEREIGN_SOLVER_EXECUTABLE", [], "missing")

    def test_final_reports_read_stored_lp_milp_and_qp_results(self):
        report = final_benchmark_reports()
        self.assertTrue(report["available"])
        rows = {row["dataset"]: row for row in report["records"]}
        self.assertEqual(rows["AFIRO"]["objective"], -464.75314285714285)
        self.assertEqual(rows["FLUGPL"]["verification"], "PASS")
        self.assertEqual(rows["QPLIB_9002"]["verification"], "KKT PASS")
        self.assertEqual(rows["traininstance2"]["status"], "TIME_LIMIT_NO_INCUMBENT")
        self.assertIsNone(rows["traininstance2"]["objective"])


if __name__ == "__main__":
    unittest.main()
