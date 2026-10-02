#pragma once
#include "model/Model.hpp"
#include <vector>
namespace sovereign {
struct Solution { std::vector<double> primal, dual, slacks, reducedCosts; double objectiveValue{}, feasibilityResidual{}, optimalityResidual{}; };
enum class LPStatus { Optimal, Infeasible, Unbounded, IterationLimit, NumericalFailure, Unsupported };
struct GomoryRow { std::size_t sourceVariable{}; std::vector<double> coefficients; double rhs{}; double violation{}; };
struct LPResult { LPStatus status{LPStatus::NumericalFailure}; double objectiveValue{}; std::size_t iterations{}; double feasibilityResidual{}, optimalityResidual{}; double standardizationTimeMs{}; double sparsePricingTimeMs{}, sparseBasisSolveTimeMs{}, sparseDevexTimeMs{}, sparseFactorizationTimeMs{}, sparseRatioTestTimeMs{}, sparseLexicographicTimeMs{}; std::size_t sparseRefactorizations{}, sparsePivots{}, sparseLexicographicSolves{}; bool sparseBlandFallbackTriggered{}; std::size_t standardizedRows{}, standardizedColumns{}, standardizedNonzeros{}; double estimatedDenseMemoryBytes{}; bool denseMemoryGuardTriggered{}; std::string method, message; Solution solution; std::vector<GomoryRow> gomoryRows; std::vector<std::size_t> basisVariables; std::vector<std::vector<double>> basisRows; };
}


