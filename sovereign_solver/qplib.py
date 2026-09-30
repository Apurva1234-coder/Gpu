"""Reader for the standard, line-oriented QPLIB interchange format."""
from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Any, Dict


@dataclass
class _QPLIBLines:
    values: list[str]
    position: int = 0

    def take(self, label: str) -> str:
        if self.position >= len(self.values):
            raise ValueError(f"Invalid QPLIB format: missing {label}.")
        value = self.values[self.position]
        self.position += 1
        return value

    def integer(self, label: str) -> int:
        value = self.take(label).split()[0]
        try:
            return int(value)
        except ValueError as exc:
            raise ValueError(f"Invalid QPLIB {label}: expected an integer, got '{value}'.") from exc

    def number(self, label: str) -> float:
        value = self.take(label).split()[0]
        try:
            return float(value)
        except ValueError as exc:
            raise ValueError(f"Invalid QPLIB {label}: expected a number, got '{value}'.") from exc

    def fields(self, count: int, label: str) -> list[str]:
        fields = self.take(label).split()
        if len(fields) != count:
            raise ValueError(f"Invalid QPLIB {label}: expected {count} values, got {len(fields)}.")
        return fields


def _clean_lines(text: str) -> list[str]:
    return [line.split("#", 1)[0].strip() for line in text.splitlines() if line.split("#", 1)[0].strip()]


def _finite_bound(value: float, infinity: float, direction: int) -> float | None:
    """Map QPLIB's finite infinity sentinel to JSON-compatible null."""
    if not math.isfinite(value):
        return None
    threshold = abs(infinity) * 0.999 if math.isfinite(infinity) else math.inf
    if direction < 0 and value <= -threshold:
        return None
    if direction > 0 and value >= threshold:
        return None
    return value


def parse_qplib(text: str) -> Dict[str, Any]:
    """Parse a continuous QPLIB model with linear constraints.

    The solver stores a diagonal Hessian, so off-diagonal objective terms and
    quadratic constraints receive a clear format error instead of being lost.
    """
    reader = _QPLIBLines(_clean_lines(text))
    if not reader.values:
        raise ValueError("QPLIB file is empty.")

    name = reader.take("problem name")
    if not name.upper().startswith("QPLIB"):
        raise ValueError("Invalid QPLIB format: the first line must identify a QPLIB model.")
    problem_type = reader.take("problem type").upper()
    sense_token = reader.take("objective sense").lower()
    if sense_token not in {"minimize", "maximize", "min", "max"}:
        raise ValueError(f"Invalid QPLIB objective sense: '{sense_token}'.")
    sense = "minimize" if sense_token.startswith("min") else "maximize"
    n_vars = reader.integer("number of variables")
    n_constraints = reader.integer("number of constraints")
    if n_vars <= 0 or n_constraints < 0:
        raise ValueError("Invalid QPLIB dimensions.")
    if not problem_type.endswith("L"):
        raise ValueError(
            f"QPLIB type '{problem_type}' has quadratic constraints. This solver currently supports QPLIB models with linear constraints only."
        )

    variables = [{"name": f"x{i}", "type": "continuous"} for i in range(1, n_vars + 1)]
    quadratic_terms: dict[str, float] = {}
    objective: dict[str, float] = {}

    quadratic_count = reader.integer("number of quadratic objective terms")
    for term_index in range(quadratic_count):
        row_text, col_text, value_text = reader.fields(3, f"quadratic objective term {term_index + 1}")
        try:
            row, column, value = int(row_text), int(col_text), float(value_text)
        except ValueError as exc:
            raise ValueError(f"Invalid QPLIB quadratic objective term {term_index + 1}.") from exc
        if not (1 <= row <= n_vars and 1 <= column <= n_vars):
            raise ValueError(f"QPLIB quadratic objective term {term_index + 1} references an invalid variable index.")
        if row != column:
            raise ValueError("QPLIB off-diagonal quadratic objectives are not supported by this solver.")
        if value:
            key = f"x{row}"
            quadratic_terms[key] = quadratic_terms.get(key, 0.0) + value

    default_objective = reader.number("default linear objective coefficient")
    if default_objective:
        objective = {f"x{i}": default_objective for i in range(1, n_vars + 1)}
    objective_count = reader.integer("number of non-default linear objective coefficients")
    for term_index in range(objective_count):
        variable_text, value_text = reader.fields(2, f"linear objective coefficient {term_index + 1}")
        try:
            variable, value = int(variable_text), float(value_text)
        except ValueError as exc:
            raise ValueError(f"Invalid QPLIB linear objective coefficient {term_index + 1}.") from exc
        if not 1 <= variable <= n_vars:
            raise ValueError(f"QPLIB linear objective coefficient {term_index + 1} references an invalid variable index.")
        if value:
            objective[f"x{variable}"] = value
        else:
            objective.pop(f"x{variable}", None)
    reader.number("objective constant")

    coefficients: list[dict[str, float]] = [{} for _ in range(n_constraints)]
    linear_term_count = reader.integer("number of linear constraint terms")
    for term_index in range(linear_term_count):
        row_text, variable_text, value_text = reader.fields(3, f"linear constraint term {term_index + 1}")
        try:
            row, variable, value = int(row_text), int(variable_text), float(value_text)
        except ValueError as exc:
            raise ValueError(f"Invalid QPLIB linear constraint term {term_index + 1}.") from exc
        if not (1 <= row <= n_constraints and 1 <= variable <= n_vars):
            raise ValueError(f"QPLIB linear constraint term {term_index + 1} references an invalid row or variable index.")
        if value:
            row_coefficients = coefficients[row - 1]
            key = f"x{variable}"
            row_coefficients[key] = row_coefficients.get(key, 0.0) + value

    infinity = abs(reader.number("infinity value"))
    if not infinity:
        raise ValueError("Invalid QPLIB infinity value.")

    def read_vector(label: str) -> list[float]:
        default = reader.number(f"default {label}")
        values = [default] * n_constraints
        count = reader.integer(f"number of non-default {label}s")
        for index in range(count):
            row_text, value_text = reader.fields(2, f"non-default {label} {index + 1}")
            try:
                row, value = int(row_text), float(value_text)
            except ValueError as exc:
                raise ValueError(f"Invalid QPLIB non-default {label} {index + 1}.") from exc
            if not 1 <= row <= n_constraints:
                raise ValueError(f"QPLIB non-default {label} {index + 1} references an invalid constraint index.")
            values[row - 1] = value
        return values

    lhs = read_vector("left-hand-side value")
    rhs = read_vector("right-hand-side value")

    def read_bounds(label: str) -> list[float]:
        default = reader.number(f"default variable {label} bound value")
        values = [default] * n_vars
        count = reader.integer(f"number of non-default variable {label} bounds")
        for index in range(count):
            variable_text, value_text = reader.fields(2, f"non-default variable {label} bound {index + 1}")
            try:
                variable, value = int(variable_text), float(value_text)
            except ValueError as exc:
                raise ValueError(f"Invalid QPLIB non-default variable {label} bound {index + 1}.") from exc
            if not 1 <= variable <= n_vars:
                raise ValueError(f"QPLIB non-default variable {label} bound {index + 1} references an invalid variable index.")
            values[variable - 1] = value
        return values

    lower_bounds = read_bounds("lower")
    upper_bounds = read_bounds("upper")
    bounds: dict[str, list[float | None]] = {}
    for index, (lower, upper) in enumerate(zip(lower_bounds, upper_bounds), 1):
        normalized_lower = _finite_bound(lower, infinity, -1)
        normalized_upper = _finite_bound(upper, infinity, 1)
        if normalized_lower is not None or normalized_upper is not None:
            bounds[f"x{index}"] = [normalized_lower, normalized_upper]

    constraints: list[dict[str, Any]] = []
    for row, (row_coefficients, lower, upper) in enumerate(zip(coefficients, lhs, rhs), 1):
        normalized_lower = _finite_bound(lower, infinity, -1)
        normalized_upper = _finite_bound(upper, infinity, 1)
        if normalized_lower is None and normalized_upper is None:
            continue
        if normalized_lower is not None and normalized_upper is not None and normalized_lower == normalized_upper:
            constraints.append({"name": f"c{row}", "coefficients": row_coefficients, "operator": "=", "rhs": normalized_lower})
        else:
            if normalized_lower is not None:
                constraints.append({"name": f"c{row}_lower", "coefficients": row_coefficients, "operator": ">=", "rhs": normalized_lower})
            if normalized_upper is not None:
                constraints.append({"name": f"c{row}_upper", "coefficients": row_coefficients, "operator": "<=", "rhs": normalized_upper})

    return {"name": name, "objective_sense": sense, "variables": variables, "objective": objective,
            "quadratic_terms": quadratic_terms, "constraints": constraints, "bounds": bounds}
