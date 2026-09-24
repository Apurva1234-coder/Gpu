#pragma once
#include "model/Classification.hpp"
#include "lp/LPSolver.hpp"
#include "lp/DualSimplex.hpp"
#include "lp/MehrotraIPM.hpp"
#include "lp/Objective.hpp"
#include <cmath>
#include <string>
namespace sovereign {
struct LPRelaxationResult { LPStatus status{LPStatus::NumericalFailure}; std::vector<double> solution; double objectiveValue=0,bound=0,primalResidual=0;std::size_t iterations=0;std::string method,boundType,message;bool fractionalSolution=false,integralWithinTolerance=false;std::size_t fractionalIntegerVariables=0; };
inline Model relaxMILP(const Model& source){Model r=source;for(auto&v:r.variables){v.type=VariableType::Continuous;if(source.variables[v.originalId].type==VariableType::Binary){v.lower=std::max(0.0,v.lower);v.upper=std::min(1.0,v.upper);}}r.rebuildMappings();return r;}
inline bool isIntegralWithinTolerance(const Model& m,const std::vector<double>& x,double tol=1e-8){for(const auto&v:m.variables)if((v.type==VariableType::Integer||v.type==VariableType::Binary)&&v.originalId<x.size()&&std::abs(x[v.originalId]-std::round(x[v.originalId]))>tol)return false;return true;}
inline LPRelaxationResult solveLPRelaxation(const Model& milp,LPMethod method=LPMethod::RevisedSimplex,std::size_t limit=10000){LPRelaxationResult r;Model lp=relaxMILP(milp);LPResult s;if(method==LPMethod::DualSimplex)s=DualSimplex{}.solve(lp,limit);else if(method==LPMethod::IPM)s=MehrotraIPM{}.solve(lp,limit);else s=LPSolver{}.solve(lp,LPMethod::RevisedSimplex,limit);r.status=s.status;r.method=s.method;r.iterations=s.iterations;r.solution=s.solution.primal;r.objectiveValue=evaluateObjective(milp,r.solution);r.bound=r.objectiveValue;r.boundType=milp.sense==Sense::Minimize?"LOWER_BOUND":"UPPER_BOUND";r.primalResidual=s.solution.feasibilityResidual;for(const auto&v:milp.variables)if((v.type==VariableType::Integer||v.type==VariableType::Binary)&&v.originalId<r.solution.size()&&std::abs(r.solution[v.originalId]-std::round(r.solution[v.originalId]))>1e-8)++r.fractionalIntegerVariables;r.fractionalSolution=r.fractionalIntegerVariables>0;r.integralWithinTolerance=!r.fractionalSolution;return r;}
}
