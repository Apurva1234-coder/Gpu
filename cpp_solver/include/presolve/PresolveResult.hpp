#pragma once
#include "model/Model.hpp"
#include <cstddef>
#include <vector>
namespace sovereign {
struct FixedVariableRecord { std::size_t originalId; double value; };
struct BoundChangeRecord { std::size_t originalId; double oldLower, oldUpper, newLower, newUpper; };
struct RemovedConstraintRecord { std::size_t originalId; std::string name; Relation relation; double rhs; std::unordered_map<std::size_t,double> coefficients; };
struct SubstitutionRecord { std::size_t eliminatedId; std::size_t retainedId; double constant; double multiplier; };
struct ReductionHistory { std::vector<FixedVariableRecord> fixed; std::vector<BoundChangeRecord> bounds; std::vector<RemovedConstraintRecord> removedConstraints; std::vector<SubstitutionRecord> substitutions; };
enum class PresolveStatus { Unchanged, Reduced, Infeasible, Unbounded };
struct PresolveStats { std::size_t boundTightenings{}, fixedVariables{}, eliminatedVariables{}, redundantRows{}, aggregations{}, substitutions{}, singletonReductions{}, passes{}; };
struct PresolvePassStats {
    std::size_t pass{};
    double timeMs{};
    std::size_t variablesBefore{}, variablesAfter{};
    std::size_t constraintsBefore{}, constraintsAfter{};
    std::size_t nonzerosBefore{}, nonzerosAfter{};
    std::size_t boundTightenings{}, fixedVariables{}, substitutions{};
    std::size_t singletonReductions{}, redundantRowsRemoved{};
    double percentageReduction{};
};
struct PresolveResult { Model model; PresolveStatus status{PresolveStatus::Unchanged}; PresolveStats stats; ReductionHistory history; std::vector<PresolvePassStats> passStats; };
inline std::size_t activeVariableCount(const Model& model) {
    std::size_t count = 0;
    for (const auto& variable : model.variables) count += variable.active ? 1 : 0;
    return count;
}
inline std::size_t activeConstraintCount(const Model& model) {
    std::size_t count = 0;
    for (const auto& constraint : model.constraints) count += constraint.active ? 1 : 0;
    return count;
}
inline std::size_t activeNonzeroCount(const Model& model) {
    std::size_t count = 0;
    for (const auto& constraint : model.constraints)
        if (constraint.active) count += constraint.coefficients.size();
    return count;
}
}
