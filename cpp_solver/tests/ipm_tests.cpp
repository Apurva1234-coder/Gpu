#include "lp/MehrotraIPM.hpp"
#include "qp/GeneralQP.hpp"
#include "model/Input.hpp"
#include <cassert>
#include <cmath>
#include <fstream>
#include <cstdio>
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

    Model nonconvex; nonconvex.name="nonconvex";
    nonconvex.variables={{0,0,"x",VariableType::Continuous,0,INF,true}};
    nonconvex.quadratic[0]=-2;
    assert(GeneralQPInteriorPoint{}.solve(nonconvex).status==QPStatus::UnsupportedNonconvex);

    Model unconstrained;unconstrained.name="one variable QP with objective constant";
    unconstrained.variables={{0,0,"x",VariableType::Continuous,0,INF,true}};
    unconstrained.quadratic[0]=2.0;unconstrained.objective[0]=-4.0;unconstrained.objectiveConstant=7.0;
    auto one=GeneralQPInteriorPoint{}.solve(unconstrained);
    assert(one.status==QPStatus::Optimal&&one.kktVerified);
    assert(std::abs(one.x[0]-2.0)<1e-6&&std::abs(one.objectiveValue-3.0)<1e-6);
    assert(one.primalResidual<1e-8&&one.dualResidual<1e-8&&one.complementarity<1e-8);

    const char* jsonPath="qp_objective_constant_test.json";
    {std::ofstream json(jsonPath);json<<R"({"name":"offset","objective_sense":"minimize","objective_constant":5.5,"variables":[{"name":"x","type":"continuous"}],"objective":{},"quadratic_terms":{},"constraints":[],"bounds":{}})";}
    const auto parsed=parseJSON(jsonPath);std::remove(jsonPath);
    assert(std::abs(parsed.objectiveConstant-5.5)<1e-12);
    const char* qplibPath="qp_native_parser_test.qplib";
    {std::ofstream qplib(qplibPath);qplib<<"QPLIB_TEST\nDCL\nminimize\n1\n0\n1\n1 1 2\n0\n0\n5.5\n0\n1e20\n-1e20\n0\n1e20\n0\n0\n0\n1e20\n0\n";}
    const auto nativeQplib=parseInput(qplibPath);std::remove(qplibPath);
    assert(nativeQplib.variables.size()==1&&nativeQplib.constraints.empty());
    assert(std::abs(nativeQplib.quadratic.at(0)-2.0)<1e-12&&std::abs(nativeQplib.objectiveConstant-5.5)<1e-12);
    assert(nativeQplib.variables[0].lower==0&&nativeQplib.variables[0].upper==INF);

    Model equality;equality.name="two variable equality QP";
    equality.variables={{0,0,"x",VariableType::Continuous,0,INF,true},{1,1,"y",VariableType::Continuous,0,INF,true}};
    equality.quadratic={{0,2.0},{1,2.0}};
    Constraint sum;sum.coefficients={{0,1.0},{1,1.0}};sum.relation=Relation::Equal;sum.rhs=3.0;equality.constraints={sum};
    auto two=GeneralQPInteriorPoint{}.solve(equality);
    assert(two.status==QPStatus::Optimal&&two.kktVerified);
    assert(std::abs(two.x[0]-1.5)<1e-6&&std::abs(two.x[1]-1.5)<1e-6);
    assert(std::abs(two.objectiveValue-4.5)<1e-6);

    Model inequality;inequality.name="less-equal active QP bound";
    inequality.variables={{0,0,"x",VariableType::Continuous,0,INF,true}};
    inequality.quadratic[0]=2.0;inequality.objective[0]=-2.0;
    Constraint cap;cap.coefficients={{0,1.0}};cap.relation=Relation::LessEqual;cap.rhs=1.0;inequality.constraints={cap};
    auto capped=GeneralQPInteriorPoint{}.solve(inequality);
    assert(capped.status==QPStatus::Optimal&&capped.kktVerified);
    assert(std::abs(capped.x[0]-1.0)<1e-6&&std::abs(capped.objectiveValue+1.0)<1e-6);

    Model doubleBound;doubleBound.name="double bounded QP";
    doubleBound.variables={{0,0,"x",VariableType::Continuous,1.0,2.0,true}};
    doubleBound.quadratic[0]=2.0;doubleBound.objective[0]=-2.0;
    auto bounded=GeneralQPInteriorPoint{}.solve(doubleBound);
    assert(bounded.status==QPStatus::Optimal&&bounded.kktVerified);
    assert(std::abs(bounded.x[0]-1.0)<1e-6&&std::abs(bounded.objectiveValue+1.0)<1e-6);

    auto limited=GeneralQPInteriorPoint{}.solve(equality,0);
    assert(limited.status==QPStatus::IterationLimit);
}
