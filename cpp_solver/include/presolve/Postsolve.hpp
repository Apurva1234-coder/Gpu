#pragma once
#include "presolve/PresolveResult.hpp"
#include "lp/Solution.hpp"
#include "lp/Objective.hpp"
namespace sovereign {
inline Solution postsolve(const Model& original,const PresolveResult& reduced,const Solution& reducedSolution){
    Solution out=reducedSolution;
    out.primal.assign(original.variables.size(),0.0);
    const bool fullIndexedVector=reducedSolution.primal.size()>=reduced.model.variables.size();
    for(const auto& v:original.variables){
        if(!v.active)continue;
        std::size_t source=v.originalId;
        if(!fullIndexedVector){
            auto it=reduced.model.originalToActive.find(v.originalId);
            source=it!=reduced.model.originalToActive.end()?it->second:v.activeId;
        }
        if(source<reducedSolution.primal.size())out.primal[v.originalId]=reducedSolution.primal[source];
    }
    for(auto r:reduced.history.fixed)out.primal[r.originalId]=r.value;
    for(auto it=reduced.history.substitutions.rbegin();it!=reduced.history.substitutions.rend();++it)
        out.primal[it->eliminatedId]=it->constant+it->multiplier*out.primal[it->retainedId];
    out.objectiveValue=evaluateObjective(original,out.primal);
    return out;
}
}
