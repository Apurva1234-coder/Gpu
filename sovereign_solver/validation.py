from typing import Any, Dict
from .model import OptimizationModel

VALID_TYPES = {"continuous", "integer", "binary"}
VALID_SENSES = {"maximize", "minimize"}
VALID_OPERATORS = {"<=", "=", ">="}


def validate_payload(data: Dict[str, Any]) -> OptimizationModel:
    required = ["name", "objective_sense", "variables", "objective", "constraints"]
    missing = [key for key in required if key not in data]
    if missing:
        raise ValueError("Missing required field(s): " + ", ".join(missing))
    if not isinstance(data["name"], str) or not data["name"].strip():
        raise ValueError("name must be a non-empty string.")
    sense = str(data["objective_sense"]).lower()
    if sense not in VALID_SENSES:
        raise ValueError("objective_sense must be 'maximize' or 'minimize'.")
    variables = data["variables"]
    if not isinstance(variables, list) or not variables:
        raise ValueError("variables must be a non-empty list.")
    names = []
    for item in variables:
        if not isinstance(item, dict) or not isinstance(item.get("name"), str) or not item["name"].strip():
            raise ValueError("Each variable needs a non-empty name.")
        if item["name"] in names:
            raise ValueError(f"Duplicate variable name: {item['name']}")
        if item.get("type", "continuous").lower() not in VALID_TYPES:
            raise ValueError(f"Invalid type for variable {item['name']}; use continuous, integer, or binary.")
        names.append(item["name"])
    _validate_coefficients(data["objective"], names, "objective")
    quadratic = data.get("quadratic_terms", {})
    _validate_coefficients(quadratic, names, "quadratic_terms")
    constraints = data["constraints"]
    if not isinstance(constraints, list):
        raise ValueError("constraints must be a list.")
    for index, item in enumerate(constraints, 1):
        if not isinstance(item, dict):
            raise ValueError(f"Constraint {index} must be an object.")
        _validate_coefficients(item.get("coefficients"), names, f"constraint {index} coefficients")
        if item.get("operator") not in VALID_OPERATORS:
            raise ValueError(f"Constraint {index} operator must be <=, =, or >=.")
        if not isinstance(item.get("rhs"), (int, float)):
            raise ValueError(f"Constraint {index} rhs must be numeric.")
    bounds = data.get("bounds", {})
    if not isinstance(bounds, dict):
        raise ValueError("bounds must be an object mapping variable names to [lower, upper].")
    normalized_bounds = {}
    for name, bound in bounds.items():
        if name not in names or not isinstance(bound, (list, tuple)) or len(bound) != 2:
            raise ValueError(f"Bounds for {name} must be [lower, upper] and reference a known variable.")
        if any(value is not None and not isinstance(value, (int, float)) for value in bound):
            raise ValueError(f"Bounds for {name} must contain numeric values or null.")
        if bound[0] is not None and bound[1] is not None and bound[0] > bound[1]:
            raise ValueError(f"Lower bound cannot exceed upper bound for {name}.")
        normalized_bounds[name] = (bound[0], bound[1])
    from .model import Variable, Constraint
    return OptimizationModel(data["name"].strip(), sense, [Variable(v["name"], v.get("type", "continuous").lower()) for v in variables], {k: float(v) for k, v in data["objective"].items()}, [Constraint(c.get("name", f"c{i}"), {k: float(v) for k, v in c["coefficients"].items()}, c["operator"], float(c["rhs"])) for i, c in enumerate(constraints, 1)], {k: float(v) for k, v in quadratic.items()}, normalized_bounds)


def _validate_coefficients(coefficients, names, label):
    if not isinstance(coefficients, dict):
        raise ValueError(f"{label} must be an object mapping variable names to numeric coefficients.")
    unknown = set(coefficients) - set(names)
    if unknown:
        raise ValueError(f"{label} references unknown variable(s): {', '.join(sorted(unknown))}")
    if any(not isinstance(v, (int, float)) for v in coefficients.values()):
        raise ValueError(f"{label} coefficients must be numeric.")
