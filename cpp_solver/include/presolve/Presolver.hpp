#pragma once
#include "core/Tolerance.hpp"
#include "presolve/PresolveResult.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
namespace sovereign {
class Presolver {
public:
    explicit Presolver(Tolerance tolerance = {}, std::size_t maxPasses = 10) : tol_(tolerance), maxPasses_(maxPasses) {}
    PresolveResult run(Model model) const {
        PresolveResult result{std::move(model)};
        bool changed = false;
        for (std::size_t pass = 1; pass <= maxPasses_; ++pass) {
            result.stats.passes = pass; changed = false;
            if (tighten(result.model, result.stats, result.history)) changed = true;
            if (contradiction(result.model)) { result.status = PresolveStatus::Infeasible; return result; }
            if (substitute(result.model, result.stats, result.history)) changed = true;
            if (fixed(result.model, result.stats, result.history)) changed = true;
            if (removeRedundant(result.model, result.stats, result.history)) changed = true;
            if (singleton(result.model, result.stats)) changed = true;
            if (!changed) break;
        }
        if (infeasibleRows(result.model)) result.status = PresolveStatus::Infeasible;
        else if (unbounded(result.model)) result.status = PresolveStatus::Unbounded;
        else if (result.stats.boundTightenings || result.stats.fixedVariables || result.stats.redundantRows || result.stats.singletonReductions) result.status = PresolveStatus::Reduced;
        return result;
    }
private:
    Tolerance tol_; std::size_t maxPasses_;
    static double value(const std::unordered_map<std::size_t,double>& a, std::size_t i) { auto it=a.find(i); return it==a.end()?0.0:it->second; }
    bool tighten(Model& m, PresolveStats& s, ReductionHistory& h) const {
        bool changed=false;
        for (const auto& row:m.constraints) if (row.coefficients.size()==1) {
            auto entry=row.coefficients.begin(); std::size_t i=entry->first; double a=entry->second; if (std::abs(a)<=tol_.zero) continue; double x=row.rhs/a;
            double oldL=m.variables[i].lower, oldU=m.variables[i].upper, l=oldL,u=oldU;
            if(row.relation==Relation::Equal) l=std::max(l,x),u=std::min(u,x);
            else if((row.relation==Relation::LessEqual && a>0)||(row.relation==Relation::GreaterEqual && a<0)) u=std::min(u,x);
            else l=std::max(l,x);
            if(l>oldL+tol_.bound||u<oldU-tol_.bound){ if(l>oldL+tol_.bound)m.variables[i].lower=l; if(u<oldU-tol_.bound)m.variables[i].upper=u; h.bounds.push_back({m.variables[i].originalId,oldL,oldU,m.variables[i].lower,m.variables[i].upper}); ++s.boundTightenings;changed=true; }
        } return changed;
    }
    bool contradiction(const Model& m) const { for(const auto& v:m.variables) if(v.lower>v.upper+tol_.feasibility) return true; return infeasibleRows(m); }
    bool infeasibleRows(const Model& m) const { for(const auto& r:m.constraints) if(r.coefficients.empty() && !satisfies(0,r.relation,r.rhs)) return true; return false; }
    bool satisfies(double x, Relation r, double b) const { return r==Relation::Equal?std::abs(x-b)<=tol_.feasibility:r==Relation::LessEqual?x<=b+tol_.feasibility:x>=b-tol_.feasibility; }
    bool fixed(Model& m, PresolveStats& s, ReductionHistory& h) const {
        std::vector<std::size_t> gone; for(std::size_t i=0;i<m.variables.size();++i) if(m.variables[i].active&&std::isfinite(m.variables[i].lower)&&std::isfinite(m.variables[i].upper)&&tol_.equal(m.variables[i].lower,m.variables[i].upper)) gone.push_back(i);
        if(gone.empty()) return false; for(auto i:gone){double x=m.variables[i].lower;h.fixed.push_back({m.variables[i].originalId,x});m.objectiveConstant+=value(m.objective,i)*x+value(m.quadratic,i)*x*x;m.objective.erase(i);m.quadratic.erase(i);for(auto& r:m.constraints){r.rhs-=value(r.coefficients,i)*x;r.coefficients.erase(i);}m.variables[i].active=false;++s.fixedVariables;++s.eliminatedVariables;}m.rebuildMappings();
        return true;
    }
    bool substitute(Model& m, PresolveStats& s, ReductionHistory& h) const {
        for (auto& row : m.constraints) {
            if (!row.active || row.relation != Relation::Equal || row.coefficients.size() != 2) continue;
            auto a = row.coefficients.begin(); auto b = std::next(a);
            if (a->first > b->first) std::swap(a, b);
            std::size_t eliminated = a->first, retained = b->first;
            double ca = a->second, cb = b->second;
            if (std::abs(ca) <= tol_.zero || !m.variables[eliminated].active || !m.variables[retained].active) continue;
            double constant = row.rhs / ca, multiplier = -cb / ca;
            if (m.variables[eliminated].lower != 0 || std::isfinite(m.variables[eliminated].upper)) continue;
            for (auto& r : m.constraints) if (r.active && r.originalId != row.originalId) {
                auto it = r.coefficients.find(eliminated);
                if (it != r.coefficients.end()) { double q = it->second; r.rhs -= q * constant; r.coefficients[retained] += q * multiplier; r.coefficients.erase(it); }
            }
            double c = value(m.objective, eliminated); if (c != 0) { m.objectiveConstant += c * constant; m.objective[retained] += c * multiplier; }
            m.objective.erase(eliminated); m.variables[eliminated].active = false; row.active = false;
            h.substitutions.push_back({eliminated, retained, constant, multiplier}); ++s.substitutions; m.rebuildMappings(); return true;
        }
        return false;
    }
    bool removeRedundant(Model& m, PresolveStats& s, ReductionHistory& h) const {
        std::vector<Constraint> keep; bool changed=false;
        for(auto& r:m.constraints){if(r.coefficients.empty()&&satisfies(0,r.relation,r.rhs)){h.removedConstraints.push_back({r.originalId,r.name,r.relation,r.rhs,r.coefficients});++s.redundantRows;changed=true;continue;}keep.push_back(std::move(r));}m.constraints=std::move(keep);m.rebuildMappings();return changed;
    }
    bool singleton(Model& m, PresolveStats& s) const { bool changed=false; for(const auto& r:m.constraints) if(r.coefficients.size()==1){++s.singletonReductions;changed=true;} return changed; }
    bool unbounded(const Model& m) const { for(auto it=m.objective.begin();it!=m.objective.end();++it){std::size_t i=it->first;double c=it->second;double direction=m.sense==Sense::Minimize?-c:c; if(direction>tol_.zero&&std::isinf(m.variables[i].upper)&&freeInRows(m,i)) return true;} return false; }
    bool freeInRows(const Model& m,std::size_t i) const { for(const auto& r:m.constraints) if(std::abs(value(r.coefficients,i))>tol_.zero)return false;return true; }
};
}
