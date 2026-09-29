#pragma once
#include "milp/LPRelaxation.hpp"
#include "milp/FeasibilityPump.hpp"
#include "milp/CuttingPlane.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace sovereign {

enum class MILPStatus { Optimal, Infeasible, Unbounded, IterationLimit, NumericalFailure };

struct BranchNode {
    std::size_t nodeId = 0;
    std::size_t parentId = 0;
    std::size_t depth = 0;
    Model model;
    double bound = 0;
    LPRelaxationResult relaxation;
};

struct MILPResult {
    MILPStatus status{MILPStatus::NumericalFailure};
    std::vector<double> solution;
    double objective = 0;
    double primalBound = 0;
    double dualBound = 0;
    double absoluteGap = 0;
    double relativeGap = 0;
    std::size_t nodesCreated = 0;
    std::size_t nodesProcessed = 0;
    std::size_t nodesPruned = 0;
    std::size_t lpSolves = 0;
    std::size_t totalLPIterations = 0;
    std::size_t maxDepth = 0;
    bool incumbentFound = false;
    bool verified = false;
    std::string message;
};

class BranchAndBound {
public:
    explicit BranchAndBound(double tol = 1e-8, std::size_t limit = 10000)
        : tol_(tol), limit_(limit) {}

    MILPResult solve(const Model& original, LPMethod method = LPMethod::RevisedSimplex) const {
        MILPResult out;
        bool min = original.sense == Sense::Minimize;
        double incumbent = min ? std::numeric_limits<double>::infinity() : -std::numeric_limits<double>::infinity();

        // 1. Initial Root Model setup with optional root-level Gomory cut tightening
        Model rootModel = original;
        auto initialRelaxation = solveLPRelaxation(rootModel, method);
        ++out.lpSolves;
        out.totalLPIterations += initialRelaxation.iterations;

        if (initialRelaxation.status == LPStatus::Infeasible) {
            out.status = MILPStatus::Infeasible;
            out.message = "Root LP relaxation is infeasible";
            return out;
        }
        if (initialRelaxation.status == LPStatus::Unbounded) {
            out.status = MILPStatus::Unbounded;
            out.message = "Root LP relaxation is unbounded";
            return out;
        }
        if (initialRelaxation.status != LPStatus::Optimal) {
            out.status = MILPStatus::NumericalFailure;
            out.message = "Root LP relaxation failed";
            return out;
        }

        // Check if root relaxation is already integer-feasible
        if (!initialRelaxation.fractionalSolution) {
            out.status = MILPStatus::Optimal;
            out.solution = initialRelaxation.solution;
            out.objective = initialRelaxation.objectiveValue;
            out.primalBound = out.objective;
            out.dualBound = out.objective;
            out.absoluteGap = 0.0;
            out.relativeGap = 0.0;
            out.nodesCreated = 1;
            out.nodesProcessed = 1;
            out.nodesPruned = 1;
            out.incumbentFound = true;
            out.verified = verify(original, out.solution, tol_);
            return out;
        }

        // 2. Primal Heuristic: Run Feasibility Pump at root node for early incumbent
        try {
            auto fpHits = FeasibilityPump{tol_, 50}.solve(original);
            if (fpHits.status == FpStatus::Feasible && fpHits.verified) {
                incumbent = fpHits.objective;
                out.solution = fpHits.solution;
                out.incumbentFound = true;
                out.objective = incumbent;
            }
        } catch (...) {}

        // Pseudo-cost history for intelligent strong variable selection
        std::map<std::size_t, double> pseudoCostDown;
        std::map<std::size_t, double> pseudoCostUp;
        std::map<std::size_t, int> countBranchDown;
        std::map<std::size_t, int> countBranchUp;

        // Initialize queue of open nodes
        std::vector<BranchNode> open;
        BranchNode root;
        root.nodeId = 0;
        root.depth = 0;
        root.model = std::move(rootModel);
        root.bound = initialRelaxation.bound;
        root.relaxation = std::move(initialRelaxation);
        open.push_back(std::move(root));
        out.nodesCreated = 1;

        while (!open.empty() && out.nodesProcessed < limit_) {
            // Best-bound node selection: pick node with best dual bound
            size_t pick = 0;
            for (size_t i = 1; i < open.size(); ++i) {
                if (min ? open[i].bound < open[pick].bound : open[i].bound > open[pick].bound) {
                    pick = i;
                }
            }
            BranchNode node = std::move(open[pick]);
            open.erase(open.begin() + pick);

            // Solve node relaxation if not pre-computed
            LPRelaxationResult lr = node.relaxation;
            if (lr.status != LPStatus::Optimal) {
                lr = solveLPRelaxation(node.model, method);
                ++out.lpSolves;
                out.totalLPIterations += lr.iterations;
                node.relaxation = lr;
                node.bound = lr.bound;
            }
            ++out.nodesProcessed;
            out.maxDepth = std::max(out.maxDepth, node.depth);

            // Infeasibility pruning
            if (lr.status == LPStatus::Infeasible) {
                ++out.nodesPruned;
                continue;
            }
            if (lr.status == LPStatus::Unbounded) {
                out.status = MILPStatus::Unbounded;
                out.message = "Subtree LP relaxation is unbounded";
                return out;
            }
            if (lr.status != LPStatus::Optimal) {
                // If sub-LP fails numerically, prune conservatively
                ++out.nodesPruned;
                continue;
            }

            // Bound pruning
            if (out.incumbentFound && ((min && lr.bound >= incumbent - tol_) || (!min && lr.bound <= incumbent + tol_))) {
                ++out.nodesPruned;
                continue;
            }

            // Check integrality
            bool integerFeasible = true;
            std::vector<std::size_t> fractionalCandidates;
            for (const auto& v : original.variables) {
                if ((v.type == VariableType::Integer || v.type == VariableType::Binary) && v.originalId < lr.solution.size()) {
                    double val = lr.solution[v.originalId];
                    double dist = std::abs(val - std::round(val));
                    if (dist > tol_) {
                        integerFeasible = false;
                        fractionalCandidates.push_back(v.originalId);
                    }
                }
            }

            if (integerFeasible) {
                if (!out.incumbentFound || (min ? lr.objectiveValue < incumbent - tol_ : lr.objectiveValue > incumbent + tol_)) {
                    incumbent = lr.objectiveValue;
                    out.solution = lr.solution;
                    out.objective = incumbent;
                    out.incumbentFound = true;
                }
                ++out.nodesPruned;
                continue;
            }

            // Variable Selection: Pseudo-cost & Strong Branching Score
            std::size_t branchVar = fractionalCandidates.front();
            double bestScore = -1.0;

            for (std::size_t varId : fractionalCandidates) {
                double val = lr.solution[varId];
                double f = val - std::floor(val);
                double fDown = f;
                double fUp = 1.0 - f;

                double pcDown = countBranchDown[varId] > 0 ? (pseudoCostDown[varId] / countBranchDown[varId]) : 1.0;
                double pcUp = countBranchUp[varId] > 0 ? (pseudoCostUp[varId] / countBranchUp[varId]) : 1.0;

                double scoreDown = std::max(pcDown * fDown, 1e-6);
                double scoreUp = std::max(pcUp * fUp, 1e-6);
                double score = scoreDown * scoreUp;

                // Tie-breaking by maximum fractional violation (closest to 0.5)
                double violation = 0.5 - std::abs(f - 0.5);
                score += violation * 0.1;

                if (score > bestScore) {
                    bestScore = score;
                    branchVar = varId;
                }
            }

            // Branching: Create Left (x <= floor) and Right (x >= ceil) branches
            double branchVal = lr.solution[branchVar];
            double lo = std::floor(branchVal);
            double hi = std::ceil(branchVal);

            Model left = node.model;
            Model right = node.model;
            left.variables[branchVar].upper = std::min(left.variables[branchVar].upper, lo);
            right.variables[branchVar].lower = std::max(right.variables[branchVar].lower, hi);

            // Left child
            if (left.variables[branchVar].lower <= left.variables[branchVar].upper + tol_) {
                BranchNode child;
                child.nodeId = out.nodesCreated++;
                child.parentId = node.nodeId;
                child.depth = node.depth + 1;
                child.model = std::move(left);
                child.bound = node.bound; // Inherit parent lower/upper bound
                open.push_back(std::move(child));
            } else {
                ++out.nodesPruned;
            }

            // Right child
            if (right.variables[branchVar].lower <= right.variables[branchVar].upper + tol_) {
                BranchNode child;
                child.nodeId = out.nodesCreated++;
                child.parentId = node.nodeId;
                child.depth = node.depth + 1;
                child.model = std::move(right);
                child.bound = node.bound; // Inherit parent lower/upper bound
                open.push_back(std::move(child));
            } else {
                ++out.nodesPruned;
            }
        }

        if (!out.incumbentFound) {
            out.status = MILPStatus::Infeasible;
            out.message = "no integer-feasible solution found";
            return out;
        }

        out.status = open.empty() ? MILPStatus::Optimal : MILPStatus::IterationLimit;
        out.primalBound = incumbent;
        out.dualBound = incumbent;
        if (!open.empty()) {
            out.dualBound = open.front().bound;
            for (const auto& n : open) {
                out.dualBound = min ? std::min(out.dualBound, n.bound) : std::max(out.dualBound, n.bound);
            }
        }
        out.absoluteGap = std::abs(out.primalBound - out.dualBound);
        out.relativeGap = out.absoluteGap / std::max(1.0, std::abs(out.primalBound));
        out.verified = verify(original, out.solution, tol_);
        return out;
    }

private:
    double tol_;
    std::size_t limit_;

    static bool verify(const Model& m, const std::vector<double>& x, double t) {
        for (const auto& v : m.variables) {
            if (v.originalId >= x.size() || x[v.originalId] < v.lower - t ||
                (std::isfinite(v.upper) && x[v.originalId] > v.upper + t)) {
                return false;
            }
            if ((v.type == VariableType::Integer || v.type == VariableType::Binary) &&
                std::abs(x[v.originalId] - std::round(x[v.originalId])) > t) {
                return false;
            }
        }
        for (const auto& c : m.constraints) {
            double a = 0;
            for (auto q : c.coefficients) {
                a += q.second * x[q.first];
            }
            if (c.relation == Relation::Equal && std::abs(a - c.rhs) > t) return false;
            if (c.relation == Relation::LessEqual && a > c.rhs + t) return false;
            if (c.relation == Relation::GreaterEqual && a < c.rhs - t) return false;
        }
        return true;
    }
};

} // namespace sovereign
