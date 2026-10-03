#pragma once
#include "qp/QPIPM.hpp"
#include "cuda/CudaBackend.hpp"
#include <chrono>

namespace sovereign {
class GeneralQPInteriorPoint {
 public:
  explicit GeneralQPInteriorPoint(Tolerance t={}):tol_(t){}

  QPResult solve(const Model& m,std::size_t limit=150) const {
    using Clock=std::chrono::steady_clock;
    if(m.sense!=Sense::Minimize){
      QPResult r;r.status=QPStatus::Unsupported;
      r.message="the convex QP method currently supports minimization only";
      return r;
    }
    const auto convexityStart=Clock::now();
    const auto convexity=checkQPConvexity(m,tol_);
    const double convexityTimeMs=std::chrono::duration<double,std::milli>(Clock::now()-convexityStart).count();
    if(!convexity.isConvex){
      QPResult r;
      r.status=convexity.classification==QPConvexity::Indefinite?QPStatus::UnsupportedNonconvex:QPStatus::NumericalFailure;
      r.message=convexity.message;
      return r;
    }
    for(const auto& row:m.quadraticMatrix) for(const auto& entry:row.second)
      if(row.first!=entry.first&&std::abs(entry.second)>tol_.zero){
        QPResult r;r.status=QPStatus::Unsupported;
        r.message="general QP transformation currently supports diagonal Hessians only";
        return r;
      }

    // Convert every variable to z >= 0. A lower-bounded variable uses
    // x = lower + z; an upper-only variable uses x = upper - z. QPLIB 9002
    // has the latter form for most variables, so it needs no variable split.
    const auto transformStart=Clock::now();
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
    std::vector<std::size_t> upperSlackVariable(n,std::numeric_limits<std::size_t>::max());

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
        upperSlackVariable[i]=slack;
        scale[slack]=scale[i];
        addEq({{i,1.0},{slack,1.0}},v.upper-v.lower,"upper_bound_"+std::to_string(i));
      }
    }

    const double transformationTimeMs=std::chrono::duration<double,std::milli>(Clock::now()-transformStart).count();
    const auto scalingStart=Clock::now();
    // QPLIB instances can combine 1e11 variable bounds with tiny diagonal
    // Hessian entries.  Normalize columns, rows, and the positive objective
    // scale before the barrier method so its Newton system is well-conditioned.
    for(std::size_t i=0;i<t.variables.size();++i){
      if(t.objective.count(i)) t.objective[i]*=scale[i];
      if(t.quadratic.count(i)) t.quadratic[i]*=scale[i]*scale[i];
    }
    std::vector<double> rowScale(t.constraints.size(),1.0);
    for(std::size_t rowIndex=0;rowIndex<t.constraints.size();++rowIndex){
      auto& row=t.constraints[rowIndex];
      double magnitude=std::max(1.0,std::abs(row.rhs));
      for(auto& coefficient:row.coefficients){
        coefficient.second*=scale[coefficient.first];
        magnitude=std::max(magnitude,std::abs(coefficient.second));
      }
      if(magnitude>1.0){
        row.rhs/=magnitude;
        for(auto& coefficient:row.coefficients) coefficient.second/=magnitude;
      }
      rowScale[rowIndex]=magnitude;
    }
    double objectiveMagnitude=1.0;
    for(const auto& entry:t.objective) objectiveMagnitude=std::max(objectiveMagnitude,std::abs(entry.second));
    for(const auto& entry:t.quadratic) objectiveMagnitude=std::max(objectiveMagnitude,std::abs(entry.second));
    if(objectiveMagnitude>1.0){
      for(auto& entry:t.objective) entry.second/=objectiveMagnitude;
      for(auto& entry:t.quadratic) entry.second/=objectiveMagnitude;
    }
    const double scalingTimeMs=std::chrono::duration<double,std::milli>(Clock::now()-scalingStart).count();

    t.rebuildMappings();
    const auto solverStart=std::chrono::steady_clock::now();
    auto r=QPInteriorPoint{tol_}.solve(t,limit);
    r.convexityCheckTimeMs+=convexityTimeMs;
    r.transformationTimeMs=transformationTimeMs;
    r.scalingTimeMs=scalingTimeMs;
    if(auto* context=cuda::Context::defaultContext()) context->synchronize();
    r.solverInvoked=true;
    r.solverTimeMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-solverStart).count();
    if(r.x.size()<n){
      r.status=QPStatus::NumericalFailure;
      r.message="standard-form solution has wrong dimension";
      return r;
    }
    const auto postsolveStart=std::chrono::steady_clock::now();
    const auto standardPoint=r.x;
    const auto standardBoundDuals=r.nonnegativeDuals;
    const auto standardRowDuals=r.constraintDuals;
    std::vector<double> original(n);
    for(std::size_t i=0;i<n;++i) original[i]=offset[i]+sign[i]*scale[i]*standardPoint[i];
    r.x=original;
    r.objectiveValue=m.objectiveConstant;
    for(std::size_t i=0;i<n;++i){
      const double q=m.quadratic.count(i)?m.quadratic.at(i):0.0;
      const double c=m.objective.count(i)?m.objective.at(i):0.0;
      r.objectiveValue+=.5*q*r.x[i]*r.x[i]+c*r.x[i];
    }
    r.postsolveTimeMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-postsolveStart).count();
    const auto verificationStart=std::chrono::steady_clock::now();
    r.constraintDuals.assign(m.constraints.size(),0.0);
    r.lowerBoundDuals.assign(n,0.0);
    r.upperBoundDuals.assign(n,0.0);
    for(std::size_t i=0;i<m.constraints.size()&&i<standardRowDuals.size();++i)
      r.constraintDuals[i]=objectiveMagnitude*standardRowDuals[i]/rowScale[i];
    for(std::size_t i=0;i<n&&i<standardBoundDuals.size();++i){
      const double multiplier=objectiveMagnitude*standardBoundDuals[i]/scale[i];
      if(std::isfinite(m.variables[i].lower))r.lowerBoundDuals[i]=multiplier;
      if(sign[i]<0.0)r.upperBoundDuals[i]=multiplier;
      const auto slack=upperSlackVariable[i];
      if(slack!=std::numeric_limits<std::size_t>::max()&&slack<standardBoundDuals.size())
        r.upperBoundDuals[i]=objectiveMagnitude*standardBoundDuals[slack]/scale[slack];
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
    r.dualResidual=0.0;
    for(std::size_t j=0;j<n;++j){
      const double gradient=(m.objective.count(j)?m.objective.at(j):0.0)+(m.quadratic.count(j)?m.quadratic.at(j)*r.x[j]:0.0);
      double stationarity=gradient,normalizer=1.0+std::abs(gradient);
      for(std::size_t i=0;i<m.constraints.size();++i){
        const auto coefficient=m.constraints[i].coefficients.find(j);
        if(coefficient==m.constraints[i].coefficients.end())continue;
        const double term=coefficient->second*r.constraintDuals[i];
        stationarity-=term;normalizer+=std::abs(term);
      }
      stationarity-=r.lowerBoundDuals[j];stationarity+=r.upperBoundDuals[j];
      normalizer+=r.lowerBoundDuals[j]+r.upperBoundDuals[j];
      r.dualResidual=std::max(r.dualResidual,std::abs(stationarity)/normalizer);
    }
    r.complementarity=0.0;
    double objectiveScale=1.0+std::abs(m.objectiveConstant);
    for(std::size_t i=0;i<n;++i){
      // Scale complementarity against the magnitude of the objective terms
      // individually; summing first can under-scale when quadratic and linear
      // contributions nearly cancel.
      objectiveScale+=std::abs(m.quadratic.count(i)?0.5*m.quadratic.at(i)*r.x[i]*r.x[i]:0.0)
                     +std::abs(m.objective.count(i)?m.objective.at(i)*r.x[i]:0.0);
    }
    for(std::size_t i=0;i<m.constraints.size();++i){
      const auto& c=m.constraints[i];
      double lhs=0.0;for(const auto& term:c.coefficients)lhs+=term.second*r.x[term.first];
      const double dual=r.constraintDuals[i];
      if(c.relation==Relation::LessEqual){
        r.dualResidual=std::max(r.dualResidual,std::max(0.0,dual)/(1.0+std::abs(dual)));
        const double slack=std::max(0.0,c.rhs-lhs);
        r.complementarity=std::max(r.complementarity,std::abs(dual*slack));
      }else if(c.relation==Relation::GreaterEqual){
        r.dualResidual=std::max(r.dualResidual,std::max(0.0,-dual)/(1.0+std::abs(dual)));
        const double slack=std::max(0.0,lhs-c.rhs);
        r.complementarity=std::max(r.complementarity,std::abs(dual*slack));
      }
    }
    for(std::size_t i=0;i<n;++i){
      if(std::isfinite(m.variables[i].lower)){
        const double slack=std::max(0.0,r.x[i]-m.variables[i].lower),dual=std::max(0.0,r.lowerBoundDuals[i]);
        r.complementarity=std::max(r.complementarity,std::abs(dual*slack));
      }
      if(std::isfinite(m.variables[i].upper)){
        const double slack=std::max(0.0,m.variables[i].upper-r.x[i]),dual=std::max(0.0,r.upperBoundDuals[i]);
        r.complementarity=std::max(r.complementarity,std::abs(dual*slack));
      }
    }
    r.complementarity/=objectiveScale;
    r.kktVerified=r.primalResidual<=tol_.feasibility&&r.dualResidual<=tol_.optimality&&r.complementarity<=tol_.convergence;
    if(r.status==QPStatus::Optimal&&!r.kktVerified){
      r.status=QPStatus::NumericalFailure;
      r.message="original-model KKT verification failed";
    }else if(r.status==QPStatus::Optimal){
      r.message="original-model KKT verification passed";
    }
    r.verificationTimeMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-verificationStart).count();
    return r;
  }
 private:
  Tolerance tol_;
};
}
