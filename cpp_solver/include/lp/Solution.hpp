#pragma once
#include "model/Model.hpp"
#include <vector>
#include <memory>
namespace sovereign {
struct Solution { std::vector<double> primal, dual, slacks, reducedCosts; double objectiveValue{}, feasibilityResidual{}, optimalityResidual{}; };
enum class LPStatus { Optimal, Infeasible, Unbounded, IterationLimit, NumericalFailure, Unsupported };
struct GomoryRow { std::size_t sourceVariable{}; std::vector<double> coefficients; double rhs{}; double violation{}; };
struct LPWarmStartState {
    std::vector<std::size_t> basisVariables;
    std::size_t structureHash{};
    // Persistent dense-simplex dictionary: retaining this avoids rebuilding
    // the basis factorization for an RHS-only branch child.
    std::vector<std::vector<double>> tableau;
    std::vector<unsigned char> basicVariables;
    std::vector<unsigned char> artificialVariables;
    std::vector<double> standardRhs;
    std::size_t modelStructureHash{};
    std::size_t originalColumnCount{};
    std::size_t rowCount{};
};
struct LPResult { LPStatus status{LPStatus::NumericalFailure}; double objectiveValue{}; std::size_t iterations{}; double feasibilityResidual{}, optimalityResidual{}; double primalResidual{}, dualResidual{}, complementarityResidual{}, pdhgPrimalWeight{}; double standardizationTimeMs{}; double denseSetupTimeMs{}, densePricingTimeMs{}, denseRatioTestTimeMs{}, densePivotTimeMs{}, denseWarmBasisRebuildTimeMs{}, denseSolutionRecoveryTimeMs{}, denseVerificationTimeMs{}, denseCutTimeMs{}; double sparsePricingTimeMs{}, sparseBasisSolveTimeMs{}, sparseDevexTimeMs{}, sparseFactorizationTimeMs{}, sparseRatioTestTimeMs{}, sparseLexicographicTimeMs{}; std::size_t sparseRefactorizations{}, sparsePivots{}, sparseLexicographicSolves{}; bool sparseBlandFallbackTriggered{}; bool warmStartAttempted{}, warmStartAccepted{}; std::size_t standardizedRows{}, standardizedColumns{}, standardizedNonzeros{}; double estimatedDenseMemoryBytes{}; bool denseMemoryGuardTriggered{}; std::string method, message; Solution solution; std::vector<GomoryRow> gomoryRows; std::vector<std::size_t> basisVariables; std::vector<std::vector<double>> basisRows; std::shared_ptr<const LPWarmStartState> warmStartState; };
}


