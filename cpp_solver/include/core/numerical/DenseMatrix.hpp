#pragma once
#include "core/numerical/Vector.hpp"
#include <stdexcept>
namespace sovereign::nla {
class DenseMatrix {
 public:
  DenseMatrix()=default;DenseMatrix(size_t r,size_t c,double value=0):rows_(r),cols_(c),data_(r*c,value){}
  size_t rows()const{return rows_;}size_t cols()const{return cols_;}
  double& operator()(size_t r,size_t c){if(r>=rows_||c>=cols_)throw std::out_of_range("matrix index");return data_[r*cols_+c];}
  double operator()(size_t r,size_t c)const{if(r>=rows_||c>=cols_)throw std::out_of_range("matrix index");return data_[r*cols_+c];}
  const std::vector<double>& data()const{return data_;}
  VectorOps::Vector multiply(const VectorOps::Vector&x)const{if(x.size()!=cols_)throw std::invalid_argument("matrix-vector dimension mismatch");VectorOps::Vector y(rows_);for(size_t i=0;i<rows_;++i){double s=0;for(size_t j=0;j<cols_;++j)s+=(*this)(i,j)*x[j];y[i]=s;}return y;}
  VectorOps::Vector transposeMultiply(const VectorOps::Vector&x)const{if(x.size()!=rows_)throw std::invalid_argument("transpose matrix-vector dimension mismatch");VectorOps::Vector y(cols_);for(size_t i=0;i<rows_;++i)for(size_t j=0;j<cols_;++j)y[j]+=(*this)(i,j)*x[i];return y;}
  DenseMatrix transpose()const{DenseMatrix t(cols_,rows_);for(size_t i=0;i<rows_;++i)for(size_t j=0;j<cols_;++j)t(j,i)=(*this)(i,j);return t;}
  DenseMatrix add(const DenseMatrix&b)const{if(rows_!=b.rows_||cols_!=b.cols_)throw std::invalid_argument("matrix dimension mismatch");DenseMatrix c(rows_,cols_);for(size_t i=0;i<data_.size();++i)c.data_[i]=data_[i]+b.data_[i];return c;}
 private:size_t rows_=0,cols_=0;std::vector<double>data_;
};
}
