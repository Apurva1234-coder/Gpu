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

    LP routing branches on matrix sparsity: sparse LPs use PDHG and CUDA when
    available; less sparse LPs use Revised Simplex on CPU.
    Revised/Dual simplex and MILP branch-and-bound remain CPU paths.
    """

    # Automatic solve time remains unlimited. LP iteration budgets are also
    # disabled below so LP methods run to solver termination. QP and MILP keep
    # their separate conservative diagnostic/search budgets.
    SIZE_LIMITS = (
        ("SMALL", 10_000, ExecutionLimits("SMALL", 10_000, 0, 10_000)),
        ("MEDIUM", 100_000, ExecutionLimits("MEDIUM", 25_000, 0, 100_000)),
        ("LARGE", float("inf"), ExecutionLimits("LARGE", 50_000, 0, 500_000)),
    )
    PROBLEM_WEIGHTS = {"LP": 1.0, "QP": 1.25, "MILP": 2.5}

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
        sparse = float(analysis.get("sparsity", 0)) >= 90
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
            # The CUDA build uses cuSOLVER for the QP KKT linear system. Keep
            # small QPs on CPU because transfer/setup overhead dominates there.
            cuda_eligible = bool(device.get("cuda_available")) and limits.name in {"MEDIUM", "LARGE"}
            backend = "cuda" if cuda_eligible else "cpu"
            backend_reason = (
                "CUDA was selected: the installed CUDA QP path executes the reduced Schur Newton linear solve through cuSOLVER."
                if cuda_eligible else
                "CPU was selected because this QP is small or CUDA is unavailable; CUDA setup and transfer overhead would not improve this run."
            )
            fallbacks: tuple[str, ...] = ()
        elif problem_type == "MILP":
            method = "milp"
            reason = f"Discrete variables use the implemented branch-and-bound path. MILP search complexity and {structure} selected a {limits.max_nodes:,}-node budget ({size})."
            backend_reason = "MILP branch-and-bound control and its verified automatic path run on CPU."
            fallbacks = ()
        else:
            # Choose the LP execution branch from sparsity alone, independent
            # of model-size tier. PDHG is the implemented sparse LP path.
            pdhg_eligible = sparse
            method = "pdhg" if pdhg_eligible else "revised-simplex"
            reason = (f"LP matrix sparsity is {analysis.get('sparsity', 0)}%, at or above the 90% sparse threshold, so the model is routed to PDHG ({size})." if pdhg_eligible else
                      f"LP matrix sparsity is {analysis.get('sparsity', 0)}%, below the 90% threshold, so the model is routed to CPU Revised Simplex ({size}).")
            reason += " Automatic LP solving has no iteration or time limit (0 = unlimited)."
            cuda_eligible = pdhg_eligible and bool(device.get("cuda_available"))
            backend = "cuda" if cuda_eligible else "cpu"
            backend_reason = ("CUDA PDHG keeps the sparse matrix resident on the device during iterations." if cuda_eligible else
                              "PDHG will run on CPU because CUDA is unavailable." if pdhg_eligible else
                              "Revised Simplex is the selected CPU path because the LP matrix is below the sparse threshold.")
            fallbacks = ("revised-simplex",) if pdhg_eligible else ("dual-simplex",)

        if not device.get("cuda_available"):
            cuda_note = " CUDA is unavailable on this machine."
        elif backend == "cuda":
            cuda_note = f" CUDA was detected and selected for the {method.upper()} solve."
        else:
            cuda_note = " CUDA was detected, but this selected solver path is CPU-only."
        return SolverSelection(
            method=method,
            backend=backend if problem_type in {"QP", "LP"} else "cpu",
            presolve=True,
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
