#pragma once
#include "qp/QPIPM.hpp"
namespace sovereign {
class GeneralQPInteriorPoint {
 public:
  explicit GeneralQPInteriorPoint(Tolerance t={}):tol_(t){}
  QPResult solve(const Model& m,std::size_t limit=150) const {
    const auto convexity=checkQPConvexity(m,tol_);
    if(!convexity.isConvex){QPResult r;r.status=convexity.classification==QPConvexity::Indefinite?QPStatus::UnsupportedNonconvex:QPStatus::NumericalFailure;r.message=convexity.message;return r;}
    for(const auto& v:m.variables) if(!std::isfinite(v.lower)){ QPResult r;r.status=QPStatus::Unsupported;r.message="free variables require split-form QP support";return r; }
    Model t;t.name=m.name+" [standard form]";const std::size_t n=m.variables.size();
    for(std::size_t i=0;i<n;++i){t.variables.push_back({i,i,m.variables[i].name,VariableType::Continuous,0,INF,true});if(m.objective.count(i))t.objective[i]=m.objective.at(i)+(m.quadratic.count(i)?m.quadratic.at(i)*m.variables[i].lower:0);if(m.quadratic.count(i))t.quadratic[i]=m.quadratic.at(i);}
    auto addVar=[&](const std::string& name){std::size_t id=t.variables.size();t.variables.push_back({id,id,name,VariableType::Continuous,0,INF,true});return id;};
    auto addEq=[&](std::unordered_map<std::size_t,double> a,double rhs,const std::string& name){Constraint c;c.originalId=t.constraints.size();c.name=name;c.coefficients=std::move(a);c.relation=Relation::Equal;c.rhs=rhs;t.constraints.push_back(std::move(c));};
    for(const auto& src:m.constraints){double rhs=src.rhs;std::unordered_map<std::size_t,double> a;for(auto q:src.coefficients){rhs-=q.second*m.variables[q.first].lower;a[q.first]=q.second;}if(src.relation==Relation::Equal)addEq(a,rhs,src.name);else{std::size_t s=addVar("slack_"+src.name);a[s]=1;addEq(a,rhs,src.name+"_slack");}}
    for(std::size_t i=0;i<n;++i)if(std::isfinite(m.variables[i].upper)){std::size_t s=addVar("upper_slack_"+std::to_string(i));addEq({{i,1},{s,1}},m.variables[i].upper-m.variables[i].lower,"upper_bound_"+std::to_string(i));}
    t.rebuildMappings();auto r=QPInteriorPoint{tol_}.solve(t,limit);if(r.x.size()<n){r.status=QPStatus::NumericalFailure;r.message="standard-form solution has wrong dimension";return r;}std::vector<double> original(n);for(std::size_t i=0;i<n;++i)original[i]=r.x[i]+m.variables[i].lower;r.x=original;r.objectiveValue=0;for(std::size_t i=0;i<n;++i)r.objectiveValue+=.5*(m.quadratic.count(i)?m.quadratic.at(i):0)*r.x[i]*r.x[i]+(m.objective.count(i)?m.objective.at(i):0)*r.x[i];r.primalResidual=0;for(const auto& c:m.constraints){double lhs=0;for(auto q:c.coefficients)lhs+=q.second*r.x[q.first];double e=c.relation==Relation::Equal?std::abs(lhs-c.rhs):c.relation==Relation::LessEqual?std::max(0.0,lhs-c.rhs):std::max(0.0,c.rhs-lhs);r.primalResidual=std::max(r.primalResidual,e);}for(std::size_t i=0;i<n;++i){r.primalResidual=std::max(r.primalResidual,std::max(0.0,m.variables[i].lower-r.x[i]));if(std::isfinite(m.variables[i].upper))r.primalResidual=std::max(r.primalResidual,std::max(0.0,r.x[i]-m.variables[i].upper));}return r;
  }
 private: Tolerance tol_;
};
}

