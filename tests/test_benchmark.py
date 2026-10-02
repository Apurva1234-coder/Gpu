import argparse
import gzip
import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from sovereign_solver import benchmark
from sovereign_solver.parser import parse_problem_file


ROOT = Path(__file__).resolve().parents[1]
FAKE_OUTPUT = """MODEL fixture
Backend: CPU
Presolve: ON
Presolve time budget ms: 500
Presolve termination: diminishing_returns
Parse time ms: 0.1
Presolve time ms: 0.0
Presolve pass: 1 time_ms=0.2 variables=3->2 constraints=2->1 nnz=4->2 bound_tightenings=1 fixed_variables=1 substitutions=0 singleton_reductions=0 redundant_rows=1 reduction_percent=40.0
Standardization time ms: 0.3
Solve pipeline time ms: 0.7
Solve time ms: 0.4
Standardized rows: 1
Standardized columns: 2
Standardized nonzeros: 2
Final variables: 1
Final constraints: 1
Presolve reductions: 0
Presolve fallback: YES
Estimated dense memory bytes: 512000000
Dense memory budget bytes: 268435456
Dense memory guard: TRIGGERED
Sparse pricing time ms: 1.25
Sparse basis solve time ms: 2.5
Sparse Devex time ms: 3.75
Sparse factorization time ms: 5
Sparse ratio test time ms: 6.25
Sparse lexicographic time ms: 0.75
Sparse refactorizations: 4
Sparse pivots: 19
Sparse lexicographic solves: 7
Sparse Bland fallback: YES
Problem Type: LP
Status: OPTIMAL
Iterations: 2
Objective: 1650
Feasibility: 0
Verification: PASS
Primal: 15 17.5
"""


class BenchmarkTests(unittest.TestCase):
    def test_presolve_pass_and_standardization_telemetry_is_parsed(self):
        passes = benchmark._presolve_pass_stats(FAKE_OUTPUT)
        self.assertEqual(len(passes), 1)
        self.assertEqual(passes[0]["pass"], 1)
        self.assertEqual(passes[0]["variables"], "3->2")
        self.assertEqual(passes[0]["fixed_variables"], 1)
        self.assertEqual(benchmark._first_number(benchmark._field(FAKE_OUTPUT, "Standardization time ms")), 0.3)
        self.assertEqual(benchmark._int(benchmark._field(FAKE_OUTPUT, "Standardized nonzeros")), 2)
        self.assertEqual(benchmark._field(FAKE_OUTPUT, "Presolve termination"), "diminishing_returns")
        self.assertEqual(benchmark._first_number(benchmark._field(FAKE_OUTPUT, "Presolve time budget ms")), 500.0)

    def test_netlib_mps_fixture_loads_as_lp(self):
        model = parse_problem_file(str(ROOT / "examples" / "afiro.mps"))
        self.assertGreater(len(model.variables), 0)
        self.assertGreater(len(model.constraints), 0)
        self.assertEqual([v.name for v in model.variables[:4]], ["X01", "X02", "X03", "X04"])

    def test_netlib_emps_is_rejected_without_unverified_conversion(self):
        emps = "NAME ADLITTLE\n1 2 3 4 5 6 7 8\n9 10 11\n12 13 14\n"
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "adlittle.mps.txt"
            path.write_text(emps, encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "cannot verify coefficient fidelity"):
                parse_problem_file(str(path))

    def test_miplib_style_integer_mps_preserves_bounds(self):
        text = """NAME TESTMILP
ROWS
 N OBJ
 G DEMAND
COLUMNS
 MARK0000 'MARKER' 'INTORG'
 X OBJ 1 DEMAND 1
 MARK0001 'MARKER' 'INTEND'
RHS
 RHS1 DEMAND 2
BOUNDS
 BV BND X
ENDATA
"""
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "tiny.mps"
            path.write_text(text, encoding="utf-8")
            model = parse_problem_file(str(path))
        self.assertEqual(model.variables[0].type, "binary")
        self.assertEqual(model.bounds["X"], (0.0, 1.0))

    def test_mps_ranges_and_binary_bounds_are_preserved(self):
        text = """NAME RANGE
OBJSENSE
 MAX
ROWS
 N OBJ
 L CAP
COLUMNS
 X OBJ 1 CAP 1
RHS
 R CAP 4
 ALT CAP 99
RANGES
 R CAP 2
 ALT CAP 50
BOUNDS
 BV BND X
 UP OTHER X 0.5
ENDATA
"""
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "range.mps"
            path.write_text(text, encoding="utf-8")
            model = parse_problem_file(str(path))
        self.assertEqual(model.variables[0].type, "binary")
        self.assertEqual(model.bounds["X"], (0.0, 1.0))
        self.assertEqual(model.objective_sense, "maximize")
        self.assertEqual([(row.operator, row.rhs) for row in model.constraints], [(">=", 4.0), ("<=", 6.0)])

    def test_gzip_compressed_miplib_mps_loads_without_staging_in_parser(self):
        text = """NAME GZIPMILP
ROWS
 N OBJ
 G NEED
COLUMNS
 MARK0000 'MARKER' 'INTORG'
 X OBJ 1 NEED 1
 MARK0001 'MARKER' 'INTEND'
RHS
 RHS1 NEED 1
BOUNDS
 BV BND X
ENDATA
"""
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "case.mps.gz"
            with gzip.open(path, "wt", encoding="utf-8") as compressed:
                compressed.write(text)
            model = parse_problem_file(str(path))
        self.assertEqual(model.variables[0].type, "binary")
        self.assertEqual(model.bounds["X"], (0.0, 1.0))

    def test_gzip_solver_input_is_streamed_to_temporary_mps(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            compressed_path = root / "case.mps.gz"
            with gzip.open(compressed_path, "wt", encoding="utf-8") as compressed:
                compressed.write("NAME T\nENDATA\n")
            seen = {}
            def fake_run(command, **kwargs):
                staged = Path(command[command.index("--input") + 1])
                seen["exists_during_call"] = staged.exists()
                seen["content"] = staged.read_text(encoding="utf-8")
                return subprocess.CompletedProcess(command, 0, "Status: OPTIMAL\nPrimal: 0\n", "")
            with patch.object(benchmark.subprocess, "run", side_effect=fake_run):
                result = benchmark._invoke(Path("solver.exe"), compressed_path, [], "cpu", 0, 1)
            self.assertEqual(result["status"], "OPTIMAL")
            self.assertTrue(seen["exists_during_call"])
            self.assertIn("ENDATA", seen["content"])
            self.assertEqual(list(root.glob("sovereign_benchmark_*")), [])

    def test_netlib_emps_cannot_reach_solver_until_fidelity_is_verified(self):
        solver = ROOT / "cpp_solver" / "build" / "sovereign_presolve_cli.exe"
        with tempfile.TemporaryDirectory() as tmp:
            inputs = Path(tmp) / "inputs"
            inputs.mkdir()
            (inputs / "afiro.mps.txt").write_text(
                "NAME AFIRO\n1 2 3 4 5 6 7 8\n9 10 11\n12 13 14\n", encoding="utf-8")
            args = argparse.Namespace(input=str(inputs), solver=str(solver), output_dir=str(Path(tmp) / "out"),
                dataset="netlib", device=0, tier="quick", limit=1, method="revised-simplex", backend="cpu",
                no_presolve=False, time_limit=5.0, feasibility_tol=1e-7, objective_tol=1e-6,
                compare_highs=False, compare_backends=False, compare_presolve=False)
            completed = subprocess.CompletedProcess([], 0, "CUDA Available: NO", "")
            with patch.object(benchmark.subprocess, "run", return_value=completed):
                report = benchmark.run_benchmark(args)
        self.assertEqual(report["rows"][0]["status"], "UNSUPPORTED")
        self.assertIn("cannot verify coefficient fidelity", report["rows"][0]["failure_reason"])

    def test_highs_runtime_ratio_is_solver_only_and_total_is_separate(self):
        values = benchmark._runtime_comparison(4.0, 12.0, 2.0)
        self.assertEqual(values["runtime_ratio"], 2.0)
        self.assertEqual(values["speedup_vs_highs"], 0.5)
        self.assertEqual(values["our_runtime_ms"], 4.0)
        self.assertEqual(values["our_total_time_ms"], 12.0)

    def test_qplib_qp_style_supported_diagonal_input_classifies(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "convex.json"
            path.write_text(json.dumps({"name": "qplib style", "objective_sense": "minimize",
                "variables": [{"name": "x"}], "objective": {"x": 0}, "quadratic_terms": {"x": 2},
                "constraints": [{"name": "bound", "coefficients": {"x": 1}, "operator": ">=", "rhs": 0}]}))
            model = parse_problem_file(str(path))
        self.assertEqual(benchmark.classify_model(model).problem_type, "QP")

    def test_standard_qplib_extension_is_explicitly_unsupported_by_runner(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            inputs = root / "inputs"
            inputs.mkdir()
            (inputs / "QPLIB_0018.qplib").write_text("QPLIB_0018\nQCL\n", encoding="utf-8")
            solver = root / "solver.exe"
            solver.touch()
            args = argparse.Namespace(input=str(inputs), solver=str(solver), output_dir=str(root / "results"),
                dataset="qplib", device=0, tier="quick", limit=1, method="qp", backend="cpu", no_presolve=False,
                time_limit=5.0, feasibility_tol=1e-7, objective_tol=1e-6, compare_highs=False,
                compare_backends=False, compare_presolve=False)
            completed = subprocess.CompletedProcess([], 0, "CUDA Available: NO", "")
            with patch.object(benchmark.subprocess, "run", return_value=completed):
                report = benchmark.run_benchmark(args)
        self.assertEqual(report["rows"][0]["status"], "UNSUPPORTED")
        self.assertIn(".mps, .json, and .txt", report["rows"][0]["failure_reason"])

    def test_qp_convexity_fields_are_captured(self):
        result = benchmark._parse_solver_output("Status: OPTIMAL\nConvexity: CONVEX\nHessian: POSITIVE_SEMIDEFINITE\n", 0)
        self.assertEqual(result["convexity"], "CONVEX")
        self.assertEqual(result["hessian_type"], "POSITIVE_SEMIDEFINITE")

    def test_presolve_fallback_is_captured(self):
        result = benchmark._parse_solver_output("Status: OPTIMAL\nPresolve fallback: YES\n", 0)
        self.assertTrue(result["presolve_fallback"])

    def test_dense_memory_guard_telemetry_is_captured(self):
        result = benchmark._parse_solver_output(FAKE_OUTPUT, 0)
        self.assertEqual(result["estimated_dense_memory_bytes"], 512000000.0)
        self.assertEqual(result["dense_memory_budget_bytes"], 268435456)
        self.assertTrue(result["dense_memory_guard_triggered"])

    def test_sparse_simplex_profile_is_captured(self):
        result = benchmark._parse_solver_output(FAKE_OUTPUT, 0)
        self.assertEqual(result["sparse_pricing_ms"], 1.25)
        self.assertEqual(result["sparse_factorization_ms"], 5.0)
        self.assertEqual(result["sparse_lexicographic_ms"], 0.75)
        self.assertEqual(result["sparse_refactorizations"], 4)
        self.assertEqual(result["sparse_pivots"], 19)
        self.assertEqual(result["sparse_lexicographic_solves"], 7)
        self.assertTrue(result["sparse_bland_fallback_triggered"])

    def test_original_model_verification_bounds_integrality_and_objective(self):
        model = parse_problem_file(str(ROOT / "examples" / "milp_relaxation.json"))
        result = benchmark.verify_original(model, [1, 0])
        self.assertTrue(result["pass"])
        self.assertTrue(result["integer_feasible"])

    def test_named_primal_mapping_is_reordered_to_original_model(self):
        model = parse_problem_file(str(ROOT / "examples" / "lp.json"))
        # Solver emits its own variable order (y, x); verification must map by name.
        result = benchmark.verify_original(model, [17.5, 15], 1e-7, ["y", "x"])
        self.assertTrue(result["pass"])
        self.assertEqual(result["objective"], 1650)

    def test_zero_gap_values_are_not_lost(self):
        result = benchmark._parse_solver_output("Status: OPTIMAL\nPrimal Bound: 0\nDual Bound: 0\n", 0)
        self.assertEqual(result["best_primal_bound"], 0.0)
        self.assertEqual(result["best_dual_bound"], 0.0)

    def test_milp_gap_metrics_parsed(self):
        result = benchmark._parse_solver_output("Status: OPTIMAL\nAbsolute Gap: 0\nRelative Gap: 0\nNodes Created: 4\nLP Solves: 9\n", 0)
        self.assertEqual(result["absolute_gap"], 0.0)
        self.assertEqual(result["relative_gap"], 0.0)
        self.assertEqual(result["nodes_created"], 4)
        self.assertEqual(result["lp_solves"], 9)

    def test_timeout_becomes_time_limit(self):
        with patch.object(benchmark.subprocess, "run", side_effect=subprocess.TimeoutExpired("solver", 0.01)):
            result = benchmark._invoke(Path("solver"), Path("instance.mps"), [], "cpu", 0, 0.01)
        self.assertEqual(result["status"], "TIME_LIMIT")

    def test_no_presolve_flag_reaches_cli(self):
        completed = subprocess.CompletedProcess([], 0, "Status: OPTIMAL\nPrimal: 0\n", "")
        with patch.object(benchmark.subprocess, "run", return_value=completed) as run:
            benchmark._invoke(Path("solver"), Path("instance.json"), ["--method", "ipm"], "cpu", 0, 2, no_presolve=True)
        self.assertIn("--no-presolve", run.call_args.args[0])

    def test_missing_highs_is_reported_not_fatal(self):
        model = parse_problem_file(str(ROOT / "examples" / "lp.json"))
        with patch.dict("sys.modules", {"highspy": None}):
            result = benchmark._reference_highs(model)
        self.assertEqual(result["reference_solver_status"], "NOT_AVAILABLE")

    def test_missing_cuda_is_cpu_fallback(self):
        with tempfile.TemporaryDirectory() as tmp:
            solver = Path(tmp) / "solver.exe"
            solver.touch()
            with patch.object(benchmark.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "CUDA Available: NO", "")):
                gpu = benchmark._gpu_info(solver, 0)
        self.assertFalse(gpu["available"])

    def test_cpu_gpu_speedup_aggregate_uses_measured_pair_only(self):
        values = benchmark._aggregate([], [], [{"cpu_time_ms": 12.0, "gpu_time_ms": 6.0,
            "gpu_used": True, "speedup": 2.0}])
        self.assertEqual(values["cpu_runs"], 1)
        self.assertEqual(values["gpu_runs"], 1)
        self.assertEqual(values["average_gpu_speedup"], 2.0)

    def test_objective_comparison_obeys_configured_tolerance(self):
        difference, matches = benchmark._objective_comparison(100.0000001, 100.0, 1e-6)
        self.assertAlmostEqual(difference, 1e-7)
        self.assertTrue(matches)

    def test_unavailable_reference_integer_metrics_are_not_serialized_as_sentinels(self):
        self.assertIsNone(benchmark._nonnegative_int(-1))
        self.assertEqual(benchmark._nonnegative_int(6), 6)

    def test_failed_solver_status_is_not_optimal(self):
        result = benchmark._parse_solver_output("Status: NUMERICAL_FAILURE\n", 1)
        self.assertEqual(result["status"], "NUMERICAL_FAILURE")

    def test_run_writes_csv_json_and_presolve_backend_rows(self):
        with tempfile.TemporaryDirectory() as tmp:
            temp = Path(tmp)
            inputs = temp / "inputs" / "small"
            inputs.mkdir(parents=True)
            (inputs / "case.json").write_text((ROOT / "examples" / "lp.json").read_text(encoding="utf-8"), encoding="utf-8")
            solver = temp / "solver.exe"
            solver.touch()
            args = argparse.Namespace(input=str(temp / "inputs"), solver=str(solver), output_dir=str(temp / "results"),
                dataset="netlib", device=0, tier="quick", limit=None, method="auto", backend="cpu", no_presolve=False,
                time_limit=5.0, feasibility_tol=1e-7, objective_tol=1e-6, compare_highs=True,
                compare_backends=True, compare_presolve=True)
            def fake_run(command, **kwargs):
                if "--device-info" in command:
                    return subprocess.CompletedProcess(command, 0, "CUDA Available: NO", "")
                return subprocess.CompletedProcess(command, 0, FAKE_OUTPUT, "")
            with patch.object(benchmark.subprocess, "run", side_effect=fake_run), \
                 patch.object(benchmark, "_reference_highs", return_value={"reference_solver_status": "NOT_AVAILABLE"}):
                report = benchmark.run_benchmark(args)
            result_dir = Path(report["output_dir"])
            self.assertTrue((result_dir / "lp_benchmark.csv").exists())
            self.assertTrue((result_dir / "netlib_benchmark.json").exists())
            self.assertTrue((result_dir / "presolve_benchmark.csv").exists())
            self.assertTrue((result_dir / "cpu_gpu_benchmark.csv").exists())
            self.assertEqual(report["rows"][0]["verification_pass"], True)
            self.assertTrue(report["rows"][0]["presolve_fallback"])
            self.assertFalse(report["rows"][0]["presolve_applied_to_solve"])

    def test_reported_verification_failure_keeps_a_failure_reason(self):
        result = benchmark._parse_solver_output("Status: ITERATION_LIMIT\nVerification: FAIL\nPrimal: 0\n", 0)
        self.assertFalse(result["reported_verification"])
        self.assertEqual(result["status"], "FEASIBLE")
        reason = benchmark._verification_failure_reason(result, {"pass": True}, True, True)
        self.assertEqual(reason, "solver reported Verification: FAIL")

    def test_empty_comparison_outputs_explain_not_requested(self):
        with tempfile.TemporaryDirectory() as tmp:
            temp = Path(tmp)
            inputs = temp / "inputs"
            inputs.mkdir()
            (inputs / "case.json").write_text((ROOT / "examples" / "lp.json").read_text(encoding="utf-8"), encoding="utf-8")
            solver = temp / "solver.exe"
            solver.touch()
            args = argparse.Namespace(input=str(inputs), solver=str(solver), output_dir=str(temp / "results"),
                dataset="custom", device=0, tier="quick", limit=1, method="auto", backend="cpu", no_presolve=False,
                time_limit=5.0, feasibility_tol=1e-7, objective_tol=1e-6, compare_highs=False,
                compare_backends=False, compare_presolve=False)
            def fake_run(command, **kwargs):
                if "--device-info" in command:
                    return subprocess.CompletedProcess(command, 0, "CUDA Available: NO", "")
                return subprocess.CompletedProcess(command, 0, FAKE_OUTPUT, "")
            with patch.object(benchmark.subprocess, "run", side_effect=fake_run):
                report = benchmark.run_benchmark(args)
            result_dir = Path(report["output_dir"])
            external = json.loads((result_dir / "external_solver_comparison.json").read_text(encoding="utf-8"))
            gpu = json.loads((result_dir / "cpu_gpu_benchmark.json").read_text(encoding="utf-8"))
            self.assertEqual(external[0]["reference_solver_status"], "NOT_REQUESTED")
            self.assertEqual(gpu[0]["status"], "NOT_REQUESTED")
            self.assertIn("NOT_REQUESTED", (result_dir / "external_solver_comparison.csv").read_text(encoding="utf-8"))
            self.assertIn("NOT_REQUESTED", (result_dir / "cpu_gpu_benchmark.csv").read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
