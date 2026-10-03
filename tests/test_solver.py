import unittest
import tempfile
from pathlib import Path
from sovereign_solver.classification import classify_model
from sovereign_solver.parser import parse_problem_file
from sovereign_solver.validation import validate_payload
from sovereign_solver.presolve import presolve
from webui.app import ROOT, _run_solver, parse_solver_output


def payload(variable_type="continuous", quadratic=None):
    return {"name": "demo", "objective_sense": "maximize", "variables": [{"name": "x", "type": variable_type}], "objective": {"x": 3}, "quadratic_terms": quadratic or {}, "constraints": [{"name": "limit", "coefficients": {"x": 5}, "operator": "<=", "rhs": 10}]}


class SolverPrototypeTests(unittest.TestCase):
    def test_milp_telemetry_output_is_parsed(self):
        output = """Status: TIME_LIMIT
MILP Model Preparation time ms: 12.5
Root LP time ms: 3.25
Root LP iterations: 14
Root fractional integer variables: 2
Feasibility Pump time ms: 7.5
Feasibility Pump LP solves: 3
Feasibility Pump iterations: 2
Branch-and-Bound time ms: 20
Node selection time ms: 1.5
Node model/update time ms: 2.5
Node LP time ms: 15
Branching time ms: 0.25
Pruning time ms: 0.5
Incumbent updates: 1
Maximum depth: 4
Peak open nodes: 7
Cuts generated: 0
Cuts accepted: 0
Cuts rejected: 0
Warm starts attempted: 0
Warm starts successful: 0
Cold starts: 5
Total MILP solver time ms: 31
Nodes Created: 6
Nodes Processed: 5
Nodes Pruned: 1
LP Solves: 5
LP Iterations: 24
Verification: PASS
Objective: 5
Primal Bound: 5
Dual Bound: 4
Absolute Gap: 1
Relative Gap: 0.2
"""
        result = parse_solver_output(output, 40)
        self.assertEqual(result["status"], "TIME_LIMIT_WITH_INCUMBENT")
        self.assertEqual(result["metrics"]["root_lp_iterations"], 14)
        self.assertEqual(result["metrics"]["feasibility_pump_lp_solves"], 3)
        self.assertEqual(result["metrics"]["nodes_processed"], 5)
        self.assertEqual(result["timing"]["milp_stages_ms"]["node_lp"], 15)

    def test_milp_python_backend_uses_release_solver_and_returns_verified_result(self):
        model_path = ROOT / "examples" / "milp_relaxation.json"
        self.assertTrue(model_path.is_file())
        job = {
            "path": model_path,
            "preparation_time_ms": 0.0,
            "analysis": {"problem_type": "MILP", "variables": 2, "constraints": 1, "nonzeros": 2},
        }
        configuration = {
            "method": "milp", "selected_algorithm": "milp", "backend": "cpu",
            "presolve": True, "time_limit_seconds": 10, "execution_time_limit_seconds": 10,
            "max_iterations": 10000, "max_nodes": 10000,
        }
        result = _run_solver("milp-backend-integration", job, configuration)
        self.assertEqual(result["status"], "OPTIMAL")
        self.assertEqual(result["verification"], "PASS")
        self.assertIsNotNone(result["metrics"]["objective"])
        self.assertIsNotNone(result["timings"]["solver_time_ms"])
        self.assertEqual(result["metrics"]["build_mode"], "RELEASE")
        self.assertIn("build-route-cpu", configuration["solver_executable"])

    def test_small_flugpl_milp_returns_verified_result_through_python_backend(self):
        model_path = ROOT / "datasets" / "milp" / "small" / "flugpl.mps"
        self.assertTrue(model_path.is_file())
        job = {
            "path": model_path,
            "preparation_time_ms": 0.0,
            "analysis": {"problem_type": "MILP", "variables": 18, "constraints": 18, "nonzeros": 46},
        }
        configuration = {
            "method": "milp", "selected_algorithm": "milp", "backend": "auto",
            "presolve": True, "time_limit_seconds": 10, "execution_time_limit_seconds": 10,
            "max_iterations": 25000, "max_nodes": 10000,
        }
        result = _run_solver("small-flugpl-regression", job, configuration)
        self.assertEqual(result["status"], "OPTIMAL")
        self.assertEqual(result["verification"], "PASS")
        self.assertAlmostEqual(result["metrics"]["objective"], 1201500.0, places=4)
        self.assertIsNotNone(result["timings"]["solver_time_ms"])
        self.assertEqual(result["metrics"]["build_mode"], "RELEASE")
        self.assertEqual(result["metrics"]["backend"], "cpu")
        self.assertIn("CUDA is available but is not used", result["metrics"]["backend_reason"])
        self.assertEqual(result["metrics"]["warm_starts_attempted"], 0)
        self.assertEqual(result["metrics"]["cold_starts"], result["metrics"]["lp_solves"])

    def test_lp(self):
        self.assertEqual(classify_model(validate_payload(payload())).problem_type, "LP")

    def test_milp(self):
        self.assertEqual(classify_model(validate_payload(payload("integer"))).problem_type, "MILP")

    def test_qp(self):
        self.assertEqual(classify_model(validate_payload(payload(quadratic={"x": 2}))).problem_type, "QP")

    def test_miqp_rejected(self):
        with self.assertRaises(ValueError):
            classify_model(validate_payload(payload("binary", {"x": 2})))

    def test_unknown_variable_rejected(self):
        data = payload()
        data["objective"] = {"unknown": 1}
        with self.assertRaises(ValueError):
            validate_payload(data)

    def test_arbitrary_extension_text_is_parsed_and_classified(self):
        content = """name: text model
objective: maximize 4x + 2y^2
var x: continuous
var y: continuous
constraint: limit: x + y <= 10
"""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "benchmark_file"
            path.write_text(content, encoding="utf-8")
            model = parse_problem_file(str(path))
        self.assertEqual(classify_model(model).problem_type, "QP")

    def test_invalid_file_has_useful_error(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "problem.dat"
            path.write_text("name: incomplete", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "objective_sense"):
                parse_problem_file(str(path))

    def test_standard_qplib_txt_preserves_qp_matrix_and_bounds(self):
        content = """QPLIB_TEST
DCL
minimize
2
1
2
1 1 2
2 2 4
0
1
1 3
0
2
1 1 1
1 2 2
1e20
0
0
0
0
-1e20
1
1 0
1e20
1
2 5
"""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "QPLIB_TEST.qplib.txt"
            path.write_text(content, encoding="utf-8")
            model = parse_problem_file(str(path))
        self.assertEqual(classify_model(model).problem_type, "QP")
        self.assertEqual(len(model.variables), 2)
        self.assertEqual(len(model.constraints), 1)
        self.assertEqual(sum(len(row.coefficients) for row in model.constraints), 2)
        self.assertEqual(model.quadratic_terms, {"x1": 2.0, "x2": 4.0})
        self.assertEqual(model.bounds["x1"], (0.0, None))
        self.assertEqual(model.bounds["x2"], (None, 5.0))

    def test_mps_lp(self):
        model = self._parse_mps("""NAME TESTLP
ROWS
 N COST
 L LIMIT
COLUMNS
 X COST 3 LIMIT 1
 Y COST 2 LIMIT 4
RHS
 RHS1 LIMIT 10
BOUNDS
 LO BND X 0
ENDATA
""")
        self.assertEqual(classify_model(model).problem_type, "LP")
        self.assertEqual(model.constraints[0].operator, "<=")

    def test_mps_default_rhs_set_name_is_not_treated_as_a_section_header(self):
        model = self._parse_mps("""NAME TESTRHS
ROWS
 N COST
 L LIMIT
COLUMNS
 X COST 1 LIMIT 1
RHS
 RHS LIMIT 7
ENDATA
""")
        self.assertEqual(model.constraints[0].rhs, 7.0)

    def test_mps_integer_marker_is_milp(self):
        model = self._parse_mps("""NAME TESTMILP
ROWS
 N OBJ
 G DEMAND
COLUMNS
 MARK0000 'MARKER' 'INTORG'
 X OBJ 5 DEMAND 1
 MARK0001 'MARKER' 'INTEND'
 Y OBJ 1 DEMAND 1
RHS
 RHS1 DEMAND 3
BOUNDS
 BV BND X
ENDATA
""")
        self.assertEqual(classify_model(model).problem_type, "MILP")
        self.assertEqual(model.variables[0].type, "binary")

    def test_invalid_mps_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "broken"
            path.write_text("NAME BROKEN\nROWS\n N OBJ\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Incomplete MPS"):
                parse_problem_file(str(path))

    def test_presolve_fixed_variable_and_redundant_row(self):
        data = payload()
        data["bounds"] = {"x": [2, 2]}
        data["constraints"].append({"name": "duplicate", "coefficients": {"x": 5}, "operator": "<=", "rhs": 10})
        result = presolve(validate_payload(data))
        self.assertEqual(result.stats.variables_fixed, 1)
        self.assertEqual(result.model.variables, [])

    def test_presolve_infeasible_singleton(self):
        data = payload()
        data["constraints"] = [{"name": "bad", "coefficients": {"x": 1}, "operator": ">=", "rhs": 20}]
        data["bounds"] = {"x": [0, 10]}
        self.assertEqual(presolve(validate_payload(data)).status, "INFEASIBLE")

    def test_presolve_proves_simple_unboundedness(self):
        data = payload()
        data["constraints"] = []
        data["bounds"] = {"x": [0, None]}
        self.assertEqual(presolve(validate_payload(data)).status, "UNBOUNDED")

    @staticmethod
    def _parse_mps(text):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "benchmark_file"
            path.write_text(text, encoding="utf-8")
            return parse_problem_file(str(path))


if __name__ == "__main__":
    unittest.main()
