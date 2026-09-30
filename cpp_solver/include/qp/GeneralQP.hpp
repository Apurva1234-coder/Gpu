#pragma once
#include "qp/QPIPM.hpp"

namespace sovereign {
class GeneralQPInteriorPoint {
 public:
  explicit GeneralQPInteriorPoint(Tolerance t={}):tol_(t){}

  QPResult solve(const Model& m,std::size_t limit=150) const {
    const auto convexity=checkQPConvexity(m,tol_);
    if(!convexity.isConvex){
      QPResult r;
      r.status=convexity.classification==QPConvexity::Indefinite?QPStatus::UnsupportedNonconvex:QPStatus::NumericalFailure;
      r.message=convexity.message;
      return r;
    }

    // Convert every variable to z >= 0. A lower-bounded variable uses
    // x = lower + z; an upper-only variable uses x = upper - z. QPLIB 9002
    // has the latter form for most variables, so it needs no variable split.
    const std::size_t n=m.variables.size();
    std::vector<double> offset(n),sign(n,1.0),scale(n,1.0);
    for(std::size_t i=0;i<n;++i){
      const auto& v=m.variables[i];
      if(std::isfinite(v.lower)) {
        offset[i]=v.lower;
        if(std::isfinite(v.upper)) scale[i]=std::max(1.0,std::abs(v.upper-v.lower));
      }
      else if(std::isfinite(v.upper)){offset[i]=v.upper;sign[i]=-1.0;scale[i]=std::max(1.0,std::abs(v.upper));}
      else {
        QPResult r;
        r.status=QPStatus::Unsupported;
        r.message="variables without either finite bound require split-form QP support";
        return r;
      }
    }

    Model t;
    t.name=m.name+" [standard form]";
    for(std::size_t i=0;i<n;++i){
      t.variables.push_back({i,i,m.variables[i].name,VariableType::Continuous,0,INF,true});
      const double q=m.quadratic.count(i)?m.quadratic.at(i):0.0;
      const double c=m.objective.count(i)?m.objective.at(i):0.0;
      if(q) t.quadratic[i]=q;
      const double transformedLinear=sign[i]*(c+q*offset[i]);
      if(transformedLinear) t.objective[i]=transformedLinear;
    }
    auto addVar=[&](const std::string& name){
      const std::size_t id=t.variables.size();
      t.variables.push_back({id,id,name,VariableType::Continuous,0,INF,true});
      scale.push_back(1.0);
      return id;
    };
    auto addEq=[&](std::unordered_map<std::size_t,double> a,double rhs,const std::string& name){
      Constraint c;
      c.originalId=t.constraints.size();
      c.name=name;
      c.coefficients=std::move(a);
      c.relation=Relation::Equal;
      c.rhs=rhs;
      t.constraints.push_back(std::move(c));
    };

    for(const auto& src:m.constraints){
      double rhs=src.rhs;
      std::unordered_map<std::size_t,double> a;
      for(auto q:src.coefficients){
        rhs-=q.second*offset[q.first];
        a[q.first]=q.second*sign[q.first];
      }
      if(src.relation==Relation::Equal) addEq(std::move(a),rhs,src.name);
      else {
        const std::size_t slack=addVar("slack_"+src.name);
        a[slack]=src.relation==Relation::LessEqual?1.0:-1.0;
        addEq(std::move(a),rhs,src.name+"_slack");
      }
    }

    // Only variables with both bounds need an explicit finite z upper bound.
    for(std::size_t i=0;i<n;++i){
      const auto& v=m.variables[i];
      if(std::isfinite(v.lower)&&std::isfinite(v.upper)){
        const std::size_t slack=addVar("upper_slack_"+std::to_string(i));
        scale[slack]=scale[i];
        addEq({{i,1.0},{slack,1.0}},v.upper-v.lower,"upper_bound_"+std::to_string(i));
      }
    }

    // QPLIB instances can combine 1e11 variable bounds with tiny diagonal
    // Hessian entries.  Normalize columns, rows, and the positive objective
    // scale before the barrier method so its Newton system is well-conditioned.
    for(std::size_t i=0;i<t.variables.size();++i){
      if(t.objective.count(i)) t.objective[i]*=scale[i];
      if(t.quadratic.count(i)) t.quadratic[i]*=scale[i]*scale[i];
    }
    for(auto& row:t.constraints){
      double magnitude=std::max(1.0,std::abs(row.rhs));
      for(auto& coefficient:row.coefficients){
        coefficient.second*=scale[coefficient.first];
        magnitude=std::max(magnitude,std::abs(coefficient.second));
      }
      if(magnitude>1.0){
        row.rhs/=magnitude;
        for(auto& coefficient:row.coefficients) coefficient.second/=magnitude;
      }
    }
    double objectiveMagnitude=1.0;
    for(const auto& entry:t.objective) objectiveMagnitude=std::max(objectiveMagnitude,std::abs(entry.second));
    for(const auto& entry:t.quadratic) objectiveMagnitude=std::max(objectiveMagnitude,std::abs(entry.second));
    if(objectiveMagnitude>1.0){
      for(auto& entry:t.objective) entry.second/=objectiveMagnitude;
      for(auto& entry:t.quadratic) entry.second/=objectiveMagnitude;
    }

    t.rebuildMappings();
    auto r=QPInteriorPoint{tol_}.solve(t,limit);
    if(r.x.size()<n){
      r.status=QPStatus::NumericalFailure;
      r.message="standard-form solution has wrong dimension";
      return r;
    }
    std::vector<double> original(n);
    for(std::size_t i=0;i<n;++i) original[i]=offset[i]+sign[i]*scale[i]*r.x[i];
    r.x=original;
    r.objectiveValue=0;
    for(std::size_t i=0;i<n;++i){
      const double q=m.quadratic.count(i)?m.quadratic.at(i):0.0;
      const double c=m.objective.count(i)?m.objective.at(i):0.0;
      r.objectiveValue+=.5*q*r.x[i]*r.x[i]+c*r.x[i];
    }
    r.primalResidual=0;
    for(const auto& c:m.constraints){
      double lhs=0,normalizer=std::max(1.0,std::abs(c.rhs));
      for(auto q:c.coefficients){ lhs+=q.second*r.x[q.first]; normalizer+=std::abs(q.second*r.x[q.first]); }
      const double error=c.relation==Relation::Equal?std::abs(lhs-c.rhs):
        c.relation==Relation::LessEqual?std::max(0.0,lhs-c.rhs):std::max(0.0,c.rhs-lhs);
      r.primalResidual=std::max(r.primalResidual,error/normalizer);
    }
    for(std::size_t i=0;i<n;++i){
      const double lowerViolation=std::max(0.0,m.variables[i].lower-r.x[i]);
      r.primalResidual=std::max(r.primalResidual,lowerViolation/std::max(1.0,std::abs(m.variables[i].lower)));
      if(std::isfinite(m.variables[i].upper)){
        const double upperViolation=std::max(0.0,r.x[i]-m.variables[i].upper);
        r.primalResidual=std::max(r.primalResidual,upperViolation/std::max(1.0,std::abs(m.variables[i].upper)));
      }
    }
    return r;
  }
 private:
  Tolerance tol_;
};
}
