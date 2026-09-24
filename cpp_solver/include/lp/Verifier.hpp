#pragma once
#include "lp/Objective.hpp"
#include "core/Tolerance.hpp"
#include <algorithm>
namespace sovereign {
struct Verification { bool feasible{true}; double residual{}, objectiveError{}; };
inline Verification verify(const Model& m,const Solution& s,const Tolerance& t={}) {
    Verification v;
    for(size_t i=0;i<m.variables.size();++i) if(i>=s.primal.size()||s.primal[i]<m.variables[i].lower-t.feasibility||s.primal[i]>m.variables[i].upper+t.feasibility){v.feasible=false;v.residual=1;}
    for(const auto& r:m.constraints){double a=0;for(auto e:r.coefficients)if(e.first<s.primal.size())a+=e.second*s.primal[e.first];double d=r.relation==Relation::Equal?std::abs(a-r.rhs):r.relation==Relation::LessEqual?std::max(0.0,a-r.rhs):std::max(0.0,r.rhs-a);v.residual=std::max(v.residual,d);if(d>t.feasibility)v.feasible=false;}
    v.objectiveError=std::abs(evaluateObjective(m,s.primal)-s.objectiveValue); return v;
}
}
