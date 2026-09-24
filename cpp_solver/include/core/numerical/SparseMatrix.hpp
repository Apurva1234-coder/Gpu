#pragma once
#include "core/numerical/Vector.hpp"
#include <algorithm>
#include <stdexcept>
namespace sovereign::nla {
struct Triplet {size_t row,col;double value;};
class CSRMatrix {
 public:
  CSRMatrix()=default;
  CSRMatrix(size_t rows,size_t cols,const std::vector<Triplet>&entries,double zeroTol=0):rows_(rows),cols_(cols),rowPtr_(rows+1,0){auto e=entries;for(const auto&t:e)if(t.row>=rows||t.col>=cols)throw std::out_of_range("CSR triplet index");std::sort(e.begin(),e.end(),[](const Triplet&a,const Triplet&b){return a.row!=b.row?a.row<b.row:a.col<b.col;});for(size_t k=0;k<e.size();){size_t j=k+1;double v=e[k].value;while(j<e.size()&&e[j].row==e[k].row&&e[j].col==e[k].col)v+=e[j++].value;if(std::abs(v)>zeroTol){colIndex_.push_back(e[k].col);values_.push_back(v);++rowPtr_[e[k].row+1];}k=j;}for(size_t i=1;i<rowPtr_.size();++i)rowPtr_[i]+=rowPtr_[i-1];}
  size_t rows()const{return rows_;}size_t cols()const{return cols_;}size_t nonZeros()const{return values_.size();}
  VectorOps::Vector multiply(const VectorOps::Vector&x)const{if(x.size()!=cols_)throw std::invalid_argument("CSR matrix-vector dimension mismatch");VectorOps::Vector y(rows_);for(size_t i=0;i<rows_;++i)for(size_t k=rowPtr_[i];k<rowPtr_[i+1];++k)y[i]+=values_[k]*x[colIndex_[k]];return y;}
  VectorOps::Vector transposeMultiply(const VectorOps::Vector&x)const{if(x.size()!=rows_)throw std::invalid_argument("CSR transpose dimension mismatch");VectorOps::Vector y(cols_);for(size_t i=0;i<rows_;++i)for(size_t k=rowPtr_[i];k<rowPtr_[i+1];++k)y[colIndex_[k]]+=values_[k]*x[i];return y;}
  double get(size_t r,size_t c)const{if(r>=rows_||c>=cols_)throw std::out_of_range("CSR index");for(size_t k=rowPtr_[r];k<rowPtr_[r+1];++k)if(colIndex_[k]==c)return values_[k];return 0;}
  const std::vector<size_t>& rowPointers()const{return rowPtr_;}const std::vector<size_t>& columnIndices()const{return colIndex_;}const VectorOps::Vector& values()const{return values_;}
 private:size_t rows_=0,cols_=0;std::vector<size_t>rowPtr_,colIndex_;VectorOps::Vector values_;
};
}
