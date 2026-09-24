#include "core/numerical/LinearAlgebra.hpp"
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
using namespace sovereign;using namespace sovereign::nla;
int main(){using V=VectorOps::Vector;V a{1,2,3},b{4,5,6};assert((VectorOps::add(a,b)==V{5,7,9}));assert((VectorOps::subtract(b,a)==V{3,3,3}));assert(std::abs(VectorOps::dot(a,b)-32)<1e-12);assert(std::abs(VectorOps::norm1(a)-6)<1e-12);assert(std::abs(VectorOps::norm2(a)-std::sqrt(14.0))<1e-12);assert(std::abs(VectorOps::normInf(a)-3)<1e-12);assert((VectorOps::scale(a,2)==V{2,4,6}));V y{1,1,1};VectorOps::axpy(2,a,y);assert((y==V{3,5,7}));V div;assert(!VectorOps::elementDivide(a,V{1,0,1},div,1e-9));
 DenseMatrix d(2,3);d(0,0)=1;d(0,2)=2;d(1,1)=3;assert((d.multiply({2,4,5})==V{12,12}));assert((d.transposeMultiply({2,4})==V{2,12,4}));auto dt=d.transpose();assert(dt.rows()==3&&dt.cols()==2&&dt(2,0)==2);
 CSRMatrix sp(2,3,{{0,0,1},{0,2,2},{1,1,3}});assert(sp.nonZeros()==3);assert((sp.multiply({2,4,5})==V{12,12}));assert((sp.transposeMultiply({2,4})==V{2,12,4}));
 DenseMatrix sys(2,2);sys(0,0)=3;sys(0,1)=1;sys(1,0)=1;sys(1,1)=2;V sol;assert(DenseLinearSolver{}.solve(sys,{5,5},sol));assert(std::abs(sol[0]-1)<1e-10&&std::abs(sol[1]-2)<1e-10);LUFactorization lu;assert(lu.factor(sys)&&lu.solve({5,5},sol));CholeskyFactorization ch;assert(ch.factor(sys)&&ch.solve({5,5},sol));assert(std::abs(sol[0]-1)<1e-10&&std::abs(sol[1]-2)<1e-10);
 DenseMatrix singular(2,2);singular(0,0)=1;singular(0,1)=2;singular(1,0)=2;singular(1,1)=4;assert(!lu.factor(singular));DenseMatrix near(2,2);near(0,0)=1;near(1,1)=1e-14;Tolerance nt;nt.singularity=1e-12;assert(!lu.factor(near,nt));assert(!ch.factor(DenseMatrix(1,1,-1)));bool dim=false;try{d.multiply({1});}catch(const std::invalid_argument&){dim=true;}assert(dim);
 // Simple reproducible throughput samples; timings are reported, not gated.
 constexpr size_t n=180;DenseMatrix dense(n,n);for(size_t i=0;i<n;++i)for(size_t j=0;j<n;++j)dense(i,j)=((i*17+j*13)%19==0)?0.25:0.0;std::vector<Triplet> entries;for(size_t i=0;i<n*10;++i)entries.push_back({i/n,(i*7)%n,0.25});CSRMatrix sparse(n,n,entries);V x(n,1);auto t0=std::chrono::steady_clock::now();for(int k=0;k<200;++k)(void)dense.multiply(x);auto t1=std::chrono::steady_clock::now();for(int k=0;k<200;++k)(void)sparse.multiply(x);auto t2=std::chrono::steady_clock::now();std::cout<<"NLA sample timings (200 matvecs): dense="<<std::chrono::duration<double,std::milli>(t1-t0).count()<<"ms sparse="<<std::chrono::duration<double,std::milli>(t2-t1).count()<<"ms; CSR nnz="<<sparse.nonZeros()<<"\n";return 0;}
