#pragma once
#include "model/Model.hpp"
#include "qp/Convexity.hpp"
#include "core/Tolerance.hpp"
#include "core/LinearSystem.hpp"
#include "core/numerical/Vector.hpp"
#include <algorithm>
#include <chrono>
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
  std::vector<double> constraintDuals,nonnegativeDuals,lowerBoundDuals,upperBoundDuals;
  double objectiveValue=0,primalResidual=0,dualResidual=0,complementarity=0;
  double solverTimeMs=0,postsolveTimeMs=0,verificationTimeMs=0;
  double convexityCheckTimeMs=0,transformationTimeMs=0,scalingTimeMs=0,initializationTimeMs=0,residualComputationTimeMs=0;
  double newtonAssemblyTimeMs=0,linearSystemBuildTimeMs=0,linearSolveTimeMs=0,updateTimeMs=0;
  bool solverInvoked=false;
  bool kktVerified=false;
  std::size_t iterations=0,linearSystemSolves=0,linearSolverIterations=0;
  std::string message;
};

inline bool qpLinearSolve(const std::vector<std::vector<double>>& a,const std::vector<double>& b,std::vector<double>& x,double eps,double* buildMs=nullptr,double* solveMs=nullptr){
  using Clock=std::chrono::steady_clock;
  const auto buildStart=Clock::now();
  Tolerance t;
  t.pivot=eps;
  t.singularity=eps;
  if(a.size()!=b.size()) return false;
  nla::DenseMatrix matrix(a.size(),a.size());
  for(std::size_t i=0;i<a.size();++i){
    if(a[i].size()!=a.size()) return false;
    for(std::size_t j=0;j<a.size();++j) matrix(i,j)=a[i][j];
  }
  if(buildMs) *buildMs+=std::chrono::duration<double,std::milli>(Clock::now()-buildStart).count();
  const auto solveStart=Clock::now();
  const bool solved=nla::KKTLinearSolver(t).solve(matrix,b,x);
  if(solveMs) *solveMs+=std::chrono::duration<double,std::milli>(Clock::now()-solveStart).count();
  return solved;
}

inline bool qpSparseSchurSolve(
    const std::vector<std::vector<std::pair<std::size_t,double>>>& columns,
    const std::vector<double>& inverseDiagonal,
    const std::vector<double>& rhs,
    std::vector<double>& solution,
    double relativeTolerance,
    std::size_t* iterations=nullptr) {
  const std::size_t rows=rhs.size();
  constexpr double regularization=1e-9;
  if(columns.size()!=inverseDiagonal.size()) return false;
  std::vector<double> diagonal(rows,regularization);
  for(std::size_t j=0;j<columns.size();++j) for(const auto& entry:columns[j]) {
    if(entry.first>=rows) return false;
    diagonal[entry.first]+=entry.second*entry.second*inverseDiagonal[j];
  }
  for(double value:diagonal) if(!(value>0.0)||!std::isfinite(value)) return false;
  auto multiply=[&](const std::vector<double>& vector,std::vector<double>& product){
    product.assign(rows,0.0);
    for(std::size_t i=0;i<rows;++i) product[i]=regularization*vector[i];
    for(std::size_t j=0;j<columns.size();++j){
      double dot=0.0;
      for(const auto& entry:columns[j]) dot+=entry.second*vector[entry.first];
      const double scaled=inverseDiagonal[j]*dot;
      for(const auto& entry:columns[j]) product[entry.first]+=entry.second*scaled;
    }
  };
  const auto normSquared=[](const std::vector<double>& values){double sum=0.0;for(double v:values)sum+=v*v;return sum;};
  solution.assign(rows,0.0);
  std::vector<double> residual=rhs,preconditioned(rows),direction(rows),product;
  const double rhsNorm=std::sqrt(normSquared(rhs));
  const double target=std::max(1e-13,relativeTolerance)*std::max(1.0,rhsNorm);
  if(rhsNorm<=target) return true;
  for(std::size_t i=0;i<rows;++i) preconditioned[i]=residual[i]/diagonal[i];
  direction=preconditioned;
  double rz=0.0;for(std::size_t i=0;i<rows;++i)rz+=residual[i]*preconditioned[i];
  const std::size_t maximumIterations=std::max<std::size_t>(100,std::min<std::size_t>(10000,4*rows));
  for(std::size_t iteration=0;iteration<maximumIterations;++iteration){
    multiply(direction,product);
    double curvature=0.0;for(std::size_t i=0;i<rows;++i)curvature+=direction[i]*product[i];
    if(!(curvature>0.0)||!std::isfinite(curvature)||!std::isfinite(rz)) return false;
    const double alpha=rz/curvature;
    for(std::size_t i=0;i<rows;++i){solution[i]+=alpha*direction[i];residual[i]-=alpha*product[i];}
    if(iterations) *iterations=iteration+1;
    const double residualNorm=std::sqrt(normSquared(residual));
    if(residualNorm<=target) return true;
    for(std::size_t i=0;i<rows;++i) preconditioned[i]=residual[i]/diagonal[i];
    double nextRz=0.0;for(std::size_t i=0;i<rows;++i)nextRz+=residual[i]*preconditioned[i];
    if(!(nextRz>0.0)||!std::isfinite(nextRz)) return false;
    const double beta=nextRz/rz;
    for(std::size_t i=0;i<rows;++i)direction[i]=preconditioned[i]+beta*direction[i];
    rz=nextRz;
  }
  return false;
}

class QPInteriorPoint {
 public:
  explicit QPInteriorPoint(Tolerance t={}):tol_(t){}

  QPResult solve(const Model& m,std::size_t limit=100) const {
    using Clock=std::chrono::steady_clock;
    QPResult out;
    const auto convexityStart=Clock::now();
    const auto convexity=checkQPConvexity(m,tol_);
    out.convexityCheckTimeMs=std::chrono::duration<double,std::milli>(Clock::now()-convexityStart).count();
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
    const auto initializationStart=Clock::now();
    out.x.assign(n,1.0);
    std::vector<double> y(p,0.0),z(n,1.0);
    std::vector<std::vector<std::pair<std::size_t,double>>> columns(n);
    std::vector<std::vector<std::pair<std::size_t,double>>> rows(p);
    std::vector<double> b(p);
    for(std::size_t i=0;i<p;++i){
      b[i]=m.constraints[i].rhs;
      for(const auto& coefficient:m.constraints[i].coefficients) if(coefficient.first<n){
        columns[coefficient.first].push_back({i,coefficient.second});
        rows[i].push_back({coefficient.first,coefficient.second});
      }
    }
    out.initializationTimeMs=std::chrono::duration<double,std::milli>(Clock::now()-initializationStart).count();

    for(std::size_t iteration=0;iteration<limit;++iteration){
      const auto residualStart=Clock::now();
      std::vector<double> primalResidual(p),dualResidual(n),complementarityResidual(n);
      for(std::size_t i=0;i<p;++i){
        for(const auto& entry:rows[i]) primalResidual[i]+=entry.second*out.x[entry.first];
        primalResidual[i]-=b[i];
      }
      for(std::size_t j=0;j<n;++j){
        const double hessian=m.quadratic.count(j)?m.quadratic.at(j):0.0;
        dualResidual[j]=(m.objective.count(j)?m.objective.at(j):0.0)+hessian*out.x[j]-z[j];
        for(const auto& entry:columns[j]) dualResidual[j]-=entry.second*y[entry.first];
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
      out.residualComputationTimeMs+=std::chrono::duration<double,std::milli>(Clock::now()-residualStart).count();
      if(out.primalResidual<tol_.convergence&&out.dualResidual<tol_.optimality&&mu<=tol_.convergence){
        out.status=QPStatus::Optimal;
        out.iterations=iteration;
        out.message="KKT verification passed";
        break;
      }

      // Eliminate dx through the diagonal Newton block and solve the sparse
      // Schur system matrix-free for larger models. Building/factoring the
      // dense Schur matrix is cubic in the number of equality constraints.
      const auto assemblyStart=Clock::now();
      std::vector<double> schurRhs(p),inverseDiagonal(n),stationarityRhs(n);
      for(std::size_t i=0;i<p;++i){
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
      }
      if(!validDiagonal){ out.status=QPStatus::NumericalFailure; out.message="invalid diagonal Newton block"; break; }
      out.newtonAssemblyTimeMs+=std::chrono::duration<double,std::milli>(Clock::now()-assemblyStart).count();
      std::vector<double> deltaY;
      const auto linearSolveStart=Clock::now();
      bool linearSolved=false;
      if(p==0){
        deltaY.clear();linearSolved=true;
      } else if(p>64){
        ++out.linearSystemSolves;
        std::size_t pcgIterations=0;
        linearSolved=qpSparseSchurSolve(columns,inverseDiagonal,schurRhs,deltaY,tol_.linearResidual,&pcgIterations);
        out.linearSolverIterations+=pcgIterations;
        out.linearSolveTimeMs+=std::chrono::duration<double,std::milli>(Clock::now()-linearSolveStart).count();
      } else {
        ++out.linearSystemSolves;
        constexpr double dualRegularization=1e-9;
        std::vector<std::vector<double>> schur(p,std::vector<double>(p));
        for(std::size_t i=0;i<p;++i)schur[i][i]=dualRegularization;
        for(std::size_t j=0;j<n;++j)for(const auto& left:columns[j])for(const auto& right:columns[j])schur[left.first][right.first]+=left.second*inverseDiagonal[j]*right.second;
        linearSolved=qpLinearSolve(schur,schurRhs,deltaY,tol_.pivot,&out.linearSystemBuildTimeMs,&out.linearSolveTimeMs);
      }
      if(!linearSolved){
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
      const auto updateStart=Clock::now();
      for(std::size_t j=0;j<n;++j){ out.x[j]+=alpha*deltaX[j]; z[j]+=alpha*deltaZ[j]; }
      for(std::size_t i=0;i<p;++i) y[i]+=alpha*deltaY[i];
      out.updateTimeMs+=std::chrono::duration<double,std::milli>(Clock::now()-updateStart).count();
      out.iterations=iteration+1;
    }

    out.objectiveValue=m.objectiveConstant;
    for(std::size_t j=0;j<n;++j){
      const double hessian=m.quadratic.count(j)?m.quadratic.at(j):0.0;
      const double linear=m.objective.count(j)?m.objective.at(j):0.0;
      out.objectiveValue+=.5*hessian*out.x[j]*out.x[j]+linear*out.x[j];
    }
    if(out.status!=QPStatus::Optimal&&out.message.empty()){
      out.status=QPStatus::IterationLimit;
      out.message="maximum iterations reached";
    }
    out.constraintDuals=std::move(y);
    out.nonnegativeDuals=std::move(z);
    return out;
  }

 private:
  Tolerance tol_;
};
}
