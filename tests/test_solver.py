import unittest
import tempfile
from pathlib import Path
from sovereign_solver.classification import classify_model
from sovereign_solver.parser import parse_problem_file
from sovereign_solver.validation import validate_payload
from sovereign_solver.presolve import presolve


def payload(variable_type="continuous", quadratic=None):
    return {"name": "demo", "objective_sense": "maximize", "variables": [{"name": "x", "type": variable_type}], "objective": {"x": 3}, "quadratic_terms": quadratic or {}, "constraints": [{"name": "limit", "coefficients": {"x": 5}, "operator": "<=", "rhs": 10}]}


class SolverPrototypeTests(unittest.TestCase):
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
