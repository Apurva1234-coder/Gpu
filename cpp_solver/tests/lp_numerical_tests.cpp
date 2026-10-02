#include "lp/LPSolver.hpp"
#include "model/Input.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
using namespace sovereign;
static bool close(double a,double b,double t=1e-6){return std::abs(a-b)<=t*(1+std::abs(b));}
int main(){
    {
        Model m; m.name="two_phase_equality"; m.sense=Sense::Maximize;
        m.variables={{0,0,"x",VariableType::Continuous,0,INF,true},{1,1,"y",VariableType::Continuous,0,INF,true}};
        m.objective[0]=2; m.objective[1]=3;
        Constraint eq; eq.name="sum"; eq.coefficients[0]=1; eq.coefficients[1]=1; eq.relation=Relation::Equal; eq.rhs=10; m.constraints.push_back(eq);
        Constraint xcap; xcap.name="xcap"; xcap.coefficients[0]=1; xcap.relation=Relation::LessEqual; xcap.rhs=7; m.constraints.push_back(xcap);
        Constraint ycap; ycap.name="ycap"; ycap.coefficients[1]=1; ycap.relation=Relation::LessEqual; ycap.rhs=8; m.constraints.push_back(ycap);
        auto r=LPSolver{}.solve(m); assert(r.status==LPStatus::Optimal); assert(close(r.solution.primal[0],2)); assert(close(r.solution.primal[1],8)); assert(close(r.objectiveValue,28));
    }
    {
        Model m; m.variables={{0,0,"x",VariableType::Continuous,0,INF,true}};
        Constraint a; a.coefficients[0]=1; a.relation=Relation::LessEqual; a.rhs=1; m.constraints.push_back(a);
        Constraint b; b.coefficients[0]=1; b.relation=Relation::GreaterEqual; b.rhs=2; m.constraints.push_back(b);
        assert(LPSolver{}.solve(m).status==LPStatus::Infeasible);
    }
    {
        Model m; m.sense=Sense::Minimize; m.variables={{0,0,"x",VariableType::Continuous,0,INF,true}}; m.objective[0]=1;
        Constraint row; row.coefficients[0]=1; row.relation=Relation::GreaterEqual; row.rhs=5; m.constraints.push_back(row);
        auto r=LPSolver{}.solve(m); assert(r.status==LPStatus::Optimal); assert(close(r.solution.primal[0],5));
    }
    {
        Model m; m.sense=Sense::Maximize; m.variables={{0,0,"x",VariableType::Continuous,-INF,3,true}}; m.objective[0]=1;
        auto r=LPSolver{}.solve(m); assert(r.status==LPStatus::Optimal); assert(close(r.solution.primal[0],3));
    }
    {
        Model m; m.sense=Sense::Minimize; m.variables={{0,0,"x",VariableType::Continuous,0,INF,true}}; m.objective[0]=1;
        Constraint row; row.coefficients[0]=1e8; row.relation=Relation::GreaterEqual; row.rhs=2e8; m.constraints.push_back(row);
        auto r=LPSolver{}.solve(m); assert(r.status==LPStatus::Optimal); assert(close(r.solution.primal[0],2));
    }
    {
        const char* path="sovereign_leading_comment_mps_test.mps";
        std::ofstream f(path); f<<"* generated fixture\nNAME COMMENTED\nOBJSENSE\n MAX\nROWS\n N OBJ\n L CAP\nCOLUMNS\n X OBJ 1 CAP 1\nRHS\n R CAP 2\nENDATA\n"; f.close();
        Model m=parseInput(path); std::remove(path); assert(m.name=="COMMENTED"); assert(m.variables.size()==1); assert(m.constraints.size()==1);
        auto r=LPSolver{}.solve(m); assert(r.status==LPStatus::Optimal); assert(close(r.solution.primal[0],2));
    }
    {
        // Force the sparse revised-simplex path and exercise Phase I on a
        // model large enough that a dense tableau is no longer selected.
        Model m; m.name="sparse_phase_one"; m.sense=Sense::Maximize;
        const std::size_t count=1650;
        for(std::size_t i=0;i<count;++i) {
            m.variables.push_back({i,i,"x"+std::to_string(i),VariableType::Continuous,0,1,true});
            m.objective[i]=1;
        }
        Constraint lower; lower.name="positive_lower"; lower.coefficients[0]=1;
        lower.relation=Relation::GreaterEqual; lower.rhs=0.5; m.constraints.push_back(lower);
        auto r=LPSolver{}.solve(m);
        assert(r.status==LPStatus::Optimal); assert(r.solution.primal.size()==count);
        assert(close(r.solution.primal[0],1)); assert(close(r.objectiveValue,static_cast<double>(count)));
        assert(r.sparsePivots>0); assert(r.sparseRefactorizations>0);
        assert(r.sparsePricingTimeMs>=0); assert(r.sparseFactorizationTimeMs>=0);
    }
    return 0;
}
