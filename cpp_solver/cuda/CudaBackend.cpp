#include "cuda/CudaBackend.hpp"
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
