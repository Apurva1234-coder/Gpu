#pragma once
#include "model/Classification.hpp"
#include "lp/LPSolver.hpp"
#include "lp/DualSimplex.hpp"
#include "lp/MehrotraIPM.hpp"
#include "lp/PDHGSolver.hpp"
#include "lp/Objective.hpp"
#include <chrono>
#include <cmath>
#include <string>
namespace sovereign {
struct LPRelaxationResult {
    LPStatus status{LPStatus::NumericalFailure};
    std::vector<double> solution;
    double objectiveValue=0,bound=0,primalResidual=0,standardizationTimeMs=0;
    double denseSetupTimeMs=0,densePricingTimeMs=0,denseRatioTestTimeMs=0,densePivotTimeMs=0;
    double denseWarmBasisRebuildTimeMs=0,denseSolutionRecoveryTimeMs=0,denseVerificationTimeMs=0,denseCutTimeMs=0;
    double sparsePricingTimeMs=0,sparseBasisSolveTimeMs=0,sparseFactorizationTimeMs=0;
    std::size_t iterations=0,sparseRefactorizations=0,sparsePivots=0;
    bool warmStartAttempted=false,warmStartAccepted=false;
    std::vector<std::size_t> basisVariables;
    std::shared_ptr<const LPWarmStartState> warmStartState;
    std::string method,boundType,message;
    bool fractionalSolution=false,integralWithinTolerance=false;
    std::size_t fractionalIntegerVariables=0;
};
inline Model relaxMILP(const Model& source){Model r=source;for(auto&v:r.variables){v.type=VariableType::Continuous;if(source.variables[v.originalId].type==VariableType::Binary){v.lower=std::max(0.0,v.lower);v.upper=std::min(1.0,v.upper);}}r.rebuildMappings();return r;}
inline bool isIntegralWithinTolerance(const Model& m,const std::vector<double>& x,double tol=1e-8){for(const auto&v:m.variables)if((v.type==VariableType::Integer||v.type==VariableType::Binary)&&v.originalId<x.size()&&std::abs(x[v.originalId]-std::round(x[v.originalId]))>tol)return false;return true;}
inline LPRelaxationResult solveLPRelaxation(
        const Model& milp, const Model& relaxedLP, LPMethod method=LPMethod::RevisedSimplex,
        std::size_t limit=10000, const std::chrono::steady_clock::time_point* deadline=nullptr,
        const LPWarmStartState* initialState=nullptr,bool retainWarmStartState=true,
        const StandardLP* preparedStandard=nullptr) {
    LPRelaxationResult r;
    if(deadline && std::chrono::steady_clock::now() >= *deadline) {
        r.status=LPStatus::IterationLimit; r.message="wall-clock time limit reached"; return r;
    }
    LPResult s;
    if(method==LPMethod::DualSimplex)s=DualSimplex{}.solve(relaxedLP,limit);
    else if(method==LPMethod::IPM)s=MehrotraIPM{}.solve(relaxedLP,limit);
    else if(method==LPMethod::PDHG)s=PDHGSolver{}.solve(relaxedLP,limit);
    else s=LPSolver{}.solve(relaxedLP,LPMethod::RevisedSimplex,limit,nullptr,deadline,initialState,retainWarmStartState,preparedStandard);
    r.status=s.status;r.method=s.method;r.iterations=s.iterations;r.message=s.message;r.solution=s.solution.primal;
    r.objectiveValue=evaluateObjective(milp,r.solution);r.bound=r.objectiveValue;
    r.boundType=milp.sense==Sense::Minimize?"LOWER_BOUND":"UPPER_BOUND";
    r.primalResidual=s.solution.feasibilityResidual;
    r.standardizationTimeMs=s.standardizationTimeMs;
    r.denseSetupTimeMs=s.denseSetupTimeMs;r.densePricingTimeMs=s.densePricingTimeMs;
    r.denseRatioTestTimeMs=s.denseRatioTestTimeMs;r.densePivotTimeMs=s.densePivotTimeMs;
    r.denseWarmBasisRebuildTimeMs=s.denseWarmBasisRebuildTimeMs;
    r.denseSolutionRecoveryTimeMs=s.denseSolutionRecoveryTimeMs;r.denseVerificationTimeMs=s.denseVerificationTimeMs;
    r.denseCutTimeMs=s.denseCutTimeMs;
    r.sparsePricingTimeMs=s.sparsePricingTimeMs;
    r.sparseBasisSolveTimeMs=s.sparseBasisSolveTimeMs;
    r.sparseFactorizationTimeMs=s.sparseFactorizationTimeMs;
    r.sparseRefactorizations=s.sparseRefactorizations;
    r.sparsePivots=s.sparsePivots;
    r.warmStartAttempted=s.warmStartAttempted;r.warmStartAccepted=s.warmStartAccepted;r.basisVariables=s.basisVariables;r.warmStartState=s.warmStartState;
    for(const auto&v:milp.variables)if((v.type==VariableType::Integer||v.type==VariableType::Binary)&&v.originalId<r.solution.size()&&std::abs(r.solution[v.originalId]-std::round(r.solution[v.originalId]))>1e-8)++r.fractionalIntegerVariables;
    r.fractionalSolution=r.fractionalIntegerVariables>0;r.integralWithinTolerance=!r.fractionalSolution;
    return r;
}
inline LPRelaxationResult solveLPRelaxation(
        const Model& milp, LPMethod method=LPMethod::RevisedSimplex, std::size_t limit=10000,
        const std::chrono::steady_clock::time_point* deadline=nullptr) {
    Model lp=relaxMILP(milp);
    return solveLPRelaxation(milp,lp,method,limit,deadline);
}
}
