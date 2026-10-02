import unittest
from unittest.mock import patch

from webui import app as solver_app


class AutomaticAttemptTests(unittest.TestCase):
    def test_fallback_attempt_is_reported_and_preserves_presolve_setting(self):
        job_id = "attempt-test"
        analysis = {
            "problem_type": "LP",
            "variables": 472,
            "constraints": 305,
            "nonzeros": 2494,
            "sparsity": 98.27,
            "types": {"continuous": 472, "integer": 0, "binary": 0},
            "variable_metadata": [],
        }
        job = {"analysis": analysis, "preparation_time_ms": 0.0}
        failed = {
            "status": "NUMERICAL_FAILURE", "verification": "FAIL", "variables": [],
            "timings": {"backend_total_time_ms": 12.0, "presolve_time_ms": 1.0, "solver_time_ms": 8.0},
            "metrics": {"iterations": 9}, "presolve": {},
        }
        recovered = {
            "status": "OPTIMAL", "verification": "PASS", "variables": [],
            "timings": {"backend_total_time_ms": 7.0, "solver_time_ms": 4.0},
            "metrics": {"iterations": 4}, "presolve": {},
        }
        calls = []

        def run_solver(_job_id, _job, configuration):
            calls.append(dict(configuration))
            return failed if len(calls) == 1 else recovered

        with patch.dict(solver_app.JOBS, {job_id: job}), \
             patch.object(solver_app, "device_info", return_value={"cuda_available": False}), \
             patch.object(solver_app, "_run_solver", side_effect=run_solver), \
             patch.object(solver_app, "_complete_result", side_effect=lambda _jid, _job, _cfg, result, automation: {**result, "automation": automation}), \
             patch.object(solver_app, "_publish_timing_contract"):
            result = solver_app.solve_automatically(solver_app.AutoSolveRequest(job_id=job_id))

        self.assertEqual(len(calls), 2)
        self.assertFalse(calls[0]["presolve"])
        self.assertFalse(calls[1]["presolve"])
        attempts = result["automation"]["attempts"]
        self.assertEqual([item["status"] for item in attempts], ["NUMERICAL_FAILURE", "OPTIMAL"])
        self.assertEqual([item["iterations"] for item in attempts], [9, 4])
        self.assertEqual([item["time_ms"] for item in attempts], [12.0, 7.0])


if __name__ == "__main__":
    unittest.main()
