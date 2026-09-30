#pragma once
#include "milp/LPRelaxation.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

namespace sovereign {
enum class FpStatus { Feasible, NoFeasibleSolution, LPFailure, NumericalFailure };
struct FeasibilityPumpResult {
    FpStatus status{FpStatus::NoFeasibleSolution};
    std::vector<double> solution;
    double objective = 0;
    std::size_t iterations = 0, roundingAttempts = 0, projections = 0, perturbations = 0;
    bool verified = false;
    std::string message;
};

class FeasibilityPump {
public:
    FeasibilityPump(double tol = 1e-7, std::size_t maxIt = 100,
                    std::size_t lpLimit = 10000)
        : tol_(tol), maxIterations_(maxIt), lpIterationLimit_(lpLimit) {}

    FeasibilityPumpResult solve(const Model& original,
                                LPMethod method = LPMethod::RevisedSimplex) const {
        FeasibilityPumpResult r;
        auto root = solveLPRelaxation(original, method, lpIterationLimit_);
        if (root.status != LPStatus::Optimal) {
            r.status = FpStatus::LPFailure;
            r.message = "initial LP relaxation failed";
            return r;
        }
        std::vector<double> point = root.solution, candidate;
        std::set<std::string> seen;
        std::vector<std::size_t> ints;
        for (const auto& v : original.variables)
            if (v.type == VariableType::Integer || v.type == VariableType::Binary)
                ints.push_back(v.originalId);
        for (std::size_t it = 0; it < maxIterations_; ++it) {
            r.iterations = it + 1;
            ++r.roundingAttempts;
            candidate = point;
            for (auto id : ints) {
                double value = std::round(point[id]);
                const auto& v = original.variables[id];
                double lo = std::isfinite(v.lower) ? std::ceil(v.lower - tol_) : -1e15;
                double hi = std::isfinite(v.upper) ? std::floor(v.upper + tol_) : 1e15;
                if (v.type == VariableType::Binary) { lo = std::max(lo, 0.0); hi = std::min(hi, 1.0); }
                if (lo > hi) { r.status = FpStatus::NoFeasibleSolution; r.message = "integer bounds contain no integer value"; return r; }
                candidate[id] = std::max(lo, std::min(hi, value));
            }
            if (verify(original, candidate, tol_)) {
                r.status = FpStatus::Feasible; r.solution = candidate;
                r.objective = evaluateObjective(original, candidate); r.verified = true;
                r.message = "verified integer-feasible incumbent"; return r;
            }
            std::string key = keyOf(candidate, ints);
            if (!seen.insert(key).second) {
                ++r.perturbations;
                if (!ints.empty()) {
                    std::size_t id = ints[it % ints.size()];
                    const auto& v = original.variables[id];
                    double lo = std::isfinite(v.lower) ? std::ceil(v.lower - tol_) : std::floor(candidate[id]) - 1;
                    double hi = std::isfinite(v.upper) ? std::floor(v.upper + tol_) : std::ceil(candidate[id]) + 1;
                    if (v.type == VariableType::Binary) { lo = std::max(0.0, lo); hi = std::min(1.0, hi); }
                    if (candidate[id] < hi) candidate[id] += 1;
                    else if (candidate[id] > lo) candidate[id] -= 1;
                    key = keyOf(candidate, ints); seen.insert(key);
                }
            }
            Model projection = makeProjection(original, candidate, ints);
            LPMethod projectionMethod = method == LPMethod::DualSimplex ? LPMethod::DualSimplex : LPMethod::RevisedSimplex;
            LPResult projected = LPSolver{}.solve(projection, projectionMethod, lpIterationLimit_);
            ++r.projections;
            if (projected.status != LPStatus::Optimal) {
                r.status = projected.status == LPStatus::Infeasible ? FpStatus::NoFeasibleSolution : FpStatus::LPFailure;
                r.message = "projection LP did not solve"; return r;
            }
            point.assign(projected.solution.primal.begin(), projected.solution.primal.begin() + original.variables.size());
            if (!finite(point)) { r.status = FpStatus::NumericalFailure; r.message = "non-finite projection"; return r; }
        }
        r.status = FpStatus::NoFeasibleSolution;
        r.message = "iteration limit reached; heuristic did not find a feasible point";
        return r;
    }

private:
    double tol_;
    std::size_t maxIterations_;
    std::size_t lpIterationLimit_;

    static std::string keyOf(const std::vector<double>& x, const std::vector<std::size_t>& ids) {
        std::string k;
        for (auto i : ids) k += std::to_string(static_cast<long long>(std::llround(x[i]))) + ":";
        return k;
    }
    static bool finite(const std::vector<double>& x) {
        for (double v : x) if (!std::isfinite(v)) return false;
        return true;
    }
    static Model makeProjection(const Model& src, const std::vector<double>& rounded,
                                const std::vector<std::size_t>& ids) {
        Model p = relaxMILP(src); p.sense = Sense::Minimize; p.objective.clear();
        std::size_t next = p.variables.size();
        for (auto id : ids) {
            std::size_t d = next++;
            p.variables.push_back({d, d, "fp_dev_" + std::to_string(id), VariableType::Continuous, 0, INF, true});
            p.objective[d] = 1;
            Constraint a; a.originalId = p.constraints.size(); a.name = "fp_pos_" + std::to_string(id);
            a.coefficients[id] = 1; a.coefficients[d] = -1; a.relation = Relation::LessEqual; a.rhs = rounded[id]; p.constraints.push_back(a);
            Constraint b; b.originalId = p.constraints.size(); b.name = "fp_neg_" + std::to_string(id);
            b.coefficients[id] = -1; b.coefficients[d] = -1; b.relation = Relation::LessEqual; b.rhs = -rounded[id]; p.constraints.push_back(b);
        }
        p.rebuildMappings(); return p;
    }
    static bool verify(const Model& m, const std::vector<double>& x, double t) {
        if (!finite(x)) return false;
        for (const auto& v : m.variables) {
            if (v.originalId >= x.size() || x[v.originalId] < v.lower - t || (std::isfinite(v.upper) && x[v.originalId] > v.upper + t)) return false;
            if ((v.type == VariableType::Integer || v.type == VariableType::Binary) && std::abs(x[v.originalId] - std::round(x[v.originalId])) > t) return false;
        }
        for (const auto& c : m.constraints) {
            double a = 0; for (auto q : c.coefficients) a += q.second * x[q.first];
            if (!std::isfinite(a)) return false;
            if (c.relation == Relation::Equal && std::abs(a - c.rhs) > t) return false;
            if (c.relation == Relation::LessEqual && a > c.rhs + t) return false;
            if (c.relation == Relation::GreaterEqual && a < c.rhs - t) return false;
        }
        return true;
    }
};
}
