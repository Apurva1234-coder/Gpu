"""Optional reference-solver adapters used only by the benchmark comparison UI."""
from __future__ import annotations

import importlib
import importlib.util
import math
import time
from functools import lru_cache
from typing import Any

from .benchmark import verify_original


def _finite(value: Any) -> float | None:
    try:
        number = float(value)
        return number if math.isfinite(number) else None
    except (TypeError, ValueError, OverflowError):
        return None


def _elapsed_ms(started_ns: int) -> float:
    return (time.perf_counter_ns() - started_ns) / 1e6


def _gurobi_license_status(error: Exception) -> str:
    code = getattr(error, "errno", getattr(error, "code", None))
    message = str(error).lower()
    if code == 10010 or "size-limited" in message or "model too large" in message:
        return "LICENSE_LIMIT"
    return "LICENSE_UNAVAILABLE"


def _probe_highs(module: Any) -> None:
    """Confirm HiGHS can optimize a tiny bounded LP in this environment."""
    engine = module.Highs()
    engine.setOptionValue("output_flag", False)
    engine.addVar(0.0, module.kHighsInf)
    engine.changeColCost(0, 1.0)
    engine.addRow(1.0, module.kHighsInf, 1, [0], [1.0])
    engine.run()
    solution = engine.getSolution()
    if "OPTIMAL" not in str(engine.modelStatusToString(engine.getModelStatus())).upper() or not solution.value_valid:
        raise RuntimeError("HiGHS could not solve the availability probe LP.")
    if not solution.col_value or abs(float(solution.col_value[0]) - 1.0) > 1e-7:
        raise RuntimeError("HiGHS availability probe returned an invalid solution.")


def _probe_gurobi(module: Any) -> None:
    """Exercise both Gurobi initialization and the active local license."""
    from gurobipy import GRB
    with module.Env(empty=True) as environment:
        environment.setParam("OutputFlag", 0)
        environment.start()
        with module.Model(env=environment) as solver:
            solver.Params.OutputFlag = 0
            variable = solver.addVar(lb=0.0)
            solver.addConstr(variable >= 1.0)
            solver.setObjective(variable, GRB.MINIMIZE)
            solver.optimize()
            if solver.Status != GRB.OPTIMAL or solver.SolCount < 1 or abs(variable.X - 1.0) > 1e-7:
                raise RuntimeError("Gurobi license probe did not solve its test LP to optimality.")


def _probe_cplex(module: Any) -> None:
    """Confirm a CPLEX runtime and usable license by solving a tiny LP."""
    engine = module.Cplex()
    engine.set_log_stream(None); engine.set_error_stream(None); engine.set_warning_stream(None); engine.set_results_stream(None)
    engine.variables.add(obj=[1.0], lb=[0.0], names=["probe_x"])
    engine.linear_constraints.add(
        lin_expr=[module.SparsePair(ind=["probe_x"], val=[1.0])], senses=["G"], rhs=[1.0]
    )
    engine.solve()
    if not engine.solution.is_optimal() or abs(engine.solution.get_values("probe_x")[0] - 1.0) > 1e-7:
        raise RuntimeError("CPLEX license probe did not solve its test LP to optimality.")


@lru_cache(maxsize=1)
def comparator_availability() -> dict[str, dict[str, Any]]:
    """Report engines only after the installed solver and license pass a real LP."""
    result: dict[str, dict[str, Any]] = {"sovereign": {"available": True, "status": "AVAILABLE"}}
    for name, module_name in (("highs", "highspy"), ("gurobi", "gurobipy"), ("cplex", "cplex")):
        try:
            if importlib.util.find_spec(module_name) is None:
                result[name] = {"available": False, "status": "NOT_INSTALLED", "reason": f"{module_name} is not installed"}
                continue
            module = importlib.import_module(module_name)
            if name == "highs":
                _probe_highs(module)
            elif name == "gurobi":
                _probe_gurobi(module)
            else:
                _probe_cplex(module)
            result[name] = {"available": True, "status": "AVAILABLE"}
        except Exception as exc:
            label = _gurobi_license_status(exc) if name == "gurobi" else "LICENSE_UNAVAILABLE" if name == "cplex" else "UNAVAILABLE"
            result[name] = {"available": False, "status": label, "reason": str(exc)[:240]}
    return result


def _common(model: Any, status: str, solver_time_ms: float | None, objective: float | None = None,
            primal: list[float] | None = None, iterations: int | None = None,
            nodes: int | None = None, bound: float | None = None,
            gap: float | None = None, error: str | None = None,
            include_solution: bool = False) -> dict[str, Any]:
    check = verify_original(model, primal) if primal is not None else None
    result = {
        "status": status,
        "objective": _finite(check["objective"] if check and check.get("objective") is not None else objective),
        "solve_time_ms": _finite(solver_time_ms),
        "iterations": _finite(iterations),
        "nodes": _finite(nodes),
        "best_bound": _finite(bound),
        "relative_gap": _finite(gap),
        "verification": "PASS" if check and check["pass"] else "FAIL" if check else "N/A",
        "primal_residual": _finite(check.get("primal_residual")) if check else None,
        "failure_reason": check.get("failure_reason") if check else error,
    }
    if include_solution:
        result["primal_values"] = list(primal) if primal is not None else None
    return result


def solve_comparator(name: str, model: Any, time_limit_seconds: float,
                     include_solution: bool = False,
                     gurobi_method: int | None = None,
                     check_availability: bool = True) -> dict[str, Any]:
    """Solve a single uploaded model with an installed optional engine."""
    if check_availability:
        availability = comparator_availability().get(name, {})
        if not availability.get("available"):
            status = availability.get("status", "NOT_AVAILABLE")
            return {"solver": name, "status": status, "objective": None, "solve_time_ms": None,
                    "iterations": None, "nodes": None, "best_bound": None, "relative_gap": None,
                    "verification": "N/A", "failure_reason": availability.get("reason")}
    try:
        if name == "highs":
            result = _solve_highs(model, time_limit_seconds, include_solution)
        elif name == "gurobi":
            result = _solve_gurobi(model, time_limit_seconds, include_solution, gurobi_method)
        elif name == "cplex":
            result = _solve_cplex(model, time_limit_seconds, include_solution)
        else:
            return {"solver": name, "status": "UNSUPPORTED", "verification": "N/A"}
        result["solver"] = name
        return result
    except Exception as exc:
        status = _gurobi_license_status(exc) if name == "gurobi" else "FAILED"
        return {"solver": name, "status": status, "objective": None, "solve_time_ms": None,
                "iterations": None, "nodes": None, "best_bound": None, "relative_gap": None,
                "verification": "N/A", "failure_reason": str(exc)[:500]}


def _solve_highs(model: Any, time_limit: float, include_solution: bool = False) -> dict[str, Any]:
    if model.has_quadratic_objective:
        return {"solver": "highs", "status": "UNSUPPORTED", "objective": None, "solve_time_ms": None,
                "iterations": None, "nodes": None, "best_bound": None, "relative_gap": None,
                "verification": "N/A", "failure_reason": "HiGHS adapter supports LP/MILP, not QP."}
    import highspy
    highs = highspy.Highs()
    highs.setOptionValue("output_flag", False)
    highs.setOptionValue("time_limit", float(time_limit))
    inf = highspy.kHighsInf
    by_name = {variable.name: i for i, variable in enumerate(model.variables)}
    for i, variable in enumerate(model.variables):
        lower, upper = model.bounds.get(variable.name, (0.0, 1.0 if variable.type == "binary" else None))
        highs.addVar(-inf if lower is None else float(lower), inf if upper is None else float(upper))
        highs.changeColCost(i, float(model.objective.get(variable.name, 0.0)))
        if variable.type in {"integer", "binary"}:
            highs.changeColIntegrality(i, highspy.HighsVarType.kInteger)
    if hasattr(highs, "changeObjectiveOffset"):
        highs.changeObjectiveOffset(float(model.objective_constant))
    for row in model.constraints:
        lower, upper = (-inf, inf)
        if row.operator == "=": lower = upper = float(row.rhs)
        elif row.operator == "<=": upper = float(row.rhs)
        else: lower = float(row.rhs)
        indexes = [by_name[key] for key in row.coefficients]
        values = [float(value) for value in row.coefficients.values()]
        highs.addRow(lower, upper, len(indexes), indexes, values)
    highs.setMaximize() if model.objective_sense == "maximize" else highs.setMinimize()
    started = time.perf_counter_ns()
    highs.run()
    solve_time_ms = _elapsed_ms(started)
    text = str(highs.modelStatusToString(highs.getModelStatus())).upper()
    info, solution = highs.getInfo(), highs.getSolution()
    status = ("OPTIMAL" if "OPTIMAL" in text else "TIME_LIMIT" if "TIME" in text else
              "INFEASIBLE" if "INFEASIBLE" in text else "UNBOUNDED" if "UNBOUNDED" in text else
              "FEASIBLE" if solution.value_valid else "FAILED")
    primal = list(solution.col_value) if solution.value_valid else None
    nodes = _finite(getattr(info, "mip_node_count", None)) if model.has_discrete_variables else None
    nodes = int(nodes) if nodes is not None and nodes >= 0 else None
    iterations = _finite(getattr(info, "simplex_iteration_count", None))
    iterations = int(iterations) if iterations is not None and iterations >= 0 else None
    return _common(model, status, solve_time_ms, float(info.objective_function_value) if solution.value_valid else None, primal,
                   iterations, nodes,
                   getattr(info, "mip_dual_bound", None) if model.has_discrete_variables else None,
                   getattr(info, "mip_gap", None) if model.has_discrete_variables else None,
                   None if primal is not None else text, include_solution)


def _solve_gurobi(model: Any, time_limit: float, include_solution: bool = False,
                  method: int | None = None) -> dict[str, Any]:
    import gurobipy as gp
    from gurobipy import GRB
    with gp.Env(empty=True) as env:
        env.setParam("OutputFlag", 0)
        env.start()
        with gp.Model(env=env) as solver:
            solver.Params.OutputFlag = 0
            solver.Params.TimeLimit = float(time_limit)
            if method is not None:
                solver.Params.Method = int(method)
            variables = {}
            for variable in model.variables:
                lower, upper = model.bounds.get(variable.name, (0.0, 1.0 if variable.type == "binary" else None))
                vtype = GRB.BINARY if variable.type == "binary" else GRB.INTEGER if variable.type == "integer" else GRB.CONTINUOUS
                variables[variable.name] = solver.addVar(lb=-GRB.INFINITY if lower is None else lower,
                    ub=GRB.INFINITY if upper is None else upper, vtype=vtype, name=variable.name)
            solver.update()
            objective = gp.quicksum(float(c) * variables[n] for n, c in model.objective.items()) + float(model.objective_constant)
            if model.has_quadratic_objective:
                objective += gp.quicksum(0.5 * float(c) * variables[n] * variables[n] for n, c in model.quadratic_terms.items())
            solver.setObjective(objective, GRB.MAXIMIZE if model.objective_sense == "maximize" else GRB.MINIMIZE)
            for row in model.constraints:
                lhs = gp.quicksum(float(c) * variables[n] for n, c in row.coefficients.items())
                if row.operator == "=": solver.addConstr(lhs == row.rhs, name=row.name)
                elif row.operator == "<=": solver.addConstr(lhs <= row.rhs, name=row.name)
                else: solver.addConstr(lhs >= row.rhs, name=row.name)
            started = time.perf_counter_ns()
            solver.optimize()
            solve_time_ms = _elapsed_ms(started)
            sol_count = int(solver.SolCount)
            status = "OPTIMAL" if solver.Status == GRB.OPTIMAL else "TIME_LIMIT" if solver.Status == GRB.TIME_LIMIT else "INFEASIBLE" if solver.Status == GRB.INFEASIBLE else "UNBOUNDED" if solver.Status == GRB.UNBOUNDED else "FEASIBLE" if sol_count else "FAILED"
            primal = [float(variables[v.name].X) for v in model.variables] if sol_count else None
            get = lambda attr: getattr(solver, attr) if hasattr(solver, attr) else None
            return _common(model, status, solve_time_ms, float(solver.ObjVal) if sol_count else None, primal,
                           int(solver.IterCount) if hasattr(solver, "IterCount") else None,
                           int(solver.NodeCount) if hasattr(solver, "NodeCount") else None,
                           float(solver.ObjBound) if sol_count and hasattr(solver, "ObjBound") else None,
                           float(solver.MIPGap) if sol_count and model.has_discrete_variables and hasattr(solver, "MIPGap") else None,
                           None if sol_count else str(solver.Status), include_solution)


def _solve_cplex(model: Any, time_limit: float, include_solution: bool = False) -> dict[str, Any]:
    import cplex
    solver = cplex.Cplex()
    solver.set_log_stream(None); solver.set_error_stream(None); solver.set_warning_stream(None); solver.set_results_stream(None)
    solver.parameters.timelimit.set(float(time_limit))
    names = [v.name for v in model.variables]
    lower, upper, types = [], [], ""
    for variable in model.variables:
        lo, hi = model.bounds.get(variable.name, (0.0, 1.0 if variable.type == "binary" else None))
        lower.append(-solver.infinity if lo is None else float(lo)); upper.append(solver.infinity if hi is None else float(hi))
        types += "B" if variable.type == "binary" else "I" if variable.type == "integer" else "C"
    solver.variables.add(obj=[float(model.objective.get(name, 0.0)) for name in names], lb=lower, ub=upper, types=types, names=names)
    senses = ""; lin_expr = []; rhs = []
    for row in model.constraints:
        senses += {"=": "E", "<=": "L", ">=": "G"}[row.operator]
        lin_expr.append(cplex.SparsePair(ind=list(row.coefficients), val=[float(v) for v in row.coefficients.values()]))
        rhs.append(float(row.rhs))
    if lin_expr: solver.linear_constraints.add(lin_expr=lin_expr, senses=senses, rhs=rhs)
    if model.has_quadratic_objective:
        solver.objective.set_quadratic([[name, name, float(model.quadratic_terms.get(name, 0.0))] for name in names])
    solver.objective.set_offset(float(model.objective_constant))
    solver.objective.set_sense(solver.objective.sense.maximize if model.objective_sense == "maximize" else solver.objective.sense.minimize)
    started = time.perf_counter_ns()
    solver.solve()
    solve_time_ms = _elapsed_ms(started)
    try:
        feasible = solver.solution.is_primal_feasible()
    except Exception:
        feasible = False
    text = solver.solution.get_status_string().upper()
    status = "OPTIMAL" if solver.solution.is_optimal() else "TIME_LIMIT" if "TIME" in text else "FEASIBLE" if feasible else "INFEASIBLE" if "INFEASIBLE" in text else "UNBOUNDED" if "UNBOUNDED" in text else "FAILED"
    primal = list(map(float, solver.solution.get_values())) if feasible else None
    metrics = nodes = None
    try:
        metrics = solver.solution.progress.get_num_iterations()
        nodes = solver.solution.progress.get_num_nodes_processed()
    except Exception:
        pass
    bound = solver.solution.MIP.get_best_objective() if model.has_discrete_variables and feasible else None
    gap = solver.solution.MIP.get_mip_relative_gap() if model.has_discrete_variables and feasible else None
    objective = float(solver.solution.get_objective_value()) if feasible else None
    return _common(model, status, solve_time_ms, objective, primal, metrics, nodes, bound, gap,
                   None if feasible else text, include_solution)
