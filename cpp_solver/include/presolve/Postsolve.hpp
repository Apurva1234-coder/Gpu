#pragma once
#include "presolve/PresolveResult.hpp"
#include "lp/Solution.hpp"
#include "lp/Objective.hpp"
namespace sovereign { inline Solution postsolve(const Model& original,const PresolveResult& reduced,const Solution& reducedSolution){Solution out=reducedSolution;out.primal.assign(original.variables.size(),0.0);for(auto&v:original.variables){auto it=reduced.model.originalToActive.find(v.originalId);const std::size_t active=it!=reduced.model.originalToActive.end()?it->second:v.activeId;if(v.active&&active<reducedSolution.primal.size())out.primal[v.originalId]=reducedSolution.primal[active];}for(auto r:reduced.history.fixed)out.primal[r.originalId]=r.value;for(auto it=reduced.history.substitutions.rbegin();it!=reduced.history.substitutions.rend();++it)out.primal[it->eliminatedId]=it->constant+it->multiplier*out.primal[it->retainedId];out.objectiveValue=evaluateObjective(original,out.primal);return out;} }
