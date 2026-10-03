#pragma once
#include "milp/LPRelaxation.hpp"
#include "milp/FeasibilityPump.hpp"
#include "milp/CuttingPlane.hpp"
#include "presolve/Postsolve.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <vector>

namespace sovereign {

enum class MILPStatus { Optimal, Infeasible, Unbounded, NodeLimit, IterationLimit, TimeLimit, TimeLimitNoIncumbent, NumericalFailure };

struct BranchNode {
    std::size_t nodeId = 0;
    std::size_t parentId = 0;
    std::size_t depth = 0;
    struct BoundChange {
        std::shared_ptr<const BoundChange> parent;
        std::size_t variable = 0;
        bool lowerBound = false;
        double value = 0.0;
    };
    std::shared_ptr<const BoundChange> bounds;
    std::shared_ptr<const LPWarmStartState> warmStartState;
    std::size_t branchVariable = std::numeric_limits<std::size_t>::max();
    bool branchDown = false;
    double branchFractionalDistance = 0.0;
    double parentBound = 0.0;
    double bound = 0;
    LPRelaxationResult relaxation;
};

struct BestBoundFirst {
    bool minimize = true;
    bool operator()(const std::shared_ptr<BranchNode>& a,
                    const std::shared_ptr<BranchNode>& b) const {
        if (a->bound != b->bound)
            return minimize ? a->bound > b->bound : a->bound < b->bound;
        return a->nodeId > b->nodeId;
    }
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
    std::size_t nodesPrunedBeforeLP = 0;
    std::size_t nodeLPSolvesAvoidedByBound = 0;
    std::size_t lpSolves = 0;
    std::size_t totalLPIterations = 0;
    std::size_t rootLPIterations = 0;
    std::size_t nodeLPIterations = 0;
    std::size_t feasibilityPumpLPSolves = 0;
    std::size_t feasibilityPumpIterations = 0;
    std::size_t rootFractionalVariables = 0;
    std::size_t incumbentUpdates = 0;
    std::size_t rootRoundingHeuristicAttempts = 0;
    std::size_t rootRoundingHeuristicAccepted = 0;
    std::size_t firstIncumbentNode = 0;
    double firstIncumbentTimeMs = 0.0;
    std::size_t cutsGenerated = 0;
    std::size_t cutsAccepted = 0;
    std::size_t cutsRejected = 0;
    std::size_t warmStartsAttempted = 0;
    std::size_t warmStartsSuccessful = 0;
    std::size_t warmStartsFailed = 0;
    std::size_t coldFallbacks = 0;
    std::size_t coldStarts = 0;
    std::size_t rootLPSparseRefactorizations = 0;
    std::size_t rootLPSparsePivots = 0;
    std::size_t peakOpenNodes = 0;
    std::size_t maxDepth = 0;
    double rootLPTimeMs = 0.0;
    double rootLPBound = 0.0;
    bool hasRootLPBound = false;
    double rootLPStandardizationTimeMs = 0.0;
    double rootLPSparsePricingTimeMs = 0.0;
    double rootLPSparseBasisSolveTimeMs = 0.0;
    double rootLPSparseFactorizationTimeMs = 0.0;
    double feasibilityPumpTimeMs = 0.0;
    double branchAndBoundTimeMs = 0.0;
    double nodeSelectionTimeMs = 0.0;
    double nodeModelUpdateTimeMs = 0.0;
    double nodeLPTimeMs = 0.0;
    double nodeLPStandardizationTimeMs = 0.0;
    double nodeLPDenseSetupTimeMs = 0.0, nodeLPDensePricingTimeMs = 0.0;
    double nodeLPDenseRatioTestTimeMs = 0.0, nodeLPDensePivotTimeMs = 0.0;
    double nodeLPDenseWarmBasisRebuildTimeMs = 0.0, nodeLPDenseSolutionRecoveryTimeMs = 0.0;
    double nodeLPDenseVerificationTimeMs = 0.0, nodeLPDenseCutTimeMs = 0.0;
    double nodeLPSparseFactorizationTimeMs = 0.0;
    double branchingTimeMs = 0.0;
    double pruningTimeMs = 0.0;
    double verificationTimeMs = 0.0;
    double totalSolverTimeMs = 0.0;
    bool incumbentFound = false;
    bool verified = false;
    bool hasDualBound = false;
    std::string rootLPMethod;
    std::string message;
};

class BranchAndBound {
public:
    explicit BranchAndBound(double tol = 1e-8, std::size_t limit = 10000,
                            std::size_t lpIterationLimit = 10000, double timeLimitMs = 0.0,
                            bool enableWarmStarts = false)
        : tol_(tol), limit_(limit), lpIterationLimit_(lpIterationLimit == 0
              ? std::numeric_limits<std::size_t>::max() : lpIterationLimit),
          timeLimitMs_(timeLimitMs), enableWarmStarts_(enableWarmStarts) {}

    MILPResult solve(const Model& original, LPMethod method = LPMethod::RevisedSimplex) const {
        const auto solveStart = std::chrono::steady_clock::now();
        MILPResult result = solveInternal(original, method);
        result.totalSolverTimeMs = elapsedMs(solveStart);
        return result;
    }

    MILPResult solve(const Model& original, const PresolveResult& presolved,
                     LPMethod method = LPMethod::RevisedSimplex) const {
        const auto solveStart = std::chrono::steady_clock::now();
        if (presolved.status == PresolveStatus::Infeasible) {
            MILPResult result;
            result.status = MILPStatus::Infeasible;
            result.message = "integer-aware presolve proved the model infeasible";
            result.totalSolverTimeMs = elapsedMs(solveStart);
            return result;
        }
        MILPResult result = solveInternal(presolved.model, method);
        if (result.incumbentFound) {
            Solution reducedSolution;
            reducedSolution.primal = result.solution;
            Solution restored = postsolve(original, presolved, reducedSolution);
            result.solution = std::move(restored.primal);
            result.objective = evaluateObjective(original, result.solution);
            result.primalBound = result.objective;
            result.verified = verify(original, result.solution, tol_);
            if (result.status == MILPStatus::Optimal && !result.verified) {
                result.status = MILPStatus::NumericalFailure;
                result.message = "postsolved incumbent failed original-model verification";
            }
            if (result.status == MILPStatus::Optimal && result.verified) {
                result.dualBound = result.objective;
                result.hasDualBound = true;
                result.absoluteGap = 0.0;
                result.relativeGap = 0.0;
            } else if (result.hasDualBound) {
                result.absoluteGap = std::abs(result.primalBound - result.dualBound);
                result.relativeGap = result.absoluteGap / std::max(1.0, std::abs(result.primalBound));
            }
        }
        result.totalSolverTimeMs = elapsedMs(solveStart);
        return result;
    }

private:
    MILPResult solveInternal(const Model& original, LPMethod method) const {
        const auto solveStart = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point deadlineStorage;
        const std::chrono::steady_clock::time_point* deadline = nullptr;
        if (timeLimitMs_ > 0.0) {
            deadlineStorage = solveStart + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double, std::milli>(timeLimitMs_));
            deadline = &deadlineStorage;
        }
        auto timeLimitReached = [&]() { return deadline && std::chrono::steady_clock::now() >= *deadline; };
        MILPResult out;
        bool min = original.sense == Sense::Minimize;
        std::size_t originalNonzeros=0;
        for(const auto& row:original.constraints)if(row.active)originalNonzeros+=row.coefficients.size();
        const bool smallMILP=activeVariableCount(original)<=100&&activeConstraintCount(original)<=100&&originalNonzeros<=10000;
        const bool useWarmStarts=enableWarmStarts_&&smallMILP;
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
            for(const auto& v:original.variables)if(v.active&&(v.type==VariableType::Integer||v.type==VariableType::Binary)&&v.originalId<relaxation.solution.size()&&std::abs(relaxation.solution[v.originalId]-std::round(relaxation.solution[v.originalId]))>tol_)++relaxation.fractionalIntegerVariables;
            relaxation.fractionalSolution=relaxation.fractionalIntegerVariables>0;
            relaxation.integralWithinTolerance=!relaxation.fractionalSolution;
        };
        Model rootLPModel;
        auto materializeNodeModel=[&](const BranchNode& node){
            Model model=rootLPModel;
            std::vector<const BranchNode::BoundChange*> path;
            for(auto change=node.bounds;change;change=change->parent)path.push_back(change.get());
            for(auto change=path.rbegin();change!=path.rend();++change){
                auto& variable=model.variables[(*change)->variable];
                if((*change)->lowerBound)variable.lower=std::max(variable.lower,(*change)->value);
                else variable.upper=std::min(variable.upper,(*change)->value);
            }
            return model;
        };
        if (timeLimitReached()) {
            out.status = MILPStatus::TimeLimitNoIncumbent;
            out.message = "MILP wall-clock time limit reached before the root LP";
            return out;
        }
        const auto rootLPStart = std::chrono::steady_clock::now();
        rootLPModel=relaxMILP(rootModel);
        const auto rootStandardizationStart=std::chrono::steady_clock::now();
        StandardLP preparedRootLP;
        const StandardLP* preparedRootLPPtr=nullptr;
        if(denseLPWithinMemoryBudget(rootLPModel)){
            preparedRootLP=standardize(rootLPModel);
            preparedRootLPPtr=&preparedRootLP;
        }
        const double preparedRootStandardizationMs=preparedRootLPPtr?elapsedMs(rootStandardizationStart):0.0;
        auto initialRelaxation = solveLPRelaxation(rootModel, rootLPModel, method, lpIterationLimit_, deadline, nullptr, useWarmStarts, preparedRootLPPtr);
        out.rootLPTimeMs = elapsedMs(rootLPStart);
        out.rootLPBound = initialRelaxation.bound;
        out.hasRootLPBound = initialRelaxation.status == LPStatus::Optimal;
        out.rootLPMethod = initialRelaxation.method;
        out.rootLPIterations = initialRelaxation.iterations;
        out.rootLPStandardizationTimeMs = preparedRootStandardizationMs+initialRelaxation.standardizationTimeMs;
        out.rootLPSparsePricingTimeMs = initialRelaxation.sparsePricingTimeMs;
        out.rootLPSparseBasisSolveTimeMs = initialRelaxation.sparseBasisSolveTimeMs;
        out.rootLPSparseFactorizationTimeMs = initialRelaxation.sparseFactorizationTimeMs;
        out.rootLPSparseRefactorizations = initialRelaxation.sparseRefactorizations;
        out.rootLPSparsePivots = initialRelaxation.sparsePivots;
        ++out.coldStarts;
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
            const bool timedOut = timeLimitReached();
            out.status = timedOut ? MILPStatus::TimeLimitNoIncumbent : initialRelaxation.status == LPStatus::IterationLimit
                ? MILPStatus::IterationLimit : MILPStatus::NumericalFailure;
            out.message = timedOut ? "MILP wall-clock time limit reached during the root LP"
                : initialRelaxation.status == LPStatus::IterationLimit
                ? "Root LP relaxation reached the configured per-node LP iteration limit"
                : "Root LP relaxation failed: " + initialRelaxation.message;
            return out;
        }
        out.dualBound = initialRelaxation.bound;
        out.hasDualBound = true;

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
            const auto verificationStart = std::chrono::steady_clock::now();
            out.verified = verify(original, out.solution, tol_);
            out.verificationTimeMs += elapsedMs(verificationStart);
            return out;
        }
        out.rootFractionalVariables = initialRelaxation.fractionalIntegerVariables;

        // Cheap generic incumbent attempt: round the root LP's integer
        // coordinates to their nearest feasible integer values, then accept
        // only after full original-model verification.
        if(!out.incumbentFound) {
            ++out.rootRoundingHeuristicAttempts;
            auto rounded=initialRelaxation.solution;
            for(const auto& variable:original.variables) {
                if(!variable.active)continue;
                if(variable.type!=VariableType::Integer&&variable.type!=VariableType::Binary)continue;
                if(variable.originalId>=rounded.size())continue;
                double lower=variable.lower,upper=variable.upper;
                if(variable.type==VariableType::Binary){lower=std::max(0.0,lower);upper=std::min(1.0,upper);}
                const double integerLower=std::isfinite(lower)?std::ceil(lower-tol_):-std::numeric_limits<double>::infinity();
                const double integerUpper=std::isfinite(upper)?std::floor(upper+tol_):std::numeric_limits<double>::infinity();
                rounded[variable.originalId]=std::max(integerLower,std::min(integerUpper,std::round(rounded[variable.originalId])));
            }
            if(verify(original,rounded,tol_)) {
                incumbent=evaluateObjective(original,rounded);
                out.solution=std::move(rounded);out.objective=incumbent;out.incumbentFound=true;
                ++out.incumbentUpdates;++out.rootRoundingHeuristicAccepted;
                out.firstIncumbentNode=0;out.firstIncumbentTimeMs=elapsedMs(solveStart);
            }
        }

        // 2. Primal Heuristic: Run Feasibility Pump at root node for early incumbent.
        // A projection LP can have a long non-preemptible kernel. Avoid starting
        // the optional heuristic when the global budget cannot absorb its cap.
        const bool enoughTimeForFeasibilityPump = !deadline ||
            std::chrono::duration<double, std::milli>(*deadline - std::chrono::steady_clock::now()).count() >= 1000.0;
        if(isolatedValues.empty() && enoughTimeForFeasibilityPump)try {
            const auto fpStart = std::chrono::steady_clock::now();
            auto fpHits = FeasibilityPump{tol_, 10, lpIterationLimit_, 250.0, 6}
                .solve(original, method, &initialRelaxation, deadline);
            out.feasibilityPumpTimeMs += elapsedMs(fpStart);
            out.feasibilityPumpIterations += fpHits.iterations;
            out.feasibilityPumpLPSolves += fpHits.lpSolves;
            out.totalLPIterations += fpHits.lpIterations;
            if (fpHits.status == FpStatus::Feasible && fpHits.verified &&
                (!out.incumbentFound || (min ? fpHits.objective < incumbent - tol_ : fpHits.objective > incumbent + tol_))) {
                if(!out.incumbentFound){out.firstIncumbentNode=0;out.firstIncumbentTimeMs=elapsedMs(solveStart);}
                incumbent = fpHits.objective;
                out.solution = fpHits.solution;
                out.incumbentFound = true;
                out.objective = incumbent;
                ++out.incumbentUpdates;
            }
        } catch (...) {}
        if (timeLimitReached()) {
            out.status = out.incumbentFound ? MILPStatus::TimeLimit : MILPStatus::TimeLimitNoIncumbent;
            out.message = out.incumbentFound ? "MILP time limit reached after the root heuristic; verified incumbent retained"
                                              : "MILP time limit reached after the root LP; no incumbent found";
            out.dualBound = initialRelaxation.bound;
            if (out.incumbentFound) {
                out.objective = incumbent; out.primalBound = incumbent;
                out.absoluteGap = std::abs(out.primalBound - out.dualBound);
                out.relativeGap = out.absoluteGap / std::max(1.0, std::abs(out.primalBound));
                const auto verificationStart = std::chrono::steady_clock::now();
                out.verified = verify(original, out.solution, tol_);
                out.verificationTimeMs += elapsedMs(verificationStart);
            }
            return out;
        }

        // Pseudo-cost history for intelligent strong variable selection
        std::map<std::size_t, double> pseudoCostDown;
        std::map<std::size_t, double> pseudoCostUp;
        std::map<std::size_t, int> countBranchDown;
        std::map<std::size_t, int> countBranchUp;

        // Initialize queue of open nodes
        using OpenNodes = std::priority_queue<std::shared_ptr<BranchNode>,
            std::vector<std::shared_ptr<BranchNode>>, BestBoundFirst>;
        OpenNodes open(BestBoundFirst{min});
        auto root = std::make_shared<BranchNode>();
        root->nodeId = 0;
        root->depth = 0;
        root->bound = initialRelaxation.bound;
        root->relaxation = std::move(initialRelaxation);
        open.push(root);
        out.nodesCreated = 1;
        out.peakOpenNodes = open.size();
        const auto branchAndBoundStart = std::chrono::steady_clock::now();

        while (!open.empty() && (limit_ == 0 || out.nodesProcessed < limit_) && !timeLimitReached()) {
            // Extract the best-bound node in O(log n); node ids provide
            // deterministic ordering when bounds tie.
            const auto selectionStart = std::chrono::steady_clock::now();
            auto nodePtr=open.top();
            open.pop();
            BranchNode node=std::move(*nodePtr);
            out.nodeSelectionTimeMs += elapsedMs(selectionStart);

            // A queued child inherits a valid parent LP bound. Its feasible
            // region is a subset of the parent's, so an incumbent may prune
            // it before materializing its model or launching another LP.
            if(out.incumbentFound && ((min && node.bound >= incumbent - tol_) || (!min && node.bound <= incumbent + tol_))) {
                ++out.nodesPruned;
                ++out.nodesPrunedBeforeLP;
                ++out.nodeLPSolvesAvoidedByBound;
                continue;
            }

            // Solve node relaxation if not pre-computed
            LPRelaxationResult lr = node.relaxation;
            const auto nodeModelStart = std::chrono::steady_clock::now();
            Model nodeModel=materializeNodeModel(node);
            out.nodeModelUpdateTimeMs += elapsedMs(nodeModelStart);
            if (lr.status != LPStatus::Optimal) {
                const auto nodeLPStart = std::chrono::steady_clock::now();
                const LPWarmStartState* warmState=useWarmStarts&&node.warmStartState?node.warmStartState.get():nullptr;
                lr = solveLPRelaxation(original, nodeModel, method, lpIterationLimit_, deadline, warmState, useWarmStarts, preparedRootLPPtr);
                out.nodeLPTimeMs += elapsedMs(nodeLPStart);
                if(lr.status==LPStatus::Optimal)restoreIsolated(lr);
                ++out.lpSolves;
                out.totalLPIterations += lr.iterations;
                out.nodeLPIterations += lr.iterations;
                out.nodeLPStandardizationTimeMs += lr.standardizationTimeMs;
                out.nodeLPDenseSetupTimeMs+=lr.denseSetupTimeMs;out.nodeLPDensePricingTimeMs+=lr.densePricingTimeMs;
                out.nodeLPDenseRatioTestTimeMs+=lr.denseRatioTestTimeMs;out.nodeLPDensePivotTimeMs+=lr.densePivotTimeMs;
                out.nodeLPDenseWarmBasisRebuildTimeMs+=lr.denseWarmBasisRebuildTimeMs;
                out.nodeLPDenseSolutionRecoveryTimeMs+=lr.denseSolutionRecoveryTimeMs;
                out.nodeLPDenseVerificationTimeMs+=lr.denseVerificationTimeMs;out.nodeLPDenseCutTimeMs+=lr.denseCutTimeMs;
                out.nodeLPSparseFactorizationTimeMs += lr.sparseFactorizationTimeMs;
                if(lr.warmStartAttempted) {
                    ++out.warmStartsAttempted;
                    if(lr.warmStartAccepted)++out.warmStartsSuccessful;
                    else {++out.warmStartsFailed;++out.coldFallbacks;++out.coldStarts;}
                } else ++out.coldStarts;
                node.relaxation = lr;
                node.bound = lr.bound;
            }
            ++out.nodesProcessed;
            out.maxDepth = std::max(out.maxDepth, node.depth);
            if (node.branchVariable < original.variables.size() &&
                node.branchFractionalDistance > tol_ && lr.status == LPStatus::Optimal) {
                const double degradation = min ? lr.bound - node.parentBound : node.parentBound - lr.bound;
                const double observed = std::max(0.0, degradation) / node.branchFractionalDistance;
                if (node.branchDown) { pseudoCostDown[node.branchVariable] += observed; ++countBranchDown[node.branchVariable]; }
                else { pseudoCostUp[node.branchVariable] += observed; ++countBranchUp[node.branchVariable]; }
            }

            // Infeasibility pruning
            if (lr.status == LPStatus::Infeasible) {
                const auto pruningStart = std::chrono::steady_clock::now();
                ++out.nodesPruned;
                out.pruningTimeMs += elapsedMs(pruningStart);
                continue;
            }
            if (lr.status == LPStatus::Unbounded) {
                out.status = MILPStatus::Unbounded;
                out.message = "Subtree LP relaxation is unbounded";
                out.branchAndBoundTimeMs += elapsedMs(branchAndBoundStart);
                return out;
            }
            if (lr.status != LPStatus::Optimal) {
                const bool timedOut = timeLimitReached();
                out.status = timedOut ? (out.incumbentFound ? MILPStatus::TimeLimit : MILPStatus::TimeLimitNoIncumbent)
                    : lr.status == LPStatus::IterationLimit ? MILPStatus::IterationLimit : MILPStatus::NumericalFailure;
                out.message = timedOut ? (out.incumbentFound ? "MILP time limit reached; verified incumbent retained"
                                                              : "MILP time limit reached before finding an incumbent")
                    : lr.status == LPStatus::IterationLimit
                    ? "Node LP relaxation reached the configured per-node LP iteration limit"
                    : "Node LP relaxation failed: " + lr.message;
                if (lr.status == LPStatus::IterationLimit && out.incumbentFound) {
                    out.objective = incumbent;
                    out.primalBound = incumbent;
                    out.dualBound = node.bound;
                    if(!open.empty())out.dualBound=min?std::min(out.dualBound,open.top()->bound):std::max(out.dualBound,open.top()->bound);
                    out.hasDualBound = true;
                    out.absoluteGap = std::abs(out.primalBound - out.dualBound);
                    out.relativeGap = out.absoluteGap / std::max(1.0, std::abs(out.primalBound));
                    const auto verificationStart = std::chrono::steady_clock::now();
                    out.verified = verify(original, out.solution, tol_);
                    out.verificationTimeMs += elapsedMs(verificationStart);
                }
                if (timedOut && !out.incumbentFound) {
                    out.dualBound = node.bound;
                    if(!open.empty())out.dualBound=min?std::min(out.dualBound,open.top()->bound):std::max(out.dualBound,open.top()->bound);
                    out.hasDualBound = true;
                }
                out.branchAndBoundTimeMs += elapsedMs(branchAndBoundStart);
                return out;
            }

            // Bound pruning
            if (out.incumbentFound && ((min && lr.bound >= incumbent - tol_) || (!min && lr.bound <= incumbent + tol_))) {
                const auto pruningStart = std::chrono::steady_clock::now();
                ++out.nodesPruned;
                out.pruningTimeMs += elapsedMs(pruningStart);
                continue;
            }

            // Check integrality
            const auto integralityStart = std::chrono::steady_clock::now();
            bool integerFeasible = true;
            std::vector<std::size_t> fractionalCandidates;
            for (const auto& v : original.variables) {
                if (v.active && (v.type == VariableType::Integer || v.type == VariableType::Binary) && v.originalId < lr.solution.size()) {
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
                    if(!out.incumbentFound){out.firstIncumbentNode=node.nodeId;out.firstIncumbentTimeMs=elapsedMs(solveStart);}
                    out.incumbentFound = true;
                    ++out.incumbentUpdates;
                }
                ++out.nodesPruned;
                out.pruningTimeMs += elapsedMs(integralityStart);
                continue;
            }
            out.pruningTimeMs += elapsedMs(integralityStart);

            // Variable Selection: Pseudo-cost & Strong Branching Score
            const auto branchingStart = std::chrono::steady_clock::now();
            std::size_t branchVar = fractionalCandidates.front();
            double bestScore = -1.0;

            for (std::size_t varId : fractionalCandidates) {
                double val = lr.solution[varId];
                double f = val - std::floor(val);
                double fDown = f;
                double fUp = 1.0 - f;

                // Treat fewer than two observations as unreliable and retain
                // fractional branching until each direction has history.
                double pcDown = countBranchDown[varId] >= 1 ? (pseudoCostDown[varId] / countBranchDown[varId]) : 1.0;
                double pcUp = countBranchUp[varId] >= 1 ? (pseudoCostUp[varId] / countBranchUp[varId]) : 1.0;

                double scoreDown = std::max(pcDown * fDown, 1e-6);
                double scoreUp = std::max(pcUp * fUp, 1e-6);
                double score = scoreDown * scoreUp;

                // Deterministic fractional-violation tie-break.
                const double violation = 0.5 - std::abs(f - 0.5);
                score += violation * 0.1;

                if (score > bestScore) {
                    bestScore = score;
                    branchVar = varId;
                }
            }
            out.branchingTimeMs += elapsedMs(branchingStart);

            // Branching: Create Left (x <= floor) and Right (x >= ceil) branches
            double branchVal = lr.solution[branchVar];
            double lo = std::floor(branchVal);
            double hi = std::ceil(branchVal);

            const auto modelUpdateStart = std::chrono::steady_clock::now();
            const double parentLower=nodeModel.variables[branchVar].lower;
            const double parentUpper=nodeModel.variables[branchVar].upper;
            const double leftUpper=std::min(parentUpper,lo);
            const double rightLower=std::max(parentLower,hi);

            // Left child
            if (parentLower <= leftUpper + tol_) {
                auto child=std::make_shared<BranchNode>();
                child->nodeId = out.nodesCreated++;
                child->parentId = node.nodeId;
                child->depth = node.depth + 1;
                child->bounds = std::make_shared<BranchNode::BoundChange>(BranchNode::BoundChange{node.bounds,branchVar,false,leftUpper});
                child->branchVariable = branchVar;
                child->warmStartState = lr.warmStartState;
                child->branchDown = true;
                child->branchFractionalDistance = branchVal - lo;
                child->parentBound = node.bound;
                child->bound = node.bound; // Inherit parent lower/upper bound
                open.push(std::move(child));
            } else {
                ++out.nodesPruned;
            }

            // Right child
            if (rightLower <= parentUpper + tol_) {
                auto child=std::make_shared<BranchNode>();
                child->nodeId = out.nodesCreated++;
                child->parentId = node.nodeId;
                child->depth = node.depth + 1;
                child->bounds = std::make_shared<BranchNode::BoundChange>(BranchNode::BoundChange{node.bounds,branchVar,true,rightLower});
                child->branchVariable = branchVar;
                child->warmStartState = lr.warmStartState;
                child->branchDown = false;
                child->branchFractionalDistance = hi - branchVal;
                child->parentBound = node.bound;
                child->bound = node.bound; // Inherit parent lower/upper bound
                open.push(std::move(child));
            } else {
                ++out.nodesPruned;
            }
            out.nodeModelUpdateTimeMs += elapsedMs(modelUpdateStart);
            out.peakOpenNodes = std::max(out.peakOpenNodes, open.size());
        }
        out.branchAndBoundTimeMs += elapsedMs(branchAndBoundStart);

        const bool nodeLimitReached = !open.empty() && limit_ != 0 && out.nodesProcessed >= limit_;
        const bool wallTimeLimitReached = !open.empty() && timeLimitReached();
        if (!out.incumbentFound) {
            out.status = wallTimeLimitReached ? MILPStatus::TimeLimitNoIncumbent
                : nodeLimitReached ? MILPStatus::NodeLimit : MILPStatus::Infeasible;
            out.message = wallTimeLimitReached ? "MILP time limit reached before finding an integer-feasible solution"
                : nodeLimitReached ? "node limit reached before finding an integer-feasible solution" : "no integer-feasible solution found";
            if((nodeLimitReached || wallTimeLimitReached)&&!open.empty()){
                out.dualBound=open.top()->bound;
                out.hasDualBound=true;
            }
            return out;
        }

        out.status = open.empty() ? MILPStatus::Optimal : wallTimeLimitReached ? MILPStatus::TimeLimit : MILPStatus::NodeLimit;
        if (wallTimeLimitReached) out.message = "MILP time limit reached; verified incumbent retained";
        else if (nodeLimitReached) out.message = "configured branch-and-bound node limit reached";
        out.primalBound = incumbent;
        out.dualBound = incumbent;
        out.hasDualBound = true;
        if (!open.empty()) {
            out.dualBound = open.top()->bound;
        }
        out.absoluteGap = std::abs(out.primalBound - out.dualBound);
        out.relativeGap = out.absoluteGap / std::max(1.0, std::abs(out.primalBound));
        const auto verificationStart = std::chrono::steady_clock::now();
        out.verified = verify(original, out.solution, tol_);
        out.verificationTimeMs += elapsedMs(verificationStart);
        return out;
    }

    static double elapsedMs(const std::chrono::steady_clock::time_point& start) {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }

    double tol_;
    std::size_t limit_;
    std::size_t lpIterationLimit_;
    double timeLimitMs_;
    bool enableWarmStarts_;

    static bool verify(const Model& m, const std::vector<double>& x, double t) {
        for (const auto& v : m.variables) {
            if (!v.active) continue;
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
            if (!c.active) continue;
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
