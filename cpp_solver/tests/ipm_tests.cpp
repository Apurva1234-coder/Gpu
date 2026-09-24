#include "lp/MehrotraIPM.hpp"
#include <cassert>
#include <cmath>
using namespace sovereign;

static Model productMix() {
    Model m; m.name="product mix"; m.sense=Sense::Maximize;
    m.variables={{0,0,"x",VariableType::Continuous,0,INF,true},
                 {1,1,"y",VariableType::Continuous,0,INF,true}};
    m.objective={{0,40},{1,60}};
    Constraint a; a.coefficients={{0,2},{1,4}};a.relation=Relation::LessEqual;a.rhs=100;
    Constraint b; b.coefficients={{0,3},{1,2}};b.relation=Relation::LessEqual;b.rhs=80;
    m.constraints={a,b};return m;
}

int main() {
    auto max=MehrotraIPM{}.solve(productMix());
    assert(max.status==LPStatus::Optimal);
    assert(std::abs(max.objectiveValue-1650)<1e-5);
    assert(max.solution.primal.size()==2);
    assert(std::abs(max.solution.primal[0]-15)<1e-5);
    assert(std::abs(max.solution.primal[1]-17.5)<1e-5);

    Model min;min.name="minimization";min.sense=Sense::Minimize;
    min.variables={{0,0,"x",VariableType::Continuous,0,INF,true},
                   {1,1,"y",VariableType::Continuous,0,INF,true}};
    min.objective={{0,2},{1,3}};
    Constraint demand;demand.coefficients={{0,1},{1,1}};demand.relation=Relation::GreaterEqual;demand.rhs=4;
    min.constraints={demand};
    auto low=MehrotraIPM{}.solve(min);
    assert(low.status==LPStatus::Optimal);
    assert(std::abs(low.objectiveValue-8)<1e-5);
    assert(low.solution.primal.size()==2);
    assert(std::abs(low.solution.primal[0]-4)<1e-5);
    assert(std::abs(low.solution.primal[1])<1e-5);
}
