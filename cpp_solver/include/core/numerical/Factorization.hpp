#pragma once
#include "core/numerical/DenseMatrix.hpp"
#include "core/Tolerance.hpp"
#include <cmath>
namespace sovereign::nla {
class LUFactorization {
 public:
  bool factor(const DenseMatrix& a,Tolerance t={}) {
    if(a.rows()!=a.cols())return false;lu_=a;n_=a.rows();pivots_.resize(n_);double scale=0;
    for(size_t i=0;i<n_;++i){pivots_[i]=i;double row=0;for(size_t j=0;j<n_;++j){if(!std::isfinite(a(i,j)))return false;row+=std::abs(a(i,j));}scale=std::max(scale,row);}
    threshold_=std::max(t.singularity,t.pivot)*std::max(1.0,scale);
    for(size_t k=0;k<n_;++k){size_t p=k;for(size_t i=k+1;i<n_;++i)if(std::abs(lu_(i,k))>std::abs(lu_(p,k)))p=i;if(std::abs(lu_(p,k))<=threshold_)return false;std::swap(pivots_[k],pivots_[p]);for(size_t j=0;j<n_;++j)std::swap(lu_(k,j),lu_(p,j));for(size_t i=k+1;i<n_;++i){lu_(i,k)/=lu_(k,k);for(size_t j=k+1;j<n_;++j)lu_(i,j)-=lu_(i,k)*lu_(k,j);}}
    return true;
  }
  bool solve(const VectorOps::Vector& b,VectorOps::Vector& x)const {
    if(b.size()!=n_)return false;x.resize(n_);for(size_t i=0;i<n_;++i)x[i]=b[pivots_[i]];
    for(size_t i=0;i<n_;++i)for(size_t j=0;j<i;++j)x[i]-=lu_(i,j)*x[j];
    for(size_t ii=0;ii<n_;++ii){size_t i=n_-1-ii;for(size_t j=i+1;j<n_;++j)x[i]-=lu_(i,j)*x[j];if(std::abs(lu_(i,i))<=threshold_)return false;x[i]/=lu_(i,i);if(!std::isfinite(x[i]))return false;}return true;
  }
 private:DenseMatrix lu_;size_t n_=0;std::vector<size_t>pivots_;double threshold_=0;
};
class CholeskyFactorization {
 public:
  bool factor(const DenseMatrix& a,Tolerance t={}) {
    if(a.rows()!=a.cols())return false;size_t n=a.rows();L_=DenseMatrix(n,n);double scale=0;
    for(size_t i=0;i<n;++i)for(size_t j=0;j<n;++j){if(!std::isfinite(a(i,j))||std::abs(a(i,j)-a(j,i))>t.zero)return false;scale=std::max(scale,std::abs(a(i,j)));}
    double threshold=std::max(t.singularity,t.pivot)*std::max(1.0,scale);
    for(size_t i=0;i<n;++i)for(size_t j=0;j<=i;++j){double s=a(i,j);for(size_t k=0;k<j;++k)s-=L_(i,k)*L_(j,k);if(i==j){if(!(s>threshold))return false;L_(i,j)=std::sqrt(s);}else L_(i,j)=s/L_(j,j);}return true;
  }
  bool solve(const VectorOps::Vector& b,VectorOps::Vector& x)const {
    size_t n=L_.rows();if(b.size()!=n)return false;x=b;for(size_t i=0;i<n;++i){for(size_t j=0;j<i;++j)x[i]-=L_(i,j)*x[j];x[i]/=L_(i,i);}for(size_t ii=0;ii<n;++ii){size_t i=n-1-ii;for(size_t j=i+1;j<n;++j)x[i]-=L_(j,i)*x[j];x[i]/=L_(i,i);if(!std::isfinite(x[i]))return false;}return true;
  }
 private:DenseMatrix L_;
};
}
