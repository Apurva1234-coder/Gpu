#pragma once
#include "core/Tolerance.hpp"
#include "presolve/PresolveResult.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <unordered_set>
namespace sovereign {
class Presolver {
public:
    explicit Presolver(Tolerance tolerance = {}, std::size_t maxPasses = 10, double timeBudgetMs = 0.0, bool adaptive = false)
        : tol_(tolerance), maxPasses_(maxPasses), timeBudgetMs_(timeBudgetMs), adaptive_(adaptive) {}
    PresolveResult run(Model model, bool preserveIntegerSemantics = false) const {
        const auto runStart = std::chrono::steady_clock::now();
        PresolveResult result{std::move(model)};
        bool changed = false;
        PresolvePassStats previousPass;
        double bestReduction = 0.0;
        double bestReductionPerMs = 0.0;
        for (std::size_t pass = 1; pass <= maxPasses_; ++pass) {
            if (pass > 1 && timeBudgetMs_ > 0.0 &&
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - runStart).count() >= timeBudgetMs_) {
                result.terminationReason = "time_budget";
                break;
            }
            const auto passStart = std::chrono::steady_clock::now();
            PresolvePassStats passStats;
            passStats.pass = pass;
            if (pass == 1) {
                passStats.variablesBefore = activeVariableCount(result.model);
                passStats.constraintsBefore = activeConstraintCount(result.model);
                passStats.nonzerosBefore = activeNonzeroCount(result.model);
            } else {
                passStats.variablesBefore = previousPass.variablesAfter;
                passStats.constraintsBefore = previousPass.constraintsAfter;
                passStats.nonzerosBefore = previousPass.nonzerosAfter;
            }
            const auto before = result.stats;
            std::size_t nonzerosRemoved = 0;
            result.stats.passes = pass; changed = false;
            if (tighten(result.model, result.stats, result.history, preserveIntegerSemantics)) changed = true;
            const bool foundContradiction = contradiction(result.model, preserveIntegerSemantics);
            if (!foundContradiction) {
                if (substitute(result.model, result.stats, result.history, nonzerosRemoved, preserveIntegerSemantics)) changed = true;
                if (fixed(result.model, result.stats, result.history, nonzerosRemoved)) changed = true;
                if (removeRedundant(result.model, result.stats, result.history)) changed = true;
                if (singleton(result.model, result.stats, nonzerosRemoved)) changed = true;
            }
            passStats.boundTightenings = result.stats.boundTightenings - before.boundTightenings;
            passStats.fixedVariables = result.stats.fixedVariables - before.fixedVariables;
            passStats.substitutions = result.stats.substitutions - before.substitutions;
            passStats.singletonReductions = result.stats.singletonReductions - before.singletonReductions;
            passStats.redundantRowsRemoved = result.stats.redundantRows - before.redundantRows;
            passStats.variablesAfter = passStats.variablesBefore - std::min(passStats.variablesBefore, passStats.fixedVariables + passStats.substitutions);
            passStats.constraintsAfter = passStats.constraintsBefore - std::min(passStats.constraintsBefore, passStats.substitutions + passStats.singletonReductions + passStats.redundantRowsRemoved);
            passStats.nonzerosAfter = passStats.nonzerosBefore - std::min(passStats.nonzerosBefore, nonzerosRemoved);
            const std::size_t sizeBefore = passStats.variablesBefore + passStats.constraintsBefore + passStats.nonzerosBefore;
            const std::size_t sizeAfter = passStats.variablesAfter + passStats.constraintsAfter + passStats.nonzerosAfter;
            passStats.percentageReduction = sizeBefore ? 100.0 * static_cast<double>(sizeBefore - std::min(sizeBefore, sizeAfter)) / static_cast<double>(sizeBefore) : 0.0;
            passStats.timeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - passStart).count();
            result.passStats.push_back(passStats);
            previousPass = passStats;
            if (foundContradiction) { result.status = PresolveStatus::Infeasible; result.terminationReason = "infeasible"; return result; }
            if (!changed) { result.terminationReason = "no_reductions"; break; }
            const std::size_t passWork = passStats.variablesBefore + passStats.constraintsBefore + passStats.nonzerosBefore;
            const double worthwhileThreshold = std::max(0.05, bestReduction * 0.025);
            const double reductionPerMs = passStats.percentageReduction / std::max(passStats.timeMs, 1e-9);
            const bool diminishingBenefit = passStats.percentageReduction < worthwhileThreshold;
            const bool diminishingReturnPerCost = bestReductionPerMs > 0.0 && reductionPerMs < bestReductionPerMs * 0.025;
            if (adaptive_ && pass >= 2 && passWork >= adaptiveWorkThreshold_ && (diminishingBenefit || diminishingReturnPerCost)) {
                result.terminationReason = "diminishing_returns";
                break;
            }
            bestReduction = std::max(bestReduction, passStats.percentageReduction);
            bestReductionPerMs = std::max(bestReductionPerMs, reductionPerMs);
            if (pass == maxPasses_) result.terminationReason = "max_passes";
        }
        if (result.terminationReason.empty()) result.terminationReason = "max_passes";
        if (infeasibleRows(result.model)) result.status = PresolveStatus::Infeasible;
        else if (unbounded(result.model)) result.status = PresolveStatus::Unbounded;
        else if (result.stats.boundTightenings || result.stats.fixedVariables || result.stats.redundantRows || result.stats.singletonReductions) result.status = PresolveStatus::Reduced;
        return result;
    }
private:
    Tolerance tol_; std::size_t maxPasses_; double timeBudgetMs_; bool adaptive_; static constexpr std::size_t adaptiveWorkThreshold_ = 2000;
    static double value(const std::unordered_map<std::size_t,double>& a, std::size_t i) { auto it=a.find(i); return it==a.end()?0.0:it->second; }
    bool tighten(Model& m, PresolveStats& s, ReductionHistory& h, bool preserveIntegerSemantics) const {
        bool changed=false;
        for (const auto& row:m.constraints) if (row.active && row.coefficients.size()==1) {
            auto entry=row.coefficients.begin(); std::size_t i=entry->first; double a=entry->second; if (std::abs(a)<=tol_.zero) continue; double x=row.rhs/a;
            double oldL=m.variables[i].lower, oldU=m.variables[i].upper, l=oldL,u=oldU;
            if(row.relation==Relation::Equal) l=std::max(l,x),u=std::min(u,x);
            else if((row.relation==Relation::LessEqual && a>0)||(row.relation==Relation::GreaterEqual && a<0)) u=std::min(u,x);
            else l=std::max(l,x);
            const auto type=m.variables[i].type;
            const bool integral=preserveIntegerSemantics&&(type==VariableType::Integer||type==VariableType::Binary);
            if(integral){
                if(row.relation==Relation::Equal){l=std::max(oldL,std::ceil(x-tol_.feasibility));u=std::min(oldU,std::floor(x+tol_.feasibility));}
                else if((row.relation==Relation::LessEqual && a>0)||(row.relation==Relation::GreaterEqual && a<0))u=std::min(oldU,std::floor(x+tol_.feasibility));
                else l=std::max(oldL,std::ceil(x-tol_.feasibility));
                if(type==VariableType::Binary){l=std::max(l,0.0);u=std::min(u,1.0);}
            }
            if(l>oldL+tol_.bound||u<oldU-tol_.bound){ if(l>oldL+tol_.bound)m.variables[i].lower=l; if(u<oldU-tol_.bound)m.variables[i].upper=u; h.bounds.push_back({m.variables[i].originalId,oldL,oldU,m.variables[i].lower,m.variables[i].upper}); ++s.boundTightenings;changed=true; }
        } return changed;
    }
    bool contradiction(const Model& m, bool preserveIntegerSemantics) const {
        for(const auto& v:m.variables) if(v.active){
            if(v.lower>v.upper+tol_.feasibility)return true;
            if(preserveIntegerSemantics&&(v.type==VariableType::Integer||v.type==VariableType::Binary)){
                double lower=v.lower,upper=v.upper;
                if(v.type==VariableType::Binary){lower=std::max(lower,0.0);upper=std::min(upper,1.0);}
                if(std::isfinite(lower))lower=std::ceil(lower-tol_.feasibility);
                if(std::isfinite(upper))upper=std::floor(upper+tol_.feasibility);
                if(lower>upper)return true;
            }
        }
        return infeasibleRows(m);
    }
    bool infeasibleRows(const Model& m) const { for(const auto& r:m.constraints) if(r.active && r.coefficients.empty() && !satisfies(0,r.relation,r.rhs)) return true; return false; }
    bool satisfies(double x, Relation r, double b) const { return r==Relation::Equal?std::abs(x-b)<=tol_.feasibility:r==Relation::LessEqual?x<=b+tol_.feasibility:x>=b-tol_.feasibility; }
    bool fixed(Model& m, PresolveStats& s, ReductionHistory& h, std::size_t& nonzerosRemoved) const {
        std::vector<std::size_t> gone; for(std::size_t i=0;i<m.variables.size();++i) if(m.variables[i].active&&std::isfinite(m.variables[i].lower)&&std::isfinite(m.variables[i].upper)&&tol_.equal(m.variables[i].lower,m.variables[i].upper)) gone.push_back(i);
        if(gone.empty()) return false;
        std::vector<std::vector<std::size_t>> variableRows(m.variables.size());
        for(std::size_t row=0;row<m.constraints.size();++row) if(m.constraints[row].active)
            for(const auto& coefficient:m.constraints[row].coefficients)
                if(coefficient.first<variableRows.size()) variableRows[coefficient.first].push_back(row);
        for(auto i:gone){double x=m.variables[i].lower;h.fixed.push_back({m.variables[i].originalId,x});m.objectiveConstant+=value(m.objective,i)*x+value(m.quadratic,i)*x*x;m.objective.erase(i);m.quadratic.erase(i);for(auto row:variableRows[i]){auto& r=m.constraints[row];if(!r.active)continue;auto coefficient=r.coefficients.find(i);if(coefficient!=r.coefficients.end()){r.rhs-=coefficient->second*x;r.coefficients.erase(coefficient);++nonzerosRemoved;}}m.variables[i].active=false;++s.fixedVariables;++s.eliminatedVariables;}m.rebuildMappings();
        return true;
    }
    bool substitute(Model& m, PresolveStats& s, ReductionHistory& h, std::size_t& nonzerosRemoved, bool preserveIntegerSemantics) const {
        for (auto& row : m.constraints) {
            if (!row.active || row.relation != Relation::Equal || row.coefficients.size() != 2) continue;
            auto a = row.coefficients.begin(); auto b = std::next(a);
            if (a->first > b->first) std::swap(a, b);
            std::size_t eliminated = a->first, retained = b->first;
            double ca = a->second, cb = b->second;
            if (std::abs(ca) <= tol_.zero || !m.variables[eliminated].active || !m.variables[retained].active) continue;
            // Eliminating an integer variable can turn its integrality condition
            // into a hidden congruence constraint. Keep such columns intact.
            if(preserveIntegerSemantics&&m.variables[eliminated].type!=VariableType::Continuous)continue;
            double constant = row.rhs / ca, multiplier = -cb / ca;
            if (m.variables[eliminated].lower != 0 || std::isfinite(m.variables[eliminated].upper)) continue;
            for (auto& r : m.constraints) if (r.active && r.originalId != row.originalId) {
                auto it = r.coefficients.find(eliminated);
                if (it != r.coefficients.end()) { double q = it->second; const bool retainedExisted=r.coefficients.find(retained)!=r.coefficients.end(); r.rhs -= q * constant; r.coefficients[retained] += q * multiplier; r.coefficients.erase(it); nonzerosRemoved+=retainedExisted?1:0; }
            }
            double c = value(m.objective, eliminated); if (c != 0) { m.objectiveConstant += c * constant; m.objective[retained] += c * multiplier; }
            m.objective.erase(eliminated); m.variables[eliminated].active = false; row.active = false; nonzerosRemoved+=row.coefficients.size();
            h.substitutions.push_back({eliminated, retained, constant, multiplier}); ++s.substitutions; m.rebuildMappings(); return true;
        }
        return false;
    }
    bool removeRedundant(Model& m, PresolveStats& s, ReductionHistory& h) const {
        std::vector<Constraint> keep; bool changed=false;
        for(auto& r:m.constraints){if(!r.active){keep.push_back(std::move(r));continue;}if(r.coefficients.empty()&&satisfies(0,r.relation,r.rhs)){h.removedConstraints.push_back({r.originalId,r.name,r.relation,r.rhs,r.coefficients});++s.redundantRows;changed=true;continue;}keep.push_back(std::move(r));}m.constraints=std::move(keep);m.rebuildMappings();return changed;
    }
    bool singleton(Model& m, PresolveStats& s, std::size_t& nonzerosRemoved) const {
        bool changed=false; std::vector<Constraint> keep; keep.reserve(m.constraints.size());
        for(auto& row:m.constraints) {
            if(!row.active) { keep.push_back(row); continue; }
            if(row.coefficients.size()!=1) { keep.push_back(row); continue; }
            const auto entry=*row.coefficients.begin(); const auto index=entry.first; const double coefficient=entry.second;
            const auto& variable=m.variables[index];
            const double minActivity=std::abs(coefficient)<=tol_.zero?0.0:(coefficient>0?coefficient*variable.lower:coefficient*variable.upper);
            const double maxActivity=std::abs(coefficient)<=tol_.zero?0.0:(coefficient>0?coefficient*variable.upper:coefficient*variable.lower);
            bool implied=false;
            if(row.relation==Relation::LessEqual) implied=maxActivity<=row.rhs+tol_.feasibility;
            else if(row.relation==Relation::GreaterEqual) implied=minActivity>=row.rhs-tol_.feasibility;
            else implied=std::isfinite(minActivity)&&std::isfinite(maxActivity)&&
                         std::abs(minActivity-row.rhs)<=tol_.feasibility&&
                         std::abs(maxActivity-row.rhs)<=tol_.feasibility;
            if(implied) { ++s.singletonReductions; nonzerosRemoved+=row.coefficients.size(); changed=true; }
            else keep.push_back(row);
        }
        if(changed) { m.constraints=std::move(keep); m.rebuildMappings(); }
        return changed;
    }
    bool unbounded(const Model& m) const { for(auto it=m.objective.begin();it!=m.objective.end();++it){std::size_t i=it->first;double c=it->second;double direction=m.sense==Sense::Minimize?-c:c; if(direction>tol_.zero&&std::isinf(m.variables[i].upper)&&freeInRows(m,i)) return true;} return false; }
    bool freeInRows(const Model& m,std::size_t i) const { for(const auto& r:m.constraints) if(r.active&&std::abs(value(r.coefficients,i))>tol_.zero)return false;return true; }
};
}
