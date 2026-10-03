#pragma once
#include "model/Model.hpp"
#include "core/Tolerance.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
namespace sovereign {
enum class QPConvexity { PositiveDefinite, PositiveSemidefinite, Indefinite, Invalid };
struct QPConvexityResult { QPConvexity classification{QPConvexity::Invalid}; bool isConvex=false,isPositiveDefinite=false,isPositiveSemidefinite=false; double minimumEigenvalue=0,maximumEigenvalue=0,tolerance=0; std::string message; };
inline const char* convexityName(QPConvexity c){return c==QPConvexity::PositiveDefinite?"POSITIVE DEFINITE":c==QPConvexity::PositiveSemidefinite?"POSITIVE SEMIDEFINITE":c==QPConvexity::Indefinite?"INDEFINITE":"INVALID";}
inline QPConvexityResult checkQPConvexity(const Model& m,const Tolerance& t={}) {
 QPConvexityResult r; r.tolerance=t.zero; const size_t n=m.variables.size();
 std::vector<double> diagonal(n,0.0);
 for(auto p:m.quadratic){if(p.first>=n||!std::isfinite(p.second)){r.message="invalid quadratic diagonal";return r;}diagonal[p.first]=p.second;}
 bool diagonalHessian=true;
 for(const auto& row:m.quadraticMatrix){if(row.first>=n){r.message="quadratic dimension mismatch";return r;}for(const auto& p:row.second){if(p.first>=n||!std::isfinite(p.second)){r.message="invalid quadratic entry";return r;}if(row.first==p.first)diagonal[row.first]=p.second;else if(std::abs(p.second)>t.zero)diagonalHessian=false;}}
 if(diagonalHessian){
   if(n==0){r.classification=QPConvexity::PositiveSemidefinite;r.isConvex=r.isPositiveSemidefinite=true;return r;}
   r.minimumEigenvalue=r.maximumEigenvalue=diagonal[0];
   for(size_t i=1;i<n;++i){r.minimumEigenvalue=std::min(r.minimumEigenvalue,diagonal[i]);r.maximumEigenvalue=std::max(r.maximumEigenvalue,diagonal[i]);}
   if(r.minimumEigenvalue>t.zero){r.classification=QPConvexity::PositiveDefinite;r.isPositiveDefinite=r.isConvex=true;r.message="QP Hessian is positive definite.";}
   else if(r.minimumEigenvalue>=-t.zero){r.classification=QPConvexity::PositiveSemidefinite;r.isPositiveSemidefinite=r.isConvex=true;r.message="QP Hessian is positive semidefinite.";}
   else{r.classification=QPConvexity::Indefinite;r.message="QP Hessian is not positive semidefinite; QP is non-convex.";}
   return r;
 }
 std::vector<std::vector<double>> a(n,std::vector<double>(n));
 for(auto p:m.quadratic){if(p.first>=n||!std::isfinite(p.second)){r.message="invalid quadratic diagonal";return r;}a[p.first][p.first]=p.second;}
 for(auto row:m.quadraticMatrix){if(row.first>=n){r.message="quadratic dimension mismatch";return r;}for(auto p:row.second){if(p.first>=n||!std::isfinite(p.second)){r.message="invalid quadratic entry";return r;}a[row.first][p.first]=p.second;}}
 for(size_t i=0;i<n;++i)for(size_t j=i+1;j<n;++j){if(std::abs(a[i][j]-a[j][i])>t.zero){r.message="QP Hessian is asymmetric";return r;}a[i][j]=a[j][i]=(a[i][j]+a[j][i])/2;}
 if(n==0){r.classification=QPConvexity::PositiveSemidefinite;r.isConvex=r.isPositiveSemidefinite=true;return r;}
 for(size_t z=0;z<100*n*n;++z){size_t p=0,q=0;double mx=0;for(size_t i=0;i<n;++i)for(size_t j=i+1;j<n;++j)if(std::abs(a[i][j])>mx){mx=std::abs(a[i][j]);p=i;q=j;}if(mx<=t.zero)break;double h=.5*std::atan2(2*a[p][q],a[q][q]-a[p][p]),c=std::cos(h),s=std::sin(h);for(size_t k=0;k<n;++k){double x=a[k][p],y=a[k][q];a[k][p]=c*x-s*y;a[k][q]=s*x+c*y;}for(size_t k=0;k<n;++k){double x=a[p][k],y=a[q][k];a[p][k]=c*x-s*y;a[q][k]=s*x+c*y;}}
 r.minimumEigenvalue=r.maximumEigenvalue=a[0][0];for(size_t i=1;i<n;++i){r.minimumEigenvalue=std::min(r.minimumEigenvalue,a[i][i]);r.maximumEigenvalue=std::max(r.maximumEigenvalue,a[i][i]);}
 if(!std::isfinite(r.minimumEigenvalue)||!std::isfinite(r.maximumEigenvalue)){r.message="eigenvalue decomposition failed";return r;}if(r.minimumEigenvalue>t.zero){r.classification=QPConvexity::PositiveDefinite;r.isPositiveDefinite=r.isConvex=true;r.message="QP Hessian is positive definite.";}else if(r.minimumEigenvalue>=-t.zero){r.classification=QPConvexity::PositiveSemidefinite;r.isPositiveSemidefinite=r.isConvex=true;r.message="QP Hessian is positive semidefinite.";}else{r.classification=QPConvexity::Indefinite;r.message="QP Hessian is not positive semidefinite; QP is non-convex.";}return r;
}
}
