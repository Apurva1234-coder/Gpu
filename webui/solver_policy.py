"""Deterministic automatic resource policy for the Sovereign dashboard.

The values here are intentionally conservative starting points for a local demo.
They are configuration, rather than a prediction model: update them only after
benchmarking a representative model set.
"""
from __future__ import annotations

from dataclasses import asdict, dataclass
from typing import Any


@dataclass(frozen=True)
class ExecutionLimits:
    name: str
    max_iterations: int
    time_limit_seconds: int
    max_nodes: int = 0


@dataclass(frozen=True)
class SolverSelection:
    method: str
    backend: str
    presolve: bool
    max_iterations: int
    time_limit_seconds: int
    model_size: str
    complexity_score: float
    reason: str
    backend_reason: str
    fallback_methods: tuple[str, ...] = ()
    max_nodes: int = 0

    def payload(self) -> dict[str, Any]:
        data = asdict(self)
        data["fallback_methods"] = list(self.fallback_methods)
        return data


class SolverPolicy:
    """Choose implemented solver paths and bounded resources from model complexity.

    LP routing uses Revised Simplex by default and sparse PDHG for a narrowly
    defined very-large sparse structure. Backend selection remains conservative
    until same-model, verified CPU/CUDA comparisons demonstrate a benefit.
    Revised/Dual simplex and MILP branch-and-bound remain CPU paths.
    """

    # Normal/demo runs have size-tiered wall-clock defaults. LP iteration
    # budgets remain unlimited unless a wall-clock limit stops a run.
    SIZE_LIMITS = (
        ("SMALL", 10_000, ExecutionLimits("SMALL", 10_000, 60, 10_000)),
        ("MEDIUM", 100_000, ExecutionLimits("MEDIUM", 25_000, 120, 100_000)),
        ("LARGE", float("inf"), ExecutionLimits("LARGE", 50_000, 300, 500_000)),
    )
    PROBLEM_WEIGHTS = {"LP": 1.0, "QP": 1.25, "MILP": 2.5}
    DENSE_FALLBACK_MEMORY_BUDGET_BYTES = 256 * 1024 * 1024
    ADAPTIVE_PRESOLVE_WORK_THRESHOLD = 2_000

    @classmethod
    def complexity(cls, analysis: dict[str, Any]) -> float:
        """Use dimensions, sparsity structure, and discrete content together."""
        variables = max(0, int(analysis.get("variables", 0)))
        constraints = max(0, int(analysis.get("constraints", 0)))
        nonzeros = max(0, int(analysis.get("nonzeros", 0)))
        integer_types = analysis.get("types", {}) or {}
        discrete = int(integer_types.get("integer", 0)) + int(integer_types.get("binary", 0))
        problem_type = str(analysis.get("problem_type", "LP")).upper()
        # Nonzeros are kept at full weight. A sparse model can have modest row
        # counts while still requiring substantial basis work (for example,
        # pilot.mps), so rows alone must not leave it in the small tier.
        base = variables + constraints + nonzeros
        if problem_type == "MILP":
            base += discrete * 20
        return round(base * cls.PROBLEM_WEIGHTS.get(problem_type, 1.0), 2)

    @classmethod
    def limits_for(cls, analysis: dict[str, Any]) -> tuple[ExecutionLimits, float]:
        score = cls.complexity(analysis)
        for _, threshold, limits in cls.SIZE_LIMITS:
            if score <= threshold:
                return limits, score
        raise RuntimeError("Automatic size policy has no terminal range.")

    @classmethod
    def select(cls, analysis: dict[str, Any], device: dict[str, Any]) -> SolverSelection:
        problem_type = str(analysis["problem_type"]).upper()
        limits, score = cls.limits_for(analysis)
        variables = int(analysis.get("variables", 0))
        constraints = int(analysis.get("constraints", 0))
        nonzeros = int(analysis.get("nonzeros", 0))
        sparsity = float(analysis.get("sparsity", 0.0))
        sparse = float(analysis.get("sparsity", 0)) >= 90
        # The local, original-model-verified Netlib sweep found that row-only
        # presolve made Bandm, Boeing1, and Scagr25 32-38% slower in this
        # medium sparse range. Keep presolve enabled for small and large LPs:
        # Afiro benefited slightly, while Pilot's presolved run avoided a
        # worse unbounded status but still ended in numerical failure.
        skip_lp_presolve = (
            problem_type == "LP"
            and 300 <= variables <= 700
            and 250 <= constraints <= 500
            and sparsity >= 97.0
        )
        structure = "large sparse constraint matrix" if limits.name == "LARGE" and sparse else "model dimensions and nonzero structure"
        size = f"{variables} variables, {constraints} constraints, and {nonzeros} nonzeros"
        backend = "cpu"

        if problem_type == "QP":
            method = "qp"
            # QP Newton steps factor a dense reduced Schur system.  Keep an
            # automatic run bounded enough to return a diagnostic promptly;
            # Expert mode exposes 0 for an intentionally unlimited run.
            qp_iterations = min(limits.max_iterations, 200)
            reason = f"Convex QP is routed to the implemented Newton/Barrier solver. Its {structure} selected a {qp_iterations}-iteration diagnostic budget ({size})."
            backend_reason = "CPU selected conservatively; no verified same-model evidence currently shows a CUDA speedup for this QP path."
            fallbacks: tuple[str, ...] = ()
        elif problem_type == "MILP":
            method = "milp"
            reason = f"Discrete variables use the implemented branch-and-bound path. MILP search complexity and {structure} selected a {limits.max_nodes:,}-node budget ({size})."
            backend_reason = "MILP branch-and-bound control and its verified automatic path run on CPU."
            fallbacks = ()
        else:
            # PDHG stores and operates on the sparse constraint matrix. Route
            # only genuinely large, extremely sparse LPs to it; the checked-in
            # Netlib set is too small to establish a general speed crossover.
            # Smaller or less sparse models retain Revised Simplex, whose own
            # implementation selects dense or sparse linear algebra by model
            # structure and has stronger results on the measured small case.
            large_sparse_pdhg = (
                variables >= 100_000
                and constraints >= 10_000
                and nonzeros >= 1_000_000
                and sparsity >= 99.5
            )
            if large_sparse_pdhg:
                method = "pdhg"
                reason = (
                    f"PDHG selected for a very large sparse LP ({size}, {sparsity:.3f}% sparse): "
                    "its matrix-vector path is sparse and avoids simplex basis growth. "
                    "This is a memory/scale routing rule, not a measured speedup guarantee; "
                    "the result remains subject to convergence and original-model verification."
                )
                backend_reason = "CPU selected; no verified same-model CPU/CUDA crossover is available for this PDHG workload."
                # A second full solve is not justified at this scale without
                # measured fallback benefit and a separately budgeted policy.
                fallbacks = ()
            else:
                method = "revised-simplex"
                reason = f"Revised Simplex selected for this LP structure ({size}, {sparsity:.3f}% sparse); its implementation chooses dense or sparse linear algebra from estimated work and memory."
                backend_reason = "CPU selected conservatively; no verified same-model evidence currently shows a CUDA speedup for a production LP method."
                fallbacks = ("dual-simplex",)

        if skip_lp_presolve:
            reason += " The verified local Netlib presolve sweep favored solving an LP in this sparse dimension range without presolve; the advanced setting can override this choice."

        if not device.get("cuda_available"):
            cuda_note = " CUDA is unavailable on this machine."
        else:
            cuda_note = " CUDA is available, but automatic routing remains CPU until verified speedup evidence is recorded."
        return SolverSelection(
            method=method,
            backend=backend if problem_type in {"QP", "LP"} else "cpu",
            presolve=not skip_lp_presolve,
            max_iterations=0 if problem_type == "LP" else qp_iterations if problem_type == "QP" else limits.max_iterations,
            time_limit_seconds=limits.time_limit_seconds,
            model_size=limits.name,
            complexity_score=score,
            reason=reason + cuda_note,
            backend_reason=backend_reason,
            fallback_methods=fallbacks,
            max_nodes=limits.max_nodes if problem_type == "MILP" else 0,
        )

    @staticmethod
    def can_fallback(status: str, verification: str) -> bool:
        return status.upper() in {"NUMERICAL_FAILURE", "FAILED"} or verification.upper() == "FAIL"

    @classmethod
    def dense_fallback_memory_bytes(cls, analysis: dict[str, Any]) -> int:
        """Conservative estimate for the dense dual-simplex fallback workspace."""
        rows = max(0, int(analysis.get("constraints", 0)))
        columns = max(0, int(analysis.get("variables", 0)))
        # Standardization can add bound rows and split free variables. Bound
        # both dimensions by twice the total input dimensions, then budget for
        # the standardized matrix, the dense tableau, basis, and transpose.
        dimension = 2 * (rows + columns)
        return 8 * (5 * dimension * dimension + 8 * dimension)

    @classmethod
    def dense_fallback_is_safe(cls, analysis: dict[str, Any]) -> bool:
        return cls.dense_fallback_memory_bytes(analysis) <= cls.DENSE_FALLBACK_MEMORY_BUDGET_BYTES
