import unittest

from webui.solver_policy import SolverPolicy


class SolverPolicyTests(unittest.TestCase):
    def selection(self, variables, constraints, nonzeros, sparsity, problem_type="LP"):
        analysis = {
            "problem_type": problem_type,
            "variables": variables,
            "constraints": constraints,
            "nonzeros": nonzeros,
            "sparsity": sparsity,
            "types": {"continuous": variables, "integer": 0, "binary": 0},
        }
        return SolverPolicy.select(analysis, {"cuda_available": False})

    def test_medium_sparse_lp_uses_benchmark_supported_no_presolve_route(self):
        for data in ((472, 305, 2494, 98.27), (384, 440, 3819, 97.74), (500, 471, 1554, 99.34)):
            with self.subTest(model=data):
                selection = self.selection(*data)
                self.assertFalse(selection.presolve)
                self.assertIn("verified local Netlib presolve sweep", selection.reason)

    def test_presolve_remains_enabled_outside_measured_medium_sparse_range(self):
        for data in ((32, 27, 83, 90.39), (163, 488, 2410, 96.97), (3652, 1441, 43167, 99.18), (400, 400, 8000, 95.0)):
            with self.subTest(model=data):
                self.assertTrue(self.selection(*data).presolve)

    def test_milp_does_not_use_lp_presolve_heuristic(self):
        self.assertTrue(self.selection(472, 305, 2494, 98.27, problem_type="MILP").presolve)

    def test_dense_fallback_estimate_is_bounded_for_large_models(self):
        small = {"variables": 472, "constraints": 305}
        large = {"variables": 100_000, "constraints": 80_000}
        self.assertTrue(SolverPolicy.dense_fallback_is_safe(small))
        self.assertFalse(SolverPolicy.dense_fallback_is_safe(large))
        self.assertGreater(SolverPolicy.dense_fallback_memory_bytes(large), SolverPolicy.DENSE_FALLBACK_MEMORY_BUDGET_BYTES)


if __name__ == "__main__":
    unittest.main()
