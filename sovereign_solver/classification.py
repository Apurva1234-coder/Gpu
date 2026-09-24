from dataclasses import dataclass
from .model import OptimizationModel


@dataclass(frozen=True)
class Classification:
    problem_type: str
    reason: str


def classify_model(model: OptimizationModel) -> Classification:
    if model.has_quadratic_objective:
        if model.has_discrete_variables:
            raise ValueError("MIQP is not supported in V1: quadratic objectives require continuous variables.")
        return Classification("QP", "The objective contains quadratic terms, the constraints are linear, and all variables are continuous.")
    if model.has_discrete_variables:
        return Classification("MILP", "The objective and constraints are linear, but the model contains integer/binary decision variables.")
    return Classification("LP", "The objective and constraints are linear, and all decision variables are continuous.")
