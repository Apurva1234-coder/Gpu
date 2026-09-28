#include "cuda/CudaBackend.hpp"
#ifdef SOVEREIGN_HAS_CUDA
#include <cuda_runtime.h>
#include <cublas_v2.h>
#include <cusolverDn.h>
#include <cusparse.h>
#include <algorithm>
#include <climits>
#include <limits>
#include <memory>

namespace sovereign::cuda {
Context* Context::default_=nullptr;
namespace {
template<class T> class DeviceAllocation {
 public:
  explicit DeviceAllocation(std::size_t count=0) : count_(count) {
    if (count_) check(cudaMalloc(reinterpret_cast<void**>(&ptr_), count_*sizeof(T)), "cudaMalloc");
  }
  ~DeviceAllocation(){ if(ptr_) cudaFree(ptr_); }
  DeviceAllocation(const DeviceAllocation&)=delete;
  T* get() const { return ptr_; }
 private: T* ptr_=nullptr; std::size_t count_=0;
};
void checkBlasLength(std::size_t n,const char* name){if(n>static_cast<std::size_t>(INT_MAX))throw std::invalid_argument(std::string(name)+" exceeds cuBLAS integer range");}
}
DeviceInfo deviceInfo(int device) {
  DeviceInfo out; int count=0;
  if (cudaGetDeviceCount(&count)!=cudaSuccess || count==0) { cudaGetLastError(); return out; }
  out.available=true; out.count=count; out.selected=std::clamp(device,0,count-1);
  cudaDeviceProp p{}; check(cudaGetDeviceProperties(&p,out.selected),"cudaGetDeviceProperties");
  out.name=p.name; out.major=p.major; out.minor=p.minor; out.globalMemory=p.totalGlobalMem;
  out.runtime=std::to_string(CUDART_VERSION/1000)+"."+std::to_string((CUDART_VERSION%1000)/10); return out;
}
std::string deviceInfoText(int device) {
  auto d=deviceInfo(device);
  if(!d.available) return "CUDA Available: NO\nGPU Count: 0\n";
  return "CUDA Available: YES\nGPU Count: "+std::to_string(d.count)+"\nDevice: "+d.name+"\nCompute Capability: "+std::to_string(d.major)+"."+std::to_string(d.minor)+"\nGlobal Memory: "+std::to_string(d.globalMemory)+" bytes\nCUDA Runtime: "+d.runtime+"\n";
}
Context::Context(int device) {
  auto info=deviceInfo(device); if(!info.available)return;
  check(cudaSetDevice(info.selected),"cudaSetDevice");
  cudaStream_t s=nullptr; cublasHandle_t b=nullptr; cusparseHandle_t sp=nullptr; cusolverDnHandle_t so=nullptr;
  try {
    check(cudaStreamCreateWithFlags(&s,cudaStreamNonBlocking),"cudaStreamCreateWithFlags");
    check(cublasCreate(&b),"cublasCreate"); check(cusparseCreate(&sp),"cusparseCreate"); check(cusolverDnCreate(&so),"cusolverDnCreate");
    check(cublasSetStream(b,s),"cublasSetStream"); check(cusparseSetStream(sp,s),"cusparseSetStream"); check(cusolverDnSetStream(so,s),"cusolverDnSetStream");
  } catch (...) { if(so)cusolverDnDestroy(so); if(sp)cusparseDestroy(sp); if(b)cublasDestroy(b); if(s)cudaStreamDestroy(s); throw; }
  stream_=s; blas_=b; sparse_=sp; solver_=so; available_=true;
}
Context::~Context(){
  if(!available_)return;
  if(default_==this)default_=nullptr;
  cusolverDnDestroy(static_cast<cusolverDnHandle_t>(solver_));
  cusparseDestroy(static_cast<cusparseHandle_t>(sparse_));
  cublasDestroy(static_cast<cublasHandle_t>(blas_)); cudaStreamDestroy(static_cast<cudaStream_t>(stream_));
}
void Context::synchronize()const{if(available_)check(cudaStreamSynchronize(static_cast<cudaStream_t>(stream_)),"cudaStreamSynchronize");}
void Context::axpy(double a,const double*x,double*y,std::size_t n)const{
  if(!available_)throw std::runtime_error("CUDA device is unavailable"); if(!n)return; checkBlasLength(n,"vector length");
  DeviceAllocation<double> dx(n),dy(n); auto s=static_cast<cudaStream_t>(stream_);
  check(cudaMemcpyAsync(dx.get(),x,n*sizeof(double),cudaMemcpyHostToDevice,s),"axpy H2D x"); check(cudaMemcpyAsync(dy.get(),y,n*sizeof(double),cudaMemcpyHostToDevice,s),"axpy H2D y");
  check(cublasDaxpy(static_cast<cublasHandle_t>(blas_),static_cast<int>(n),&a,dx.get(),1,dy.get(),1),"cublasDaxpy"); check(cudaMemcpyAsync(y,dy.get(),n*sizeof(double),cudaMemcpyDeviceToHost,s),"axpy D2H"); synchronize();
}
double Context::dot(const double*x,const double*y,std::size_t n)const{
  if(!available_)throw std::runtime_error("CUDA device is unavailable"); if(!n)return 0.; checkBlasLength(n,"vector length");
  DeviceAllocation<double> dx(n),dy(n); double out=0; auto s=static_cast<cudaStream_t>(stream_);
  check(cudaMemcpyAsync(dx.get(),x,n*sizeof(double),cudaMemcpyHostToDevice,s),"dot H2D x"); check(cudaMemcpyAsync(dy.get(),y,n*sizeof(double),cudaMemcpyHostToDevice,s),"dot H2D y");
  check(cublasDdot(static_cast<cublasHandle_t>(blas_),static_cast<int>(n),dx.get(),1,dy.get(),1,&out),"cublasDdot"); synchronize(); return out;
}
void Context::gemv(const double*A,std::size_t r,std::size_t c,const double*x,double*y)const{
  if(!available_)throw std::runtime_error("CUDA device is unavailable"); if(!r||!c)return; checkBlasLength(r,"row count");checkBlasLength(c,"column count");
  DeviceAllocation<double> da(r*c),dx(c),dy(r); auto s=static_cast<cudaStream_t>(stream_); double one=1.,zero=0.;
  check(cudaMemcpyAsync(da.get(),A,r*c*sizeof(double),cudaMemcpyHostToDevice,s),"gemv H2D matrix");check(cudaMemcpyAsync(dx.get(),x,c*sizeof(double),cudaMemcpyHostToDevice,s),"gemv H2D vector");
  // Row-major A is column-major A^T to cuBLAS.
  check(cublasDgemv(static_cast<cublasHandle_t>(blas_),CUBLAS_OP_T,static_cast<int>(c),static_cast<int>(r),&one,da.get(),static_cast<int>(c),dx.get(),1,&zero,dy.get(),1),"cublasDgemv");
  check(cudaMemcpyAsync(y,dy.get(),r*sizeof(double),cudaMemcpyDeviceToHost,s),"gemv D2H");synchronize();
}
void Context::gemvTranspose(const double*A,std::size_t r,std::size_t c,const double*x,double*y)const{
  if(!available_)throw std::runtime_error("CUDA device is unavailable");if(!r||!c)return;checkBlasLength(r,"row count");checkBlasLength(c,"column count");
  DeviceAllocation<double> da(r*c),dx(r),dy(c);auto s=static_cast<cudaStream_t>(stream_);double one=1.,zero=0.;
  check(cudaMemcpyAsync(da.get(),A,r*c*sizeof(double),cudaMemcpyHostToDevice,s),"gemv transpose H2D matrix");check(cudaMemcpyAsync(dx.get(),x,r*sizeof(double),cudaMemcpyHostToDevice,s),"gemv transpose H2D vector");
  check(cublasDgemv(static_cast<cublasHandle_t>(blas_),CUBLAS_OP_N,static_cast<int>(c),static_cast<int>(r),&one,da.get(),static_cast<int>(c),dx.get(),1,&zero,dy.get(),1),"cublasDgemv transpose");check(cudaMemcpyAsync(y,dy.get(),c*sizeof(double),cudaMemcpyDeviceToHost,s),"gemv transpose D2H");synchronize();
}
void Context::spmv(const nla::CSRMatrix&A,const double*x,double*y,double alpha,double beta)const{
  if(!available_)throw std::runtime_error("CUDA device is unavailable");
  checkBlasLength(A.rows(),"CSR row count");checkBlasLength(A.cols(),"CSR column count");checkBlasLength(A.nonZeros(),"CSR nonzero count");
  const auto& rows=A.rowPointers();const auto& cols=A.columnIndices();
  std::vector<int> rp(rows.size()),ci(cols.size());for(std::size_t i=0;i<rows.size();++i){if(rows[i]>INT_MAX)throw std::invalid_argument("CSR row offset exceeds 32-bit cuSPARSE index range");rp[i]=static_cast<int>(rows[i]);}for(std::size_t i=0;i<cols.size();++i){if(cols[i]>INT_MAX)throw std::invalid_argument("CSR column index exceeds 32-bit cuSPARSE index range");ci[i]=static_cast<int>(cols[i]);}
  DeviceAllocation<int> dr(rp.size()),dc(ci.size());DeviceAllocation<double> av(A.nonZeros()),dx(A.cols()),dy(A.rows());auto s=static_cast<cudaStream_t>(stream_);
  if(!rp.empty())check(cudaMemcpyAsync(dr.get(),rp.data(),rp.size()*sizeof(int),cudaMemcpyHostToDevice,s),"spmv H2D row offsets");if(!ci.empty())check(cudaMemcpyAsync(dc.get(),ci.data(),ci.size()*sizeof(int),cudaMemcpyHostToDevice,s),"spmv H2D columns");if(A.nonZeros())check(cudaMemcpyAsync(av.get(),A.values().data(),A.nonZeros()*sizeof(double),cudaMemcpyHostToDevice,s),"spmv H2D values");if(A.cols())check(cudaMemcpyAsync(dx.get(),x,A.cols()*sizeof(double),cudaMemcpyHostToDevice,s),"spmv H2D x");if(A.rows())check(cudaMemcpyAsync(dy.get(),y,A.rows()*sizeof(double),cudaMemcpyHostToDevice,s),"spmv H2D y");
  cusparseSpMatDescr_t mat=nullptr;cusparseDnVecDescr_t xv=nullptr,yv=nullptr;void* buffer=nullptr;
  try {
    check(cusparseCreateCsr(&mat,A.rows(),A.cols(),A.nonZeros(),dr.get(),dc.get(),av.get(),CUSPARSE_INDEX_32I,CUSPARSE_INDEX_32I,CUSPARSE_INDEX_BASE_ZERO,CUDA_R_64F),"cusparseCreateCsr");
    check(cusparseCreateDnVec(&xv,A.cols(),dx.get(),CUDA_R_64F),"cusparseCreateDnVec x");check(cusparseCreateDnVec(&yv,A.rows(),dy.get(),CUDA_R_64F),"cusparseCreateDnVec y");size_t bytes=0;
    check(cusparseSpMV_bufferSize(static_cast<cusparseHandle_t>(sparse_),CUSPARSE_OPERATION_NON_TRANSPOSE,&alpha,mat,xv,&beta,yv,CUDA_R_64F,CUSPARSE_SPMV_ALG_DEFAULT,&bytes),"cusparseSpMV_bufferSize");if(bytes)check(cudaMalloc(&buffer,bytes),"spmv workspace allocation");
    check(cusparseSpMV(static_cast<cusparseHandle_t>(sparse_),CUSPARSE_OPERATION_NON_TRANSPOSE,&alpha,mat,xv,&beta,yv,CUDA_R_64F,CUSPARSE_SPMV_ALG_DEFAULT,buffer),"cusparseSpMV");if(A.rows())check(cudaMemcpyAsync(y,dy.get(),A.rows()*sizeof(double),cudaMemcpyDeviceToHost,s),"spmv D2H y");synchronize();
  } catch (...) {if(buffer)cudaFree(buffer);if(yv)cusparseDestroyDnVec(yv);if(xv)cusparseDestroyDnVec(xv);if(mat)cusparseDestroySpMat(mat);throw;}
  if(buffer)cudaFree(buffer);cusparseDestroyDnVec(yv);cusparseDestroyDnVec(xv);cusparseDestroySpMat(mat);
}
bool Context::solveDense(const nla::DenseMatrix&A,const std::vector<double>&b,std::vector<double>&x)const{
  if(!available_)throw std::runtime_error("CUDA device is unavailable");if(A.rows()!=A.cols()||b.size()!=A.rows())return false;const std::size_t n=A.rows();if(n==0){x.clear();return true;}checkBlasLength(n,"dense system dimension");
  std::vector<double> hA(n*n),hx=b;for(std::size_t i=0;i<n;++i)for(std::size_t j=0;j<n;++j)hA[j*n+i]=A(i,j);
  DeviceAllocation<double> da(n*n),db(n),work;DeviceAllocation<int> piv(n),info(1);auto s=static_cast<cudaStream_t>(stream_);
  check(cudaMemcpyAsync(da.get(),hA.data(),hA.size()*sizeof(double),cudaMemcpyHostToDevice,s),"dense solve H2D matrix");check(cudaMemcpyAsync(db.get(),hx.data(),hx.size()*sizeof(double),cudaMemcpyHostToDevice,s),"dense solve H2D rhs");
  int lwork=0;check(cusolverDnDgetrf_bufferSize(static_cast<cusolverDnHandle_t>(solver_),static_cast<int>(n),static_cast<int>(n),da.get(),static_cast<int>(n),&lwork),"cusolverDnDgetrf_bufferSize");
  DeviceAllocation<double> dw(static_cast<std::size_t>(std::max(1,lwork)));
  check(cusolverDnDgetrf(static_cast<cusolverDnHandle_t>(solver_),static_cast<int>(n),static_cast<int>(n),da.get(),static_cast<int>(n),dw.get(),piv.get(),info.get()),"cusolverDnDgetrf");int hostInfo=0;check(cudaMemcpyAsync(&hostInfo,info.get(),sizeof(int),cudaMemcpyDeviceToHost,s),"dense solve factor status");synchronize();if(hostInfo!=0)return false;
  check(cusolverDnDgetrs(static_cast<cusolverDnHandle_t>(solver_),CUBLAS_OP_N,static_cast<int>(n),1,da.get(),static_cast<int>(n),piv.get(),db.get(),static_cast<int>(n),info.get()),"cusolverDnDgetrs");check(cudaMemcpyAsync(&hostInfo,info.get(),sizeof(int),cudaMemcpyDeviceToHost,s),"dense solve status");check(cudaMemcpyAsync(hx.data(),db.get(),n*sizeof(double),cudaMemcpyDeviceToHost,s),"dense solve D2H solution");synchronize();if(hostInfo!=0)return false;x=std::move(hx);return std::all_of(x.begin(),x.end(),[](double v){return std::isfinite(v);});
}
}
#else
namespace sovereign::cuda {
DeviceInfo deviceInfo(int){return {};}
std::string deviceInfoText(int){return "CUDA Available: NO\nGPU Count: 0\n";}
Context::Context(int){}
Context::~Context()=default;
void Context::synchronize()const{}
void Context::axpy(double,const double*,double*,std::size_t)const{throw std::runtime_error("CUDA backend not compiled");}
double Context::dot(const double*,const double*,std::size_t)const{throw std::runtime_error("CUDA backend not compiled");}
void Context::gemv(const double*,std::size_t,std::size_t,const double*,double*)const{throw std::runtime_error("CUDA backend not compiled");}
void Context::gemvTranspose(const double*,std::size_t,std::size_t,const double*,double*)const{throw std::runtime_error("CUDA backend not compiled");}
void Context::spmv(const nla::CSRMatrix&,const double*,double*,double,double)const{throw std::runtime_error("CUDA backend not compiled");}
bool Context::solveDense(const nla::DenseMatrix&,const std::vector<double>&,std::vector<double>&)const{throw std::runtime_error("CUDA backend not compiled");}
}
#endif
