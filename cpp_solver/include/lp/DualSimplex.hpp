#pragma once
#include "lp/LPSolver.hpp"
#include "core/LinearSystem.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

namespace sovereign {

// Dual simplex in the internal equality form
//     max c'x, A x = b, x >= 0.
// The basis state below is authoritative; tableau values are not used to
// reconstruct a solution.
class DualSimplex {
    struct State {
        std::vector<std::vector<double>> A, B;
        std::vector<double> b, c, xB, y, reduced;
        std::vector<std::size_t> basis, nonbasis, position;
        std::vector<bool> basic;
    };
    Tolerance tol_;

    bool validateBasis(const State& q) const {
        if(q.basis.size()!=q.b.size()||q.basic.size()!=q.c.size()||q.position.size()!=q.c.size())return false;
        std::vector<bool> seen(q.c.size(),false);for(auto j:q.basis){if(j>=q.c.size()||seen[j])return false;seen[j]=true;}for(auto j:q.nonbasis)if(j>=q.c.size()||seen[j])return false;return true;
    }
    bool rebuild(State& q) const {
        const std::size_t m=q.b.size(),n=q.c.size(); q.B.assign(m,std::vector<double>(m));q.position.assign(n,m);q.basic.assign(n,false);q.nonbasis.clear();
        for(std::size_t i=0;i<m;++i){if(q.basis[i]>=n)return false;q.basic[q.basis[i]]=true;q.position[q.basis[i]]=i;for(std::size_t k=0;k<m;++k)q.B[k][i]=q.A[k][q.basis[i]];}
        for(std::size_t j=0;j<n;++j)if(!q.basic[j])q.nonbasis.push_back(j);return validateBasis(q);
    }
    bool refresh(State& q) const {
        if(!rebuild(q))return false;const std::size_t m=q.b.size(),n=q.c.size();
        if(!LinearSystem::solve(q.B,q.b,q.xB,tol_))return false;
        std::vector<std::vector<double>> bt(m,std::vector<double>(m));for(std::size_t i=0;i<m;++i)for(std::size_t j=0;j<m;++j)bt[i][j]=q.B[j][i];std::vector<double> cb(m);for(std::size_t i=0;i<m;++i)cb[i]=q.c[q.basis[i]];if(!LinearSystem::solve(bt,cb,q.y,tol_))return false;
        q.reduced.assign(n,0);for(std::size_t j=0;j<n;++j){double v=q.c[j];for(std::size_t i=0;i<m;++i)v-=q.A[i][j]*q.y[i];q.reduced[j]=v;}
        double br=0,yr=0;for(std::size_t i=0;i<m;++i){double v=0;for(std::size_t j=0;j<m;++j)v+=q.B[i][j]*q.xB[j];br=std::max(br,std::abs(v-q.b[i]));double w=0;for(std::size_t j=0;j<m;++j)w+=q.B[j][i]*q.y[j];yr=std::max(yr,std::abs(w-cb[i]));}return br<=tol_.feasibility*100&&yr<=tol_.feasibility*100;
    }
    bool dualFeasible(const State& q) const {for(auto j:q.nonbasis)if(q.reduced[j]>tol_.optimality)return false;return true;}
    bool repairDualBasis(State& q,std::size_t limit) const {
        // Construct a dual-feasible basis from the slack basis. This is a
        // basis-construction phase only; all state is recomputed from B.
        for(std::size_t it=0;it<limit;++it){if(!refresh(q))return false;if(dualFeasible(q))return true;std::size_t e=q.c.size();for(auto j:q.nonbasis)if(q.reduced[j]>tol_.optimality){e=j;break;}if(e==q.c.size())return true;std::vector<double> col(q.b.size());for(std::size_t i=0;i<q.b.size();++i)col[i]=q.A[i][e];std::vector<double> d;if(!LinearSystem::solve(q.B,col,d,tol_))return false;std::size_t l=q.b.size();double best=std::numeric_limits<double>::infinity();for(std::size_t i=0;i<q.b.size();++i)if(d[i]>tol_.pivot&&q.xB[i]>=-tol_.feasibility){double z=q.xB[i]/d[i];if(z<best){best=z;l=i;}}if(l==q.b.size())return false;q.basis[l]=e;}
        return false;
    }
public:
    explicit DualSimplex(Tolerance t={}):tol_(t){}
    LPResult solve(const Model& m,std::size_t limit=10000) const {
        LPResult out;out.method="dual-simplex";for(const auto&v:m.variables)if(v.type!=VariableType::Continuous){out.status=LPStatus::Unsupported;return out;}if(!m.quadratic.empty()){out.status=LPStatus::Unsupported;return out;}
        const auto standardizationStart=std::chrono::steady_clock::now();StandardLP s=standardize(m,tol_);out.standardizedRows=s.A.size();out.standardizedColumns=s.c.size();for(const auto& row:s.A)for(double value:row)if(value!=0.0)++out.standardizedNonzeros;out.standardizationTimeMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-standardizationStart).count();const std::size_t n=s.c.size(),rows=s.A.size();State q;q.b=s.b;q.c.assign(n+rows,0);q.A.assign(rows,std::vector<double>(n+rows,0));for(std::size_t i=0;i<rows;++i){for(std::size_t j=0;j<n;++j)q.A[i][j]=s.A[i][j];q.A[i][n+i]=1;}for(std::size_t j=0;j<n;++j)q.c[j]=s.c[j];q.basis.resize(rows);std::iota(q.basis.begin(),q.basis.end(),n);
        if(!repairDualBasis(q,limit)){out.status=LPStatus::NumericalFailure;return out;}if(!refresh(q)||!dualFeasible(q)){out.status=LPStatus::NumericalFailure;return out;}
        for(std::size_t it=0;it<limit;++it){std::size_t leave=rows;double most=-tol_.feasibility;for(std::size_t i=0;i<rows;++i)if(q.xB[i]<most){most=q.xB[i];leave=i;}if(leave==rows){out.status=LPStatus::Optimal;out.iterations=it;break;}std::size_t enter=n+rows;double best=std::numeric_limits<double>::infinity();std::vector<double> row(rows);for(std::size_t j:q.nonbasis){std::vector<double> col(rows);for(std::size_t i=0;i<rows;++i)col[i]=q.A[i][j];if(!LinearSystem::solve(q.B,col,row,tol_))continue;double d=row[leave];if(d>=-tol_.pivot)continue;double ratio=(-q.reduced[j])/(-d);if(ratio<best){best=ratio;enter=j;}}
            if(enter==n+rows){out.status=LPStatus::Infeasible;out.iterations=it;return out;}q.basis[leave]=enter;if(!refresh(q)||!dualFeasible(q)){out.status=LPStatus::NumericalFailure;out.iterations=it+1;return out;}out.iterations=it+1;}
        if(out.status!=LPStatus::Optimal){out.status=LPStatus::IterationLimit;return out;}
        out.solution.primal.assign(m.variables.size(),0);auto activeIndex=[&](std::size_t original){auto it=m.originalToActive.find(original);return it==m.originalToActive.end()?original:it->second;};for(std::size_t i=0;i<rows;++i)if(q.basis[i]<n){auto id=s.map[q.basis[i]].first;out.solution.primal[activeIndex(id)]+=s.map[q.basis[i]].second*q.xB[i];}for(std::size_t i=0;i<m.variables.size();++i)if(m.variables[i].active&&std::isfinite(m.variables[i].lower)){auto k=activeIndex(m.variables[i].originalId);out.solution.primal[k]+=m.variables[i].lower;}out.objectiveValue=evaluateObjective(m,out.solution.primal);out.solution.objectiveValue=out.objectiveValue;return out;
    }
};
}
