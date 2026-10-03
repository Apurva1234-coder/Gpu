#include "lp/PDHGSolver.hpp"
#include "lp/Verifier.hpp"
#include <cassert>
#include <cmath>

using namespace sovereign;

int main() {
    Model model;
    model.name = "pdhg_small_verified";
    model.sense = Sense::Minimize;
    model.variables = {
        {0, 0, "x", VariableType::Continuous, 0.0, INF, true},
        {1, 1, "y", VariableType::Continuous, 0.0, INF, true}
    };
    model.objective[0] = 1.0;
    model.objective[1] = 1.0;
    Constraint demand;
    demand.name = "demand";
    demand.coefficients[0] = 1.0;
    demand.coefficients[1] = 1.0;
    demand.relation = Relation::GreaterEqual;
    demand.rhs = 1.0;
    model.constraints.push_back(demand);

    const auto converged = PDHGSolver{}.solve(model, 100000);
    assert(converged.status == LPStatus::IterationLimit);
    assert(std::abs(converged.objectiveValue - 1.0) <= 1e-6);
    assert(converged.primalResidual > 1e-7);
    assert(verify(model, converged.solution).feasible);
    assert(converged.solution.feasibilityResidual <= 1e-8);

    const auto capped = PDHGSolver{}.solve(model, 1);
    assert(capped.status == LPStatus::IterationLimit);
    assert(capped.status != LPStatus::Optimal);
}
