#include "milp/BranchAndBound.hpp"
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
    assert(unlimited.status == MILPStatus::Optimal);
    assert(unlimited.verified);
    assert(unlimited.objective == 0.0);

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
    return 0;
}
