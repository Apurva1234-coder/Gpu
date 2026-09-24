#include "presolve/Presolver.hpp"
#include "core/SparseMatrix.hpp"
#include "model/Classification.hpp"
#include "lp/Verifier.hpp"
#include "presolve/Postsolve.hpp"
#include <cassert>
using namespace sovereign;
int main(){
    SparseMatrix matrix(2,2); matrix.set(0,0,2); matrix.set(0,1,3); assert(matrix.get(0,1)==3); assert(matrix.multiply({1,2})[0]==8); assert(matrix.column(0).size()==1);
    Model fixed; fixed.variables.push_back({0,0,"x",VariableType::Continuous,2,2,true}); fixed.objective[0]=3; auto a=Presolver{}.run(fixed); assert(a.stats.fixedVariables==1);
    Model infeasible; infeasible.variables.push_back({0,0,"x",VariableType::Continuous,5,1,true}); assert(Presolver{}.run(infeasible).status==PresolveStatus::Infeasible);
    Model unbounded; unbounded.sense=Sense::Maximize; unbounded.variables.push_back({0,0,"x",VariableType::Continuous,0,INF,true}); unbounded.objective[0]=1; assert(Presolver{}.run(unbounded).status==PresolveStatus::Unbounded);
    Model lp; lp.variables.push_back({0,0,"x",VariableType::Continuous,0,10,true}); Constraint row;row.name="limit";row.coefficients[0]=1;row.relation=Relation::LessEqual;row.rhs=5;lp.constraints.push_back(row);auto b=Presolver{}.run(lp);assert(b.stats.boundTightenings==1);
    Model e; e.name="e2e"; e.variables.push_back({0,0,"x",VariableType::Continuous,0,INF,true}); e.variables.push_back({1,1,"y",VariableType::Continuous,0,INF,true}); e.variables.push_back({2,2,"z",VariableType::Continuous,0,INF,true}); e.objective[0]=2; e.objective[1]=1; Constraint eq;eq.originalId=0;eq.name="sum";eq.coefficients[0]=1;eq.coefficients[1]=1;eq.relation=Relation::Equal;eq.rhs=10;e.constraints.push_back(eq); Constraint fixz;fixz.originalId=1;fixz.name="fixz";fixz.coefficients[2]=1;fixz.relation=Relation::Equal;fixz.rhs=3;e.constraints.push_back(fixz); Constraint redundant;redundant.originalId=2;redundant.name="redundant";redundant.relation=Relation::LessEqual;redundant.rhs=0;e.constraints.push_back(redundant); auto pr=Presolver{}.run(e); assert(pr.stats.substitutions==1&&pr.stats.fixedVariables==1&&pr.stats.redundantRows>=1&&pr.stats.boundTightenings>=1); assert(pr.model.originalToActive.count(1)); Solution rs;rs.primal={4}; auto os=postsolve(e,pr,rs); assert(std::abs(os.primal[0]-6)<1e-9&&std::abs(os.primal[1]-4)<1e-9&&std::abs(os.primal[2]-3)<1e-9); assert(verify(e,os).feasible); assert(std::abs(os.objectiveValue-16)<1e-9); return 0;
}
