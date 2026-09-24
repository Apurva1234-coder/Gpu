from dataclasses import dataclass, field
from typing import Dict, List, Literal, Optional

VariableType = Literal["continuous", "integer", "binary"]
Sense = Literal["maximize", "minimize"]
Operator = Literal["<=", "=", ">="]


@dataclass
class Variable:
    name: str
    type: VariableType = "continuous"


@dataclass
class Constraint:
    name: str
    coefficients: Dict[str, float]
    operator: Operator
    rhs: float


@dataclass
class OptimizationModel:
    name: str
    objective_sense: Sense
    variables: List[Variable]
    objective: Dict[str, float]
    constraints: List[Constraint]
    quadratic_terms: Dict[str, float] = field(default_factory=dict)
    bounds: Dict[str, tuple] = field(default_factory=dict)
    objective_constant: float = 0.0

    @property
    def has_quadratic_objective(self) -> bool:
        return any(value != 0 for value in self.quadratic_terms.values())

    @property
    def has_discrete_variables(self) -> bool:
        return any(v.type in ("integer", "binary") for v in self.variables)

    def render(self) -> str:
        variable_names = [v.name for v in self.variables]
        lines = [f"Problem: {self.name}", "", "Decision variables:"]
        lines.extend(f"  {v.name} ({v.type})" for v in self.variables)
        lines += ["", f"{self.objective_sense.title()}:", f"  {format_expression(self.objective, variable_names, self.quadratic_terms)}", "", "Subject to:"]
        lines.extend(f"  {c.name}: {format_expression(c.coefficients, variable_names)} {c.operator} {format_number(c.rhs)}" for c in self.constraints)
        lines += ["", "Variable restrictions/types:"]
        for v in self.variables:
            restriction = "{0, 1}" if v.type == "binary" else ("integer" if v.type == "integer" else "continuous")
            bound = self.bounds.get(v.name)
            bound_text = f", bounds [{format_number(bound[0]) if bound[0] is not None else '-inf'}, {format_number(bound[1]) if bound[1] is not None else 'inf'}]" if bound else ""
            lines.append(f"  {v.name}: {restriction}{bound_text}")
        return "\n".join(lines)


def format_number(value: float) -> str:
    return str(int(value)) if float(value).is_integer() else f"{value:g}"


def format_expression(coefficients: Dict[str, float], variable_order: List[str], quadratic_terms: Optional[Dict[str, float]] = None) -> str:
    terms = []
    for name in variable_order:
        coefficient = coefficients.get(name, 0)
        if coefficient == 0:
            continue
        sign = "-" if coefficient < 0 else "+"
        magnitude = abs(coefficient)
        coefficient_text = "" if magnitude == 1 else format_number(magnitude)
        term = f"{coefficient_text}{name}"
        terms.append((sign, term))
    for name in variable_order:
        coefficient = (quadratic_terms or {}).get(name, 0)
        if coefficient == 0:
            continue
        sign = "-" if coefficient < 0 else "+"
        coefficient_text = "" if abs(coefficient) == 1 else format_number(abs(coefficient))
        terms.append((sign, f"{coefficient_text}{name}²"))
    if not terms:
        return "0"
    first_sign, first_term = terms[0]
    result = ("-" if first_sign == "-" else "") + first_term
    for sign, term in terms[1:]:
        result += f" {sign} {term}"
    return result
