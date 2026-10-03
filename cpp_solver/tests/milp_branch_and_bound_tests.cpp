#include "milp/BranchAndBound.hpp"
#include "presolve/Presolver.hpp"
#include <cassert>

using namespace sovereign;

static Model fractionalBinaryModel() {
    Model model;
    model.name = "fractional binary";
    model.sense = Sense::Maximize;
    model.variables.push_back({0, 0, "x", VariableType::Binary, 0, 1, true});
    model.objective[0] = 1;
    Constraint row;
    row.originalId = 0;
    row.name = "cap";
    row.coefficients[0] = 1;
    row.relation = Relation::LessEqual;
    row.rhs = 0.5;
    model.constraints.push_back(row);
    model.rebuildMappings();
    return model;
}

int main() {
    const auto model = fractionalBinaryModel();
    const auto limited = BranchAndBound{1e-8, 1}.solve(model);
    assert(limited.status == MILPStatus::NodeLimit);
    assert(limited.nodesProcessed == 1);
    if (limited.incumbentFound) assert(limited.verified);

    const auto unlimited = BranchAndBound{1e-8, 0}.solve(model);
    const auto warm = BranchAndBound{1e-8, 0, 10000, 0.0, true}.solve(model);
    assert(unlimited.status == MILPStatus::Optimal);
    assert(unlimited.verified);
    assert(unlimited.objective == 0.0);
    assert(unlimited.rootLPTimeMs > 0.0);
    assert(unlimited.hasRootLPBound);
    assert(std::abs(unlimited.rootLPBound - 0.5) < 1e-8);
    assert(unlimited.rootLPIterations > 0);
    assert(unlimited.branchAndBoundTimeMs > 0.0);
    assert(unlimited.totalSolverTimeMs >= unlimited.rootLPTimeMs);
    assert(unlimited.warmStartsAttempted == 0 && unlimited.coldStarts == unlimited.lpSolves);
    assert(warm.status == MILPStatus::Optimal && warm.verified);
    assert(warm.objective == unlimited.objective);
    assert(warm.warmStartsAttempted > 0);
    assert(warm.warmStartsSuccessful + warm.warmStartsFailed == warm.warmStartsAttempted);
    assert(warm.coldStarts + warm.warmStartsSuccessful == warm.lpSolves);
    assert(unlimited.peakOpenNodes > 0);
    assert(unlimited.incumbentUpdates > 0);
    assert(unlimited.verificationTimeMs > 0.0);

    const auto timeLimited = BranchAndBound{1e-8, 100, 10000, 0.001}.solve(model);
    assert(timeLimited.status == MILPStatus::TimeLimitNoIncumbent);
    assert(!timeLimited.incumbentFound);
    assert(timeLimited.message.find("time limit") != std::string::npos);

    const auto lpIterationLimited = BranchAndBound{1e-8, 100, 1}.solve(model);
    assert(lpIterationLimited.status == MILPStatus::IterationLimit);
    assert(lpIterationLimited.totalLPIterations <= lpIterationLimited.lpSolves);

    Model largeSparseLP;
    largeSparseLP.name = "large sparse LP standardization";
    largeSparseLP.sense = Sense::Minimize;
    constexpr std::size_t sparseVariableCount = 5000;
    largeSparseLP.variables.reserve(sparseVariableCount);
    for (std::size_t i = 0; i < sparseVariableCount; ++i)
        largeSparseLP.variables.push_back({i, i, "x" + std::to_string(i), VariableType::Continuous, 0, 1, true});
    largeSparseLP.objective[0] = 1;
    Constraint equality;
    equality.originalId = 0;
    equality.name = "half";
    equality.coefficients[0] = 1;
    equality.relation = Relation::Equal;
    equality.rhs = 0.5;
    largeSparseLP.constraints.push_back(equality);
    largeSparseLP.rebuildMappings();
    const auto sparseRelaxation = LPSolver{}.solve(largeSparseLP, LPMethod::RevisedSimplex, 20);
    assert(sparseRelaxation.status == LPStatus::Optimal);
    assert(std::abs(sparseRelaxation.objectiveValue - 0.5) < 1e-8);
    assert(sparseRelaxation.solution.feasibilityResidual < 1e-8);

    Model sparse;
    sparse.name = "isolated variables";
    sparse.sense = Sense::Minimize;
    constexpr std::size_t variableCount = 25000;
    sparse.variables.reserve(variableCount);
    for (std::size_t i = 0; i < variableCount; ++i)
        sparse.variables.push_back({i, i, "x" + std::to_string(i), VariableType::Binary, 0, 1, true});
    sparse.objective[0] = -1;
    Constraint cap;
    cap.originalId = 0;
    cap.name = "cap";
    cap.coefficients[0] = 1;
    cap.relation = Relation::LessEqual;
    cap.rhs = 0.5;
    sparse.constraints.push_back(cap);
    sparse.rebuildMappings();
    const auto reduced = BranchAndBound{1e-8, 100}.solve(sparse);
    assert(reduced.status == MILPStatus::Optimal);
    assert(reduced.verified);
    assert(reduced.solution.size() == variableCount);
    assert(reduced.solution[0] == 0.0);
    assert(reduced.solution[variableCount - 1] == 0.0);

    Model presolveModel;
    presolveModel.name = "integer-aware MILP presolve and postsolve";
    presolveModel.sense = Sense::Minimize;
    presolveModel.variables = {{0, 0, "i", VariableType::Integer, 0, 10, true},
                               {1, 1, "y", VariableType::Continuous, 0, 10, true}};
    presolveModel.objective[0] = 3.0;
    presolveModel.objective[1] = 1.0;
    Constraint fixInteger; fixInteger.originalId=0; fixInteger.name="fix_i";
    fixInteger.coefficients[0]=1.0; fixInteger.relation=Relation::Equal; fixInteger.rhs=2.0;
    presolveModel.constraints.push_back(fixInteger);
    Constraint capacity; capacity.originalId=1; capacity.name="capacity";
    capacity.coefficients[0]=1.0; capacity.coefficients[1]=1.0;
    capacity.relation=Relation::GreaterEqual; capacity.rhs=3.0;
    presolveModel.constraints.push_back(capacity);
    presolveModel.rebuildMappings();
    auto presolved=Presolver{}.run(presolveModel,true);
    assert(presolved.stats.fixedVariables==1);
    assert(!presolved.model.variables[0].active);
    auto presolvedResult=BranchAndBound{1e-8,0}.solve(presolveModel,presolved);
    assert(presolvedResult.status==MILPStatus::Optimal&&presolvedResult.verified);
    assert(std::abs(presolvedResult.objective-7.0)<1e-8);
    assert(std::abs(presolvedResult.solution[0]-2.0)<1e-8);
    assert(std::abs(presolvedResult.solution[1]-1.0)<1e-8);

    Model presolveInfeasible;
    presolveInfeasible.variables.push_back({0,0,"i",VariableType::Integer,0,10,true});
    Constraint noIntegerPoint; noIntegerPoint.coefficients[0]=1.0;
    noIntegerPoint.relation=Relation::Equal; noIntegerPoint.rhs=1.5;
    presolveInfeasible.constraints.push_back(noIntegerPoint);
    auto infeasiblePresolve=Presolver{}.run(presolveInfeasible,true);
    assert(infeasiblePresolve.status==PresolveStatus::Infeasible);
    assert(BranchAndBound{}.solve(presolveInfeasible,infeasiblePresolve).status==MILPStatus::Infeasible);

    return 0;
}
