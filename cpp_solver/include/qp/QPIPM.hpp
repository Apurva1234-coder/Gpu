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
#include <vector>
namespace sovereign {
enum class QPStatus { Optimal, IterationLimit, NumericalFailure, UnsupportedNonconvex, Unsupported };
struct QPResult { QPStatus status{QPStatus::NumericalFailure}; std::vector<double> x; double objectiveValue=0, primalResidual=0, dualResidual=0, complementarity=0; std::size_t iterations=0; std::string message; };
inline bool qpFinite(const std::vector<double>& v){for(double x:v)if(!std::isfinite(x))return false;return true;}
inline bool qpLinearSolve(std::vector<std::vector<double>> a,std::vector<double> b,std::vector<double>& x,double eps){Tolerance t;t.pivot=eps;t.singularity=eps;nla::DenseMatrix matrix(a.size(),a.size());if(a.size()!=b.size())return false;for(size_t i=0;i<a.size();++i){if(a[i].size()!=a.size())return false;for(size_t j=0;j<a.size();++j)matrix(i,j)=a[i][j];}return nla::KKTLinearSolver(t).solve(matrix,b,x);}
class QPInteriorPoint { public: explicit QPInteriorPoint(Tolerance t={}):tol_(t){}
 QPResult solve(const Model& m,size_t limit=100) const { QPResult o;auto cv=checkQPConvexity(m,tol_);if(!cv.isConvex){o.status=cv.classification==QPConvexity::Indefinite?QPStatus::UnsupportedNonconvex:QPStatus::NumericalFailure;o.message=cv.message;return o;}bool general=false;for(auto&v:m.variables)if(v.lower!=0||std::isfinite(v.upper))general=true;for(auto&c:m.constraints)if(c.relation!=Relation::Equal)general=true;if(general){o.status=QPStatus::Unsupported;o.message="general inequality/bound QP transformation pending";return o;}size_t n=m.variables.size(),p=0;for(auto&c:m.constraints){if(c.relation!=Relation::Equal){o.status=QPStatus::Unsupported;o.message="QP IPM currently supports equality constraints only";return o;}++p;}o.x.assign(n,1.0);std::vector<double> y(p,0),z(n,1.0);for(size_t i=0;i<n;++i)if(std::isfinite(m.variables[i].lower))o.x[i]=std::max(1.0,m.variables[i].lower+1);std::vector<std::vector<double>> A(p,std::vector<double>(n));std::vector<double>b(p);for(size_t i=0;i<p;++i){b[i]=m.constraints[i].rhs;for(auto q:m.constraints[i].coefficients)if(q.first<n)A[i][q.first]=q.second;}
  for(size_t it=0;it<limit;++it){std::vector<double> rp(p),rd(n),rc(n);for(size_t i=0;i<p;++i){for(size_t j=0;j<n;++j)rp[i]+=A[i][j]*o.x[j];rp[i]-=b[i];}for(size_t j=0;j<n;++j){rd[j]=m.objective.count(j)?m.objective.at(j):0;for(size_t k=0;k<n;++k)rd[j]+=(m.quadraticMatrix.count(j)&&m.quadraticMatrix.at(j).count(k)?m.quadraticMatrix.at(j).at(k):j==k&&m.quadratic.count(j)?m.quadratic.at(j):0)*o.x[k];for(size_t i=0;i<p;++i)rd[j]-=A[i][j]*y[i];rd[j]-=z[j];}double mu=0;for(size_t j=0;j<n;++j)mu+=o.x[j]*z[j];mu/=std::max<size_t>(1,n);for(size_t j=0;j<n;++j)rc[j]=o.x[j]*z[j]-std::max(tol_.convergence,0.2*mu);o.primalResidual=0;for(double v:rp)o.primalResidual=std::max(o.primalResidual,std::abs(v));o.dualResidual=0;for(double v:rd)o.dualResidual=std::max(o.dualResidual,std::abs(v));o.complementarity=mu;if(o.primalResidual<tol_.convergence&&o.dualResidual<tol_.convergence&&mu<=tol_.convergence){o.status=QPStatus::Optimal;o.iterations=it;o.message="KKT verification passed";break;}
   size_t N=p+2*n;std::vector<std::vector<double>> K(N,std::vector<double>(N));std::vector<double> rhs(N);for(size_t i=0;i<p;++i){for(size_t j=0;j<n;++j)K[i][j]=A[i][j];rhs[i]=-rp[i];}for(size_t j=0;j<n;++j){size_t r=p+j;for(size_t k=0;k<n;++k)K[r][k]=(m.quadraticMatrix.count(j)&&m.quadraticMatrix.at(j).count(k)?m.quadraticMatrix.at(j).at(k):j==k&&m.quadratic.count(j)?m.quadratic.at(j):0);for(size_t i=0;i<p;++i)K[r][n+i]=-A[i][j];K[r][p+n+j]=-1;rhs[r]=-rd[j];size_t cr=p+n+j;K[cr][j]=z[j];K[cr][p+n+j]=o.x[j];rhs[cr]=-rc[j];}std::vector<double>d;if(!qpLinearSolve(K,rhs,d,tol_.pivot)){o.status=QPStatus::NumericalFailure;o.message="singular Newton/KKT system";break;}double ax=1,az=1;for(size_t j=0;j<n;++j){if(d[j]<0)ax=std::min(ax,-o.x[j]/d[j]);if(d[p+n+j]<0)az=std::min(az,-z[j]/d[p+n+j]);}double a=.99*std::min({1.0,ax,az});if(!(a>0&&std::isfinite(a))){o.status=QPStatus::NumericalFailure;o.message="invalid positivity step";break;}for(size_t j=0;j<n;++j){o.x[j]+=a*d[j];z[j]+=a*d[p+n+j];}for(size_t i=0;i<p;++i)y[i]+=a*d[n+i];o.iterations=it+1;}
  o.objectiveValue=0;for(size_t j=0;j<n;++j){o.objectiveValue+=.5*(m.quadratic.count(j)?m.quadratic.at(j):0)*o.x[j]*o.x[j]+(m.objective.count(j)?m.objective.at(j):0)*o.x[j];}if(o.status!=QPStatus::Optimal&&o.message.empty()){o.status=QPStatus::IterationLimit;o.message="maximum iterations reached";}return o; }
 private: Tolerance tol_;};
}






