#pragma once
#include "model/Model.hpp"
#include "qp/Convexity.hpp"
#include "core/Tolerance.hpp"
#include "core/LinearSystem.hpp"
#include "core/numerical/Vector.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace sovereign {
enum class QPStatus { Optimal, IterationLimit, NumericalFailure, UnsupportedNonconvex, Unsupported };
struct QPResult {
  QPStatus status{QPStatus::NumericalFailure};
  std::vector<double> x;
  double objectiveValue=0,primalResidual=0,dualResidual=0,complementarity=0;
  double solverTimeMs=0,postsolveTimeMs=0,verificationTimeMs=0;
  bool solverInvoked=false;
  std::size_t iterations=0;
  std::string message;
};

inline bool qpLinearSolve(const std::vector<std::vector<double>>& a,const std::vector<double>& b,std::vector<double>& x,double eps){
  Tolerance t;
  t.pivot=eps;
  t.singularity=eps;
  if(a.size()!=b.size()) return false;
  nla::DenseMatrix matrix(a.size(),a.size());
  for(std::size_t i=0;i<a.size();++i){
    if(a[i].size()!=a.size()) return false;
    for(std::size_t j=0;j<a.size();++j) matrix(i,j)=a[i][j];
  }
  return nla::KKTLinearSolver(t).solve(matrix,b,x);
}

class QPInteriorPoint {
 public:
  explicit QPInteriorPoint(Tolerance t={}):tol_(t){}

  QPResult solve(const Model& m,std::size_t limit=100) const {
    QPResult out;
    const auto convexity=checkQPConvexity(m,tol_);
    if(!convexity.isConvex){
      out.status=convexity.classification==QPConvexity::Indefinite ? QPStatus::UnsupportedNonconvex : QPStatus::NumericalFailure;
      out.message=convexity.message;
      return out;
    }

    for(const auto& variable:m.variables) if(variable.lower!=0||std::isfinite(variable.upper)){
      out.status=QPStatus::Unsupported;
      out.message="general inequality/bound QP transformation pending";
      return out;
    }
    for(const auto& row:m.constraints) if(row.relation!=Relation::Equal){
      out.status=QPStatus::Unsupported;
      out.message="QP IPM currently supports equality constraints only";
      return out;
    }
    for(const auto& row:m.quadraticMatrix) for(const auto& entry:row.second) if(row.first!=entry.first&&std::abs(entry.second)>tol_.zero){
      out.status=QPStatus::Unsupported;
      out.message="the sparse Schur QP path currently requires a diagonal Hessian";
      return out;
    }

    const std::size_t n=m.variables.size(),p=m.constraints.size();
    out.x.assign(n,1.0);
    std::vector<double> y(p,0.0),z(n,1.0);
    std::vector<std::vector<double>> a(p,std::vector<double>(n));
    std::vector<std::vector<std::pair<std::size_t,double>>> columns(n);
    std::vector<double> b(p);
    for(std::size_t i=0;i<p;++i){
      b[i]=m.constraints[i].rhs;
      for(const auto& coefficient:m.constraints[i].coefficients) if(coefficient.first<n){
        a[i][coefficient.first]=coefficient.second;
        columns[coefficient.first].push_back({i,coefficient.second});
      }
    }

    for(std::size_t iteration=0;iteration<limit;++iteration){
      std::vector<double> primalResidual(p),dualResidual(n),complementarityResidual(n);
      for(std::size_t i=0;i<p;++i){
        for(std::size_t j=0;j<n;++j) primalResidual[i]+=a[i][j]*out.x[j];
        primalResidual[i]-=b[i];
      }
      for(std::size_t j=0;j<n;++j){
        const double hessian=m.quadratic.count(j)?m.quadratic.at(j):0.0;
        dualResidual[j]=(m.objective.count(j)?m.objective.at(j):0.0)+hessian*out.x[j]-z[j];
        for(std::size_t i=0;i<p;++i) dualResidual[j]-=a[i][j]*y[i];
      }
      double mu=0.0;
      for(std::size_t j=0;j<n;++j) mu+=out.x[j]*z[j];
      mu/=std::max<std::size_t>(1,n);
      for(std::size_t j=0;j<n;++j) complementarityResidual[j]=out.x[j]*z[j]-0.2*mu;
      out.primalResidual=0.0;
      for(double value:primalResidual) out.primalResidual=std::max(out.primalResidual,std::abs(value));
      out.dualResidual=0.0;
      for(double value:dualResidual) out.dualResidual=std::max(out.dualResidual,std::abs(value));
      out.complementarity=mu;
      if(out.primalResidual<tol_.convergence&&out.dualResidual<tol_.optimality&&mu<=tol_.convergence){
        out.status=QPStatus::Optimal;
        out.iterations=iteration;
        out.message="KKT verification passed";
        break;
      }

      // Eliminate dx through the diagonal Newton block.  The old code formed
      // a dense (p + 2n) KKT matrix.  This Schur complement has only p rows
      // and is assembled from sparse A columns, which avoids multi-gigabyte
      // temporary allocations on large QPLIB models.
      constexpr double dualRegularization=1e-9;
      std::vector<std::vector<double>> schur(p,std::vector<double>(p));
      std::vector<double> schurRhs(p),inverseDiagonal(n),stationarityRhs(n);
      for(std::size_t i=0;i<p;++i){
        schur[i][i]=dualRegularization;
        schurRhs[i]=-primalResidual[i];
      }
      bool validDiagonal=true;
      for(std::size_t j=0;j<n;++j){
        const double hessian=m.quadratic.count(j)?m.quadratic.at(j):0.0;
        const double diagonal=hessian+z[j]/out.x[j];
        if(!(diagonal>0.0)||!std::isfinite(diagonal)){ validDiagonal=false; break; }
        inverseDiagonal[j]=1.0/diagonal;
        stationarityRhs[j]=-dualResidual[j]-complementarityResidual[j]/out.x[j];
        for(const auto& row:columns[j]) schurRhs[row.first]-=row.second*inverseDiagonal[j]*stationarityRhs[j];
        for(const auto& left:columns[j]) for(const auto& right:columns[j]) {
          schur[left.first][right.first]+=left.second*inverseDiagonal[j]*right.second;
        }
      }
      if(!validDiagonal){ out.status=QPStatus::NumericalFailure; out.message="invalid diagonal Newton block"; break; }
      std::vector<double> deltaY;
      if(!qpLinearSolve(schur,schurRhs,deltaY,tol_.pivot)){
        out.status=QPStatus::NumericalFailure;
        out.message="sparse Schur Newton system could not be solved";
        break;
      }
      std::vector<double> deltaX(n),deltaZ(n);
      for(std::size_t j=0;j<n;++j){
        double transposeTimesY=0.0;
        for(const auto& row:columns[j]) transposeTimesY+=row.second*deltaY[row.first];
        deltaX[j]=inverseDiagonal[j]*(stationarityRhs[j]+transposeTimesY);
        deltaZ[j]=(-complementarityResidual[j]-z[j]*deltaX[j])/out.x[j];
      }
      double alphaX=1.0,alphaZ=1.0;
      for(std::size_t j=0;j<n;++j){
        if(deltaX[j]<0.0) alphaX=std::min(alphaX,-out.x[j]/deltaX[j]);
        if(deltaZ[j]<0.0) alphaZ=std::min(alphaZ,-z[j]/deltaZ[j]);
      }
      const double alpha=.99*std::min({1.0,alphaX,alphaZ});
      if(!(alpha>0.0&&std::isfinite(alpha))){ out.status=QPStatus::NumericalFailure; out.message="invalid positivity step"; break; }
      for(std::size_t j=0;j<n;++j){ out.x[j]+=alpha*deltaX[j]; z[j]+=alpha*deltaZ[j]; }
      for(std::size_t i=0;i<p;++i) y[i]+=alpha*deltaY[i];
      out.iterations=iteration+1;
    }

    out.objectiveValue=0.0;
    for(std::size_t j=0;j<n;++j){
      const double hessian=m.quadratic.count(j)?m.quadratic.at(j):0.0;
      const double linear=m.objective.count(j)?m.objective.at(j):0.0;
      out.objectiveValue+=.5*hessian*out.x[j]*out.x[j]+linear*out.x[j];
    }
    if(out.status!=QPStatus::Optimal&&out.message.empty()){
      out.status=QPStatus::IterationLimit;
      out.message="maximum iterations reached";
    }
    return out;
  }

 private:
  Tolerance tol_;
};
}
