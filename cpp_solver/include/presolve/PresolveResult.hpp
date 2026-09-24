#pragma once
#include "model/Model.hpp"
namespace sovereign {
struct FixedVariableRecord { std::size_t originalId; double value; };
struct BoundChangeRecord { std::size_t originalId; double oldLower, oldUpper, newLower, newUpper; };
struct RemovedConstraintRecord { std::size_t originalId; std::string name; Relation relation; double rhs; std::unordered_map<std::size_t,double> coefficients; };
struct SubstitutionRecord { std::size_t eliminatedId; std::size_t retainedId; double constant; double multiplier; };
struct ReductionHistory { std::vector<FixedVariableRecord> fixed; std::vector<BoundChangeRecord> bounds; std::vector<RemovedConstraintRecord> removedConstraints; std::vector<SubstitutionRecord> substitutions; };
enum class PresolveStatus { Unchanged, Reduced, Infeasible, Unbounded };
struct PresolveStats { std::size_t boundTightenings{}, fixedVariables{}, eliminatedVariables{}, redundantRows{}, aggregations{}, substitutions{}, singletonReductions{}, passes{}; };
struct PresolveResult { Model model; PresolveStatus status{PresolveStatus::Unchanged}; PresolveStats stats; ReductionHistory history; };
}
