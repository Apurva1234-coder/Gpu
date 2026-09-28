#pragma once
#include "lp/LPSolver.hpp"
#include "core/LinearSystem.hpp"
#include <algorithm>
#include <cmath>
namespace sovereign {
class MehrotraIPM {
 Tolerance tol_;
 static double dot(const std::vector<double>&a,const std::vector<double>&b){auto* gpu=cuda::Context::defaultContext();return gpu&&gpu->available()?gpu->dot(a.data(),b.data(),a.size()):nla::VectorOps::dot(a,b);}
 static double norm(const std::vector<double>&a){return nla::VectorOps::norm2(a);}
 static double step(const std::vector<double>&x,const std::vector<double>&d){double a=1;for(size_t i=0;i<x.size();++i)if(d[i]<0)a=std::min(a,-x[i]/d[i]);return std::max(0.,std::min(1.,a));}
public:
 explicit MehrotraIPM(Tolerance t={}):tol_(t){}
 LPResult solve(const Model&m,std::size_t limit=10000)const{
  LPResult o;o.method="ipm";for(const auto&v:m.variables)if(v.type!=VariableType::Continuous){o.status=LPStatus::Unsupported;return o;}if(!m.quadratic.empty()){o.status=LPStatus::Unsupported;return o;}
  StandardLP s=standardize(m,tol_);size_t n0=s.c.size(),p=s.A.size();if(!n0){o.status=LPStatus::NumericalFailure;return o;}size_t n=n0+p;std::vector<std::vector<double>>A(p,std::vector<double>(n));for(size_t i=0;i<p;++i){for(size_t j=0;j<n0;++j)A[i][j]=s.A[i][j];A[i][n0+i]=1;}s.c.resize(n,0);
  // standardize() stores an equivalent MAX objective, while these Newton
  // equations use the minimization convention A^T y + z - c = 0.
  // Negate the standardized objective before forming the KKT residual.
  for(double& cj:s.c)cj=-cj;s.A=std::move(A);std::vector<double>x(n,1),z(n,1),y(p),dx,dy,dz;
  auto res=[&](std::vector<double>&rp,std::vector<double>&rd){rp.assign(p,0);rd.assign(n,0);for(size_t i=0;i<p;++i){rp[i]=-s.b[i];for(size_t j=0;j<n;++j)rp[i]+=s.A[i][j]*x[j];}for(size_t j=0;j<n;++j){rd[j]=-s.c[j]+z[j];for(size_t i=0;i<p;++i)rd[j]+=s.A[i][j]*y[i];}};
  // With rd = A^T y + z - c and rc = XZ e, elimination gives
  // dx = (X rd - rc) ./ z + (x ./ z) .* (A^T dy). The first term
  // below is already divided by z; do not scale it a second time.
  auto dir=[&](const std::vector<double>&rp,const std::vector<double>&rd,const std::vector<double>&rc,std::vector<double>&ox,std::vector<double>&oy,std::vector<double>&oz){std::vector<std::vector<double>>K(p,std::vector<double>(p));std::vector<double>b(p),w(n);for(size_t j=0;j<n;++j){if(!(z[j]>0)||!std::isfinite(z[j]))return false;w[j]=(-rc[j]+x[j]*rd[j])/z[j];if(!std::isfinite(w[j]))return false;}for(size_t i=0;i<p;++i)for(size_t k=0;k<p;++k)for(size_t j=0;j<n;++j)K[i][k]+=s.A[i][j]*x[j]/z[j]*s.A[k][j];for(size_t i=0;i<p;++i){b[i]=-rp[i];for(size_t j=0;j<n;++j)b[i]-=s.A[i][j]*w[j];}
   // Near the solution, X/Z spans many orders of magnitude and the reduced
   // normal equations lose numerical rank. Apply a tiny, scale-relative ridge.
   double diagScale=0;for(size_t i=0;i<p;++i)diagScale=std::max(diagScale,std::abs(K[i][i]));if(!(diagScale>0)||!std::isfinite(diagScale))return false;for(size_t i=0;i<p;++i)K[i][i]+=1e-12*diagScale;
   if(!LinearSystem::solve(K,b,oy,tol_))return false;ox.assign(n,0);oz.assign(n,0);for(size_t j=0;j<n;++j){double q=0;for(size_t i=0;i<p;++i)q+=s.A[i][j]*oy[i];ox[j]=w[j]+x[j]*q/z[j];oz[j]=-rd[j]-q;if(!std::isfinite(ox[j])||!std::isfinite(oz[j]))return false;}return true;};
  for(size_t it=0;it<limit;++it){std::vector<double>rp,rd;res(rp,rd);double mu=dot(x,z)/n;if(norm(rp)<tol_.convergence&&norm(rd)<tol_.optimality&&mu<tol_.convergence){o.status=LPStatus::Optimal;o.iterations=it;break;}std::vector<double>rc(n);for(size_t j=0;j<n;++j)rc[j]=x[j]*z[j];if(!dir(rp,rd,rc,dx,dy,dz)){o.status=LPStatus::NumericalFailure;o.iterations=it;o.message="predictor Newton system failed";return o;}double af=std::min(step(x,dx),step(z,dz)),mua=0;for(size_t j=0;j<n;++j)mua+=(x[j]+af*dx[j])*(z[j]+af*dz[j]);mua/=n;double sig=std::pow(std::max(0.,mua/mu),3);for(size_t j=0;j<n;++j)rc[j]=x[j]*z[j]+dx[j]*dz[j]-sig*mu;if(!dir(rp,rd,rc,dx,dy,dz)){o.status=LPStatus::NumericalFailure;o.iterations=it;o.message="corrector Newton system failed";return o;}double a=.995*std::min(step(x,dx),step(z,dz));if(!(a>0)){o.status=LPStatus::NumericalFailure;o.iterations=it;o.message="non-positive fraction-to-boundary step";return o;}for(size_t j=0;j<n;++j){x[j]+=a*dx[j];z[j]+=a*dz[j];}for(size_t i=0;i<p;++i)y[i]+=a*dy[i];o.iterations=it+1;}
  if(o.status!=LPStatus::Optimal){o.status=LPStatus::IterationLimit;return o;}o.solution.primal.assign(m.variables.size(),0);for(size_t j=0;j<n0;++j)o.solution.primal[s.map[j].first]+=s.map[j].second*x[j];for(size_t i=0;i<m.variables.size();++i)if(m.variables[i].active&&std::isfinite(m.variables[i].lower))o.solution.primal[i]+=m.variables[i].lower;o.objectiveValue=evaluateObjective(m,o.solution.primal);o.solution.objectiveValue=o.objectiveValue;return o;
 }
};}


