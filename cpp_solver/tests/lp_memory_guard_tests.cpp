#include "lp/DualSimplex.hpp"
#include "lp/MehrotraIPM.hpp"
#include "lp/LPSolver.hpp"
#include <cassert>
#include <stdexcept>

using namespace sovereign;

static Model largeSparseModel() {
    Model model;
    constexpr std::size_t dimension = 4000;
    model.variables.reserve(dimension);
    model.constraints.reserve(dimension);
    for (std::size_t i = 0; i < dimension; ++i)
        model.variables.push_back({i, i, "x", VariableType::Continuous, 0.0, INF, true});
    for (std::size_t i = 0; i < dimension; ++i) {
        Constraint row;
        row.originalId = i;
        row.coefficients.emplace(i, 1.0);
        row.relation = Relation::LessEqual;
        row.rhs = 1.0;
        model.constraints.push_back(std::move(row));
    }
    return model;
}

int main() {
    const Model model = largeSparseModel();
    assert(estimateDenseLPBytes(model) > DenseLPMemoryBudgetBytes);

    const auto revised = LPSolver{}.solve(model, LPMethod::RevisedSimplex, 1);
    assert(revised.denseMemoryGuardTriggered);
    assert(revised.message.find("sparse revised simplex selected") != std::string::npos);
    assert(revised.estimatedDenseMemoryBytes > DenseLPMemoryBudgetBytes);

    const auto dual = DualSimplex{}.solve(model, 1);
    assert(dual.status == LPStatus::Unsupported);
    assert(dual.denseMemoryGuardTriggered);
    assert(dual.message.find("safety budget") != std::string::npos);

    const auto ipm = MehrotraIPM{}.solve(model, 1);
    assert(ipm.status == LPStatus::Unsupported);
    assert(ipm.denseMemoryGuardTriggered);
    assert(ipm.message.find("safety budget") != std::string::npos);

    bool rejectedDenseStandardization = false;
    try {
        (void)standardize(model);
    } catch (const std::length_error& error) {
        rejectedDenseStandardization = std::string(error.what()).find("safety budget") != std::string::npos;
    }
    assert(rejectedDenseStandardization);
}
