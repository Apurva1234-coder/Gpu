#pragma once
#include "core/numerical/Factorization.hpp"
#include <algorithm>
namespace sovereign::nla {
enum class Backend { CPU };
class DenseLinearSolver {
 public: explicit DenseLinearSolver(Tolerance t={}):tol_(t){}
  bool solve(const DenseMatrix&a,const VectorOps::Vector&b,VectorOps::Vector&x)const{if(a.rows()!=b.size()||a.cols()!=b.size())return false;const size_t n=b.size();if(!std::all_of(b.begin(),b.end(),[](double v){return std::isfinite(v);}))return false;VectorOps::Vector rs(n,1),cs(n,1),rhs=b;DenseMatrix e(n,n);
    // Equilibrate rows and columns so pivot tests are meaningful for KKT
    // systems with very different primal/dual scales.
    // Do not use the model-space zero tolerance here: a row can be tiny in
    // absolute units yet perfectly well-conditioned after equilibration.
    const double smallest=std::numeric_limits<double>::min();
    for(size_t i=0;i<n;++i){double scale=0;for(size_t j=0;j<n;++j){if(!std::isfinite(a(i,j)))return false;scale=std::max(scale,std::abs(a(i,j)));}if(!(scale>smallest)||!std::isfinite(scale))return false;rs[i]=1.0/scale;rhs[i]*=rs[i];}
    for(size_t j=0;j<n;++j){double scale=0;for(size_t i=0;i<n;++i)scale=std::max(scale,std::abs(rs[i]*a(i,j)));if(!(scale>smallest)||!std::isfinite(scale))return false;cs[j]=1.0/scale;}
    for(size_t i=0;i<n;++i)for(size_t j=0;j<n;++j)e(i,j)=rs[i]*a(i,j)*cs[j];VectorOps::Vector u;LUFactorization lu;
    // A balanced KKT matrix may legitimately have small elimination pivots
    // near an LP optimum. Let the original-system residual test decide whether
    // the computed solution is useful instead of rejecting at model-scale tol.
    Tolerance factorTol=tol_;factorTol.pivot=std::min(factorTol.pivot,1e-18);factorTol.singularity=std::min(factorTol.singularity,1e-18);
    if(!lu.factor(e,factorTol)||!lu.solve(rhs,u))return false;x.resize(n);for(size_t j=0;j<n;++j)x[j]=cs[j]*u[j];if(!std::all_of(x.begin(),x.end(),[](double v){return std::isfinite(v);}))return false;
    auto ax=a.multiply(x);auto r=VectorOps::subtract(ax,b);double scale=1+VectorOps::normInf(b);for(size_t i=0;i<a.rows();++i){double row=0;for(size_t j=0;j<a.cols();++j)row+=std::abs(a(i,j));scale=std::max(scale,1+row*VectorOps::normInf(x)+VectorOps::normInf(b));}return VectorOps::normInf(r)<=tol_.linearResidual*scale;}
 private:Tolerance tol_;
};
// Stable call boundary for future CPU/CUDA dispatch; CPU is the only backend today.
class KKTLinearSolver {public:explicit KKTLinearSolver(Tolerance t={},Backend b=Backend::CPU):solver_(t),backend_(b){}bool solve(const DenseMatrix&a,const VectorOps::Vector&b,VectorOps::Vector&x)const{return backend_==Backend::CPU&&solver_.solve(a,b,x);}private:DenseLinearSolver solver_;Backend backend_;};
}
