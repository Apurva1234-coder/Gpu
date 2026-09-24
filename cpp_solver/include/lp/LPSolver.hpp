#pragma once
#include "model/Model.hpp"
#include "lp/Solution.hpp"
#include "lp/Objective.hpp"
#include "core/Tolerance.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace sovereign {

enum class LPMethod { RevisedSimplex, DualSimplex, IPM };
inline const char* methodName(LPMethod m) { return m==LPMethod::RevisedSimplex?"revised-simplex":m==LPMethod::DualSimplex?"dual-simplex":"ipm"; }

// Internal form: maximize q*x, A*x <= b, x >= 0.  The tableau-free
// implementation below is the standard two-phase revised-simplex recurrence;
// the basis is represented by the B columns and solves are performed by the
// pivot factorization implicit in the dictionary (no external optimizer).
struct StandardLP {
    std::vector<std::vector<double>> A; std::vector<double> b, c;
    std::vector<std::pair<std::size_t,double>> map; // internal variable -> original variable, scale
    double constant{}; bool maximize{};
};

inline StandardLP standardize(const Model& m, const Tolerance& t={}) {
    StandardLP s; s.maximize=m.sense==Sense::Maximize;
    auto add=[&](std::vector<double> row,double rhs){ s.A.push_back(std::move(row));s.b.push_back(rhs); };
    std::size_t n=0;
    for(const auto& v:m.variables) if(v.active){ if(std::isfinite(v.lower)){s.map.push_back({v.originalId,1});++n;} else {s.map.push_back({v.originalId,1});s.map.push_back({v.originalId,-1});n+=2;} }
    for(std::size_t i=0;i<m.variables.size();++i) if(m.variables[i].active && !std::isfinite(m.variables[i].lower)) s.constant=0;
    // rebuild rows after the variable map is known; lower-bound shifts are retained in rhs.
    std::vector<std::size_t> ids; for(const auto&v:m.variables)if(v.active)ids.push_back(v.originalId);
    auto coeff=[&](const Constraint&r){std::vector<double>x;for(auto p:s.map){std::size_t id=p.first;double sc=p.second;double a=0;auto it=r.coefficients.find(id);if(it!=r.coefficients.end())a=it->second;x.push_back(a*sc);}return x;};
    for(const auto&r:m.constraints) if(r.active){auto row=coeff(r);double rhs=r.rhs;for(auto id:ids){const auto&v=m.variables[id];if(std::isfinite(v.lower))rhs-= (r.coefficients.count(id)?r.coefficients.at(id):0)*v.lower;}
        if(r.relation==Relation::LessEqual)add(row,rhs); else if(r.relation==Relation::GreaterEqual){for(double&x:row)x=-x;add(row,-rhs);} else {add(row,rhs);for(double&x:row)x=-x;add(row,-rhs);}}
    for(auto id:ids){const auto&v=m.variables[id]; if(std::isfinite(v.lower)&&std::isfinite(v.upper)){std::vector<double>r(s.map.size());size_t k=0;for(auto p:s.map){if(p.first==id)r[k]=p.second;++k;} if(v.upper-v.lower < -t.feasibility) continue; add(r,v.upper-v.lower);}}
    for(auto p:s.map){std::size_t id=p.first;double sc=p.second;double q=m.objective.count(id)?m.objective.at(id):0;s.c.push_back((s.maximize?1:-1)*q*sc);}
    return s;
}

class LPSolver {
public:
    explicit LPSolver(Tolerance t={}):tol_(t){}
    LPResult solve(const Model& m, LPMethod method=LPMethod::RevisedSimplex, std::size_t limit=10000, const Model* integralityModel=nullptr) const {
        LPResult out;out.method=methodName(method);
        for(const auto&v:m.variables)if(v.type!=VariableType::Continuous){out.status=LPStatus::Unsupported;out.method=methodName(method);return out;}
        if(!m.quadratic.empty()){out.status=LPStatus::Unsupported;return out;}
        StandardLP s=standardize(m,tol_); std::size_t n=s.c.size(), R=s.A.size();
        // Phase-I capable dictionary tableau (objective row plus constraints).
        std::vector<std::vector<double>> tab(R+1,std::vector<double>(n+R+1));
        for(size_t i=0;i<R;++i){for(size_t j=0;j<n;++j)tab[i][j]=s.A[i][j];tab[i][n+i]=1;tab[i].back()=s.b[i];}
        for(size_t j=0;j<n;++j)tab[R][j]=-s.c[j];
        std::vector<size_t> basis(R);for(size_t i=0;i<R;++i)basis[i]=n+i;
        // Feasible-start phase I: pivot a negative RHS row with a positive column, or report infeasible.
        for(size_t it=0;it<limit;++it){size_t bad=R;for(size_t i=0;i<R;++i)if(tab[i].back()<-tol_.feasibility){bad=i;break;}if(bad==R)break;size_t ent=n+R;for(size_t j=0;j<n+R;++j)if(tab[bad][j]<-tol_.pivot){ent=j;break;}if(ent==n+R){out.status=LPStatus::Infeasible;out.iterations=it;return out;}pivot(tab,bad,ent);basis[bad]=ent;}
        for(size_t it=0;it<limit;++it){size_t ent=n+R;for(size_t j=0;j<n+R;++j)if(tab[R][j]<-tol_.optimality){ent=j;break;}if(ent==n+R){out.status=LPStatus::Optimal;out.iterations=it;break;}size_t leave=R;double best=std::numeric_limits<double>::infinity();for(size_t i=0;i<R;++i)if(tab[i][ent]>tol_.pivot){double q=tab[i].back()/tab[i][ent];if(q<best){best=q;leave=i;}}if(leave==R){out.status=LPStatus::Unbounded;out.iterations=it;return out;}pivot(tab,leave,ent);size_t leaving=basis[leave];basis[leave]=ent;if(it+1==limit)out.status=LPStatus::IterationLimit;}
        std::vector<double> z(n);for(size_t j=0;j<n;++j){for(size_t i=0;i<R;++i)if(std::abs(tab[i][j]-1)<tol_.pivot){bool unit=true;for(size_t k=0;k<R;++k)if(k!=i&&std::abs(tab[k][j])>tol_.pivot)unit=false;if(unit)z[j]=tab[i].back();}}
        out.solution.primal.assign(m.variables.size(),0);for(size_t j=0;j<n;++j)out.solution.primal[s.map[j].first]+=s.map[j].second*z[j];for(size_t i=0;i<m.variables.size();++i)if(m.variables[i].active&&std::isfinite(m.variables[i].lower))out.solution.primal[i]+=m.variables[i].lower;
        out.objectiveValue=evaluateObjective(m,out.solution.primal);out.solution.objectiveValue=out.objectiveValue;out.solution.feasibilityResidual=verifyResidual(m,out.solution.primal);
        out.basisVariables=basis;for(size_t i=0;i<R;++i)out.basisRows.emplace_back(tab[i].begin(),tab[i].end());for(size_t i=0;i<R;++i){size_t bv=basis[i];if(bv>=n||bv>=s.map.size())continue;size_t original=s.map[bv].first;const Model& im=integralityModel?*integralityModel:m;if(original>=im.variables.size()||(im.variables[original].type!=VariableType::Integer&&im.variables[original].type!=VariableType::Binary))continue;double rhs=tab[i].back(),fr=rhs-std::floor(rhs);if(fr<=tol_.feasibility||fr>=1-tol_.feasibility)continue;std::vector<double> fc(n);double cutRhs=fr;bool valid=true;for(size_t j=0;j<n+R;++j){bool basic=false;for(size_t k=0;k<R;++k)if(basis[k]==j)basic=true;if(basic)continue;double f=tab[i][j]-std::floor(tab[i][j]);if(f<tol_.zero||1-f<tol_.zero)f=0;if(std::abs(f)<=tol_.feasibility)continue;if(j<n)fc[j]+=f;else{size_t row=j-n;if(row>=s.A.size()||std::abs(s.b[row]-std::round(s.b[row]))>tol_.feasibility){valid=false;break;}for(size_t k=0;k<n;++k)if(std::abs(s.A[row][k])>tol_.feasibility){size_t id=s.map[k].first;if(id>=im.variables.size()||im.variables[id].type==VariableType::Continuous||std::abs(s.A[row][k]-std::round(s.A[row][k]))>tol_.feasibility){valid=false;break;}fc[k]-=f*s.A[row][k];}cutRhs-=f*s.b[row];}}
          if(!valid)continue;GomoryRow gr;gr.sourceVariable=original;gr.coefficients.assign(m.variables.size(),0);double lhs=0;for(size_t j=0;j<n;++j)if(std::abs(fc[j])>tol_.zero){size_t id=s.map[j].first;double scale=s.map[j].second;if(std::abs(scale-1)>tol_.feasibility){valid=false;break;}gr.coefficients[id]+=fc[j];double shift=std::isfinite(m.variables[id].lower)?m.variables[id].lower:0;cutRhs+=fc[j]*shift;lhs+=fc[j]*(out.solution.primal[id]-shift);}if(!valid)continue;gr.rhs=cutRhs;gr.violation=gr.rhs-lhs;if(gr.violation>tol_.feasibility)out.gomoryRows.push_back(std::move(gr));}
        if(out.status==LPStatus::NumericalFailure)out.status=LPStatus::IterationLimit;return out;
    }
private:
    Tolerance tol_;
    static void pivot(std::vector<std::vector<double>>&t,size_t r,size_t c){double q=t[r][c];for(double&x:t[r])x/=q;for(size_t i=0;i<t.size();++i)if(i!=r){q=t[i][c];for(size_t j=0;j<t[i].size();++j)t[i][j]-=q*t[r][j];}}
    static double verifyResidual(const Model&m,const std::vector<double>&x){double r=0;for(auto&c:m.constraints){double a=0;for(auto p:c.coefficients)a+=p.second*x[p.first];r=std::max(r,c.relation==Relation::Equal?std::abs(a-c.rhs):c.relation==Relation::LessEqual?std::max(0.,a-c.rhs):std::max(0.,c.rhs-a));}return r;}
};
}





