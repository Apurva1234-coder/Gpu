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

enum class MILPStatus { Optimal, Infeasible, Unbounded, NodeLimit, IterationLimit, NumericalFailure };

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
    explicit BranchAndBound(double tol = 1e-8, std::size_t limit = 10000,
                            std::size_t lpIterationLimit = 10000)
        : tol_(tol), limit_(limit), lpIterationLimit_(lpIterationLimit == 0
              ? std::numeric_limits<std::size_t>::max() : lpIterationLimit) {}

    MILPResult solve(const Model& original, LPMethod method = LPMethod::RevisedSimplex) const {
        MILPResult out;
        bool min = original.sense == Sense::Minimize;
        double incumbent = min ? std::numeric_limits<double>::infinity() : -std::numeric_limits<double>::infinity();

        // 1. Initial Root Model setup with optional root-level Gomory cut tightening
        Model rootModel = original;
        std::vector<bool> appearsInRow(original.variables.size(), false);
        for(const auto& row:original.constraints)for(const auto& entry:row.coefficients)
            if(entry.first<appearsInRow.size())appearsInRow[entry.first]=true;
        std::vector<std::pair<std::size_t,double>> isolatedValues;
        for(std::size_t i=0;i<rootModel.variables.size();++i){
            auto& variable=rootModel.variables[i];
            if(!variable.active||appearsInRow[i])continue;
            const auto objective=original.objective.find(i);
            const double coefficient=objective==original.objective.end()?0.0:objective->second;
            double lower=variable.lower,upper=variable.upper;
            if(variable.type==VariableType::Binary){lower=std::max(0.0,lower);upper=std::min(1.0,upper);}
            if(variable.type==VariableType::Integer||variable.type==VariableType::Binary){
                if(std::isfinite(lower))lower=std::ceil(lower-tol_);
                if(std::isfinite(upper))upper=std::floor(upper+tol_);
            }
            if(lower>upper+tol_){out.status=MILPStatus::Infeasible;out.message="isolated integer variable has no feasible bound value";return out;}
            const bool chooseUpper=(min==(coefficient<0.0));
            double value=chooseUpper?upper:lower;
            if(coefficient==0.0){
                if(lower<=0.0&&upper>=0.0)value=0.0;
                else value=std::abs(lower)<std::abs(upper)?lower:upper;
            }
            if(!std::isfinite(value))continue;
            isolatedValues.emplace_back(i,value);
            variable.lower=variable.upper=value;
            variable.active=false;
        }
        auto restoreIsolated=[&](LPRelaxationResult& relaxation){
            if(relaxation.solution.size()<original.variables.size())relaxation.solution.resize(original.variables.size(),0.0);
            for(const auto& fixed:isolatedValues)relaxation.solution[fixed.first]=fixed.second;
            relaxation.objectiveValue=evaluateObjective(original,relaxation.solution);
            relaxation.bound=relaxation.objectiveValue;
            relaxation.fractionalIntegerVariables=0;
            for(const auto& v:original.variables)if((v.type==VariableType::Integer||v.type==VariableType::Binary)&&v.originalId<relaxation.solution.size()&&std::abs(relaxation.solution[v.originalId]-std::round(relaxation.solution[v.originalId]))>tol_)++relaxation.fractionalIntegerVariables;
            relaxation.fractionalSolution=relaxation.fractionalIntegerVariables>0;
            relaxation.integralWithinTolerance=!relaxation.fractionalSolution;
        };
        auto initialRelaxation = solveLPRelaxation(rootModel, method, lpIterationLimit_);
        if(initialRelaxation.status==LPStatus::Optimal)restoreIsolated(initialRelaxation);
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
            out.status = initialRelaxation.status == LPStatus::IterationLimit
                ? MILPStatus::IterationLimit : MILPStatus::NumericalFailure;
            out.message = initialRelaxation.status == LPStatus::IterationLimit
                ? "Root LP relaxation reached the configured per-node LP iteration limit"
                : "Root LP relaxation failed: " + initialRelaxation.message;
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
        if(isolatedValues.empty())try {
            auto fpHits = FeasibilityPump{tol_, 50, lpIterationLimit_}.solve(original, method);
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

        while (!open.empty() && (limit_ == 0 || out.nodesProcessed < limit_)) {
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
                lr = solveLPRelaxation(node.model, method, lpIterationLimit_);
                if(lr.status==LPStatus::Optimal)restoreIsolated(lr);
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
                out.status = lr.status == LPStatus::IterationLimit
                    ? MILPStatus::IterationLimit : MILPStatus::NumericalFailure;
                out.message = lr.status == LPStatus::IterationLimit
                    ? "Node LP relaxation reached the configured per-node LP iteration limit"
                    : "Node LP relaxation failed: " + lr.message;
                if (lr.status == LPStatus::IterationLimit && out.incumbentFound) {
                    out.objective = incumbent;
                    out.primalBound = incumbent;
                    out.dualBound = node.bound;
                    for (const auto& openNode : open)
                        out.dualBound = min ? std::min(out.dualBound, openNode.bound) : std::max(out.dualBound, openNode.bound);
                    out.absoluteGap = std::abs(out.primalBound - out.dualBound);
                    out.relativeGap = out.absoluteGap / std::max(1.0, std::abs(out.primalBound));
                    out.verified = verify(original, out.solution, tol_);
                }
                return out;
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

        const bool nodeLimitReached = !open.empty() && limit_ != 0 && out.nodesProcessed >= limit_;
        if (!out.incumbentFound) {
            out.status = nodeLimitReached ? MILPStatus::NodeLimit : MILPStatus::Infeasible;
            out.message = nodeLimitReached ? "node limit reached before finding an integer-feasible solution" : "no integer-feasible solution found";
            if(nodeLimitReached&&!open.empty()){
                out.dualBound=open.front().bound;
                for(const auto& node:open)out.dualBound=min?std::min(out.dualBound,node.bound):std::max(out.dualBound,node.bound);
            }
            return out;
        }

        out.status = open.empty() ? MILPStatus::Optimal : MILPStatus::NodeLimit;
        if (nodeLimitReached) out.message = "configured branch-and-bound node limit reached";
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
    std::size_t lpIterationLimit_;

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
