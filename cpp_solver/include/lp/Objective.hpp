#pragma once
#include "lp/Solution.hpp"
namespace sovereign {
inline double evaluateObjective(const Model& m, const std::vector<double>& x) {
    double value=m.objectiveConstant;
    for(auto e:m.objective) if(e.first<x.size()) value+=e.second*x[e.first];
    for(auto e:m.quadratic) if(e.first<x.size()) value+=e.second*x[e.first]*x[e.first];
    return value;
}
}
