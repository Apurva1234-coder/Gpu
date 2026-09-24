from dataclasses import dataclass, field
from math import inf, isfinite
from typing import Dict, List

from ..model import Constraint, OptimizationModel, Variable

EPS = 1e-9


@dataclass
class PresolveStats:
    bound_tightenings: int = 0
    variables_fixed: int = 0
    variables_eliminated: int = 0
    redundant_rows_removed: int = 0
    aggregations: int = 0
    substitutions: int = 0
    singleton_reductions: int = 0


@dataclass
class PresolveResult:
    model: OptimizationModel
    status: str
    passes: int
    stats: PresolveStats = field(default_factory=PresolveStats)
    messages: List[str] = field(default_factory=list)


class Presolver:
    def __init__(self, tolerance=EPS, max_passes=10):
        self.tol = tolerance
        self.max_passes = max_passes

    def run(self, source: OptimizationModel) -> PresolveResult:
        model = _copy_model(source)
        stats, messages = PresolveStats(), []
        status = "UNCHANGED"
        passes = 0
        for pass_number in range(1, self.max_passes + 1):
            passes = pass_number
            changed = False
            result = _propagate_bounds(model, self.tol)
            model, count, infeasible = result
            stats.bound_tightenings += count
            changed |= count > 0
            if infeasible:
                return PresolveResult(model, "INFEASIBLE", passes, stats, ["Contradictory bounds or an impossible constraint was detected."])
            model, count, fixed = _eliminate_fixed(model, self.tol)
            stats.variables_fixed += fixed
            stats.variables_eliminated += fixed
            changed |= fixed > 0
            model, count = _remove_redundant_rows(model, self.tol)
            stats.redundant_rows_removed += count
            changed |= count > 0
            model, count, infeasible = _process_singletons(model, self.tol)
            stats.singleton_reductions += count
            changed |= count > 0
            if infeasible:
                return PresolveResult(model, "INFEASIBLE", passes, stats, ["A singleton constraint is impossible under current bounds."])
            model, count = _substitute_singleton_equalities(model, self.tol)
            stats.substitutions += count
            stats.variables_eliminated += count
            changed |= count > 0
            if not changed:
                break
        unbounded = _proven_unbounded(model, self.tol)
        if unbounded:
            status = "UNBOUNDED"
            messages.append("Unboundedness was proven from an objective direction with no finite bound or constraint coefficient.")
        elif any(value > 0 for value in stats.__dict__.values()):
            status = "REDUCED"
        return PresolveResult(model, status, passes, stats, messages)


def presolve(model, tolerance=EPS, max_passes=10):
    return Presolver(tolerance, max_passes).run(model)


def _copy_model(m):
    return OptimizationModel(m.name, m.objective_sense, [Variable(v.name, v.type) for v in m.variables], dict(m.objective), [Constraint(c.name, dict(c.coefficients), c.operator, c.rhs) for c in m.constraints], dict(m.quadratic_terms), dict(m.bounds), m.objective_constant)


def _bounds(m):
    out = {}
    for v in m.variables:
        lo, hi = m.bounds.get(v.name, (0.0, 1.0 if v.type == "binary" else inf))
        lo = -inf if lo is None else lo
        hi = inf if hi is None else hi
        out[v.name] = [lo, hi]
    return out


def _propagate_bounds(m, tol):
    bounds, changes = _bounds(m), 0
    for c in m.constraints:
        if len(c.coefficients) != 1:
            continue
        name, coefficient = next(iter(c.coefficients.items()))
        if abs(coefficient) <= tol:
            continue
        value = c.rhs / coefficient
        lo, hi = bounds[name]
        new_lo, new_hi = lo, hi
        if c.operator == "=": new_lo = max(lo, value); new_hi = min(hi, value)
        elif (c.operator == "<=" and coefficient > 0) or (c.operator == ">=" and coefficient < 0): new_hi = min(hi, value)
        else: new_lo = max(lo, value)
        if new_lo > lo + tol: bounds[name][0], changes = new_lo, changes + 1
        if new_hi < hi - tol: bounds[name][1], changes = new_hi, changes + 1
    for name, (lo, hi) in bounds.items():
        if lo > hi + tol: return m, changes, True
        m.bounds[name] = (lo, hi)
        if abs(lo - hi) <= tol and m.variables and name not in m.bounds: changes += 1
    for c in m.constraints:
        if not c.coefficients and not _satisfies(0, c.operator, c.rhs, tol): return m, changes, True
    return m, changes, False


def _eliminate_fixed(m, tol):
    bounds = _bounds(m); fixed = {n: (lo + hi) / 2 for n, (lo, hi) in bounds.items() if isfinite(lo) and isfinite(hi) and abs(lo - hi) <= tol}
    if not fixed: return m, 0, 0
    for name, value in fixed.items():
        m.objective_constant = getattr(m, "objective_constant", 0.0) + m.objective.pop(name, 0.0) * value + m.quadratic_terms.pop(name, 0.0) * value * value
    for c in m.constraints:
        c.rhs -= sum(c.coefficients.pop(name, 0.0) * value for name, value in fixed.items())
    m.variables = [v for v in m.variables if v.name not in fixed]
    for name in fixed: m.bounds.pop(name, None)
    return m, len(fixed), len(fixed)


def _remove_redundant_rows(m, tol):
    kept, signatures, removed = [], set(), 0
    for c in m.constraints:
        coeffs = {k: v for k, v in c.coefficients.items() if abs(v) > tol}
        if not coeffs:
            if _satisfies(0, c.operator, c.rhs, tol): removed += 1; continue
        scale = max(abs(v) for v in coeffs.values())
        signature = (c.operator, tuple(sorted((k, round(v / scale, 10)) for k, v in coeffs.items())), round(c.rhs / scale, 10))
        if signature in signatures: removed += 1; continue
        signatures.add(signature); kept.append(c)
    m.constraints = kept
    return m, removed


def _process_singletons(m, tol):
    model, count, infeasible = _propagate_bounds(m, tol)
    return model, count, infeasible


def _substitute_singleton_equalities(m, tol):
    for row_index, c in enumerate(m.constraints):
        if c.operator != "=" or len(c.coefficients) != 2: continue
        # Only eliminate a continuous variable with finite implied value relation; avoid integer substitutions.
        target, coefficient = next(iter(c.coefficients.items()))
        variable = next(v for v in m.variables if v.name == target)
        if variable.type != "continuous": continue
        other, other_coefficient = next((k, v) for k, v in c.coefficients.items() if k != target)
        for row in m.constraints:
            if row is c: continue
            amount = row.coefficients.pop(target, 0.0)
            row.coefficients[other] = row.coefficients.get(other, 0.0) + amount * (-other_coefficient / coefficient)
            row.rhs -= amount * c.rhs / coefficient
        linear = m.objective.pop(target, 0.0)
        m.objective[other] = m.objective.get(other, 0.0) - linear * other_coefficient / coefficient
        m.constraints.pop(row_index); m.variables = [v for v in m.variables if v.name != target]; m.bounds.pop(target, None)
        return m, 1
    return m, 0


def _satisfies(value, operator, rhs, tol):
    return (operator == "=" and abs(value - rhs) <= tol) or (operator == "<=" and value <= rhs + tol) or (operator == ">=" and value >= rhs - tol)


def _proven_unbounded(m, tol):
    for v in m.variables:
        coefficient = m.objective.get(v.name, 0.0)
        if m.objective_sense == "minimize": coefficient = -coefficient
        lo, hi = _bounds(m)[v.name]
        if coefficient > tol and hi == inf and all(abs(c.coefficients.get(v.name, 0.0)) <= tol for c in m.constraints): return True
        if coefficient < -tol and lo is None and all(abs(c.coefficients.get(v.name, 0.0)) <= tol for c in m.constraints): return True
    return False
