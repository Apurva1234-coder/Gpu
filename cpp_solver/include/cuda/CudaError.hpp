#pragma once
#include <stdexcept>
#include <string>

#ifdef SOVEREIGN_HAS_CUDA
#include <cuda_runtime.h>
#include <cublas_v2.h>
#include <cusparse.h>
namespace sovereign::cuda {
inline void check(cudaError_t e, const char* where) { if (e != cudaSuccess) throw std::runtime_error(std::string(where)+": "+cudaGetErrorString(e)); }
inline void check(cublasStatus_t e, const char* where) { if (e != CUBLAS_STATUS_SUCCESS) throw std::runtime_error(std::string(where)+": cuBLAS error"); }
inline void check(cusparseStatus_t e, const char* where) { if (e != CUSPARSE_STATUS_SUCCESS) throw std::runtime_error(std::string(where)+": cuSPARSE error"); }
}
#endif
