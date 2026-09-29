"""Deterministic, capability-aware selection for the dashboard's automatic mode."""
from __future__ import annotations

from dataclasses import asdict, dataclass
from typing import Any


@dataclass(frozen=True)
class SolverSelection:
    method: str
    backend: str
    presolve: bool
    max_iterations: int
    time_limit_seconds: int
    reason: str
    fallback_methods: tuple[str, ...] = ()

    def payload(self) -> dict[str, Any]:
        data = asdict(self)
        data["fallback_methods"] = list(self.fallback_methods)
        return data


class SolverPolicy:
    """Select only solver paths that exist in this repository.

    The policy deliberately keeps automatic LP solving on revised simplex. Current
    benchmark evidence supports it more broadly than the experimental IPM path;
    CUDA is therefore not selected merely because a device happens to exist.
    """

    default_max_iterations = 10_000
    default_time_limit_seconds = 60

    @classmethod
    def select(cls, analysis: dict[str, Any], device: dict[str, Any]) -> SolverSelection:
        problem_type = analysis["problem_type"]
        variables, constraints, nonzeros = analysis["variables"], analysis["constraints"], analysis["nonzeros"]
        size = f"{variables} variables, {constraints} constraints, and {nonzeros} nonzeros"
        if problem_type == "QP":
            return SolverSelection(
                method="qp", backend="cpu", presolve=True, max_iterations=cls.default_max_iterations,
                time_limit_seconds=cls.default_time_limit_seconds,
                reason=f"Convex QP is routed to the implemented Newton/Barrier solver ({size}). CPU is selected because this path has no validated CUDA policy.",
            )
        if problem_type == "MILP":
            return SolverSelection(
                method="milp", backend="cpu", presolve=True, max_iterations=cls.default_max_iterations,
                time_limit_seconds=cls.default_time_limit_seconds,
                reason=f"Discrete variables require the implemented branch-and-bound path ({size}). Search control and verification run on CPU.",
            )
        cuda_note = " CUDA is unavailable on this machine." if not device.get("cuda_available") else " CUDA is available but is not selected for the validated simplex path."
        return SolverSelection(
            method="revised-simplex", backend="cpu", presolve=True, max_iterations=cls.default_max_iterations,
            time_limit_seconds=cls.default_time_limit_seconds,
            reason=f"Continuous LP is routed to Revised Simplex, the broadest validated automatic LP path for {size}.{cuda_note}",
            fallback_methods=("dual-simplex",),
        )

    @staticmethod
    def can_fallback(status: str, verification: str) -> bool:
        return status.upper() in {"NUMERICAL_FAILURE", "FAILED"} or verification.upper() == "FAIL"
