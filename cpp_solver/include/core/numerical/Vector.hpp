#pragma once
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>
namespace sovereign::nla {
class VectorOps {
 public:
  using Vector=std::vector<double>;
  static void sameSize(const Vector&a,const Vector&b){if(a.size()!=b.size())throw std::invalid_argument("vector dimension mismatch");}
  static Vector add(const Vector&a,const Vector&b){sameSize(a,b);Vector r(a.size());for(size_t i=0;i<a.size();++i)r[i]=a[i]+b[i];return r;}
  static Vector subtract(const Vector&a,const Vector&b){sameSize(a,b);Vector r(a.size());for(size_t i=0;i<a.size();++i)r[i]=a[i]-b[i];return r;}
  static Vector scale(const Vector&a,double s){Vector r=a;for(double&v:r)v*=s;return r;}
  static double dot(const Vector&a,const Vector&b){sameSize(a,b);double sum=0,c=0;for(size_t i=0;i<a.size();++i){double y=a[i]*b[i]-c,t=sum+y;c=(t-sum)-y;sum=t;}return sum;}
  static double norm1(const Vector&a){double s=0;for(double x:a)s+=std::abs(x);return s;}
  static double norm2(const Vector&a){return std::sqrt(std::max(0.0,dot(a,a)));}
  static double normInf(const Vector&a){double v=0;for(double x:a)v=std::max(v,std::abs(x));return v;}
  static Vector elementMultiply(const Vector&a,const Vector&b){sameSize(a,b);Vector r(a.size());for(size_t i=0;i<a.size();++i)r[i]=a[i]*b[i];return r;}
  static bool elementDivide(const Vector&a,const Vector&b,Vector&r,double zeroTol){sameSize(a,b);r.resize(a.size());for(size_t i=0;i<a.size();++i){if(std::abs(b[i])<=zeroTol)return false;r[i]=a[i]/b[i];if(!std::isfinite(r[i]))return false;}return true;}
  static Vector absolute(const Vector&a){Vector r=a;for(double&v:r)v=std::abs(v);return r;}
  static double min(const Vector&a){if(a.empty())throw std::invalid_argument("min of empty vector");return *std::min_element(a.begin(),a.end());}
  static double max(const Vector&a){if(a.empty())throw std::invalid_argument("max of empty vector");return *std::max_element(a.begin(),a.end());}
  static void axpy(double alpha,const Vector&x,Vector&y){sameSize(x,y);for(size_t i=0;i<x.size();++i)y[i]+=alpha*x[i];}
};
}
