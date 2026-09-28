#pragma once
#include "core/numerical/SparseMatrix.hpp"
#include "core/numerical/DenseMatrix.hpp"
#include "cuda/CudaError.hpp"
#include <string>
#include <vector>

namespace sovereign::cuda {
struct DeviceInfo { bool available=false; int count=0, selected=-1; std::string name; int major=0, minor=0; std::size_t globalMemory=0; std::string runtime; };
DeviceInfo deviceInfo(int device=0);
std::string deviceInfoText(int device=0);

enum class Backend { CPU, CUDA, Auto };
inline const char* backendName(Backend b) { return b==Backend::CPU?"cpu":b==Backend::CUDA?"cuda":"auto"; }
inline Backend parseBackend(const std::string& s) { return s=="cuda"?Backend::CUDA:s=="auto"?Backend::Auto:Backend::CPU; }

class Context {
public:
  explicit Context(int device=0);
  ~Context();
  Context(const Context&)=delete;
  bool available() const { return available_; }
  static void setDefault(Context* context) { default_=context; }
  static Context* defaultContext() { return default_; }
  void synchronize() const;
  void axpy(double alpha, const double* x, double* y, std::size_t n) const;
  double dot(const double* x, const double* y, std::size_t n) const;
  void gemv(const double* A, std::size_t rows, std::size_t cols, const double* x, double* y) const;
  void gemvTranspose(const double* A, std::size_t rows, std::size_t cols, const double* x, double* y) const;
  void spmv(const nla::CSRMatrix& A, const double* x, double* y, double alpha=1.0, double beta=0.0) const;
  bool solveDense(const nla::DenseMatrix& A, const std::vector<double>& b, std::vector<double>& x) const;
private:
  inline static Context* default_=nullptr;
  bool available_=false;
#ifdef SOVEREIGN_HAS_CUDA
  void* stream_=nullptr; void* blas_=nullptr; void* sparse_=nullptr; void* solver_=nullptr;
#endif
};

inline Backend chooseBackend(Backend requested, std::size_t rows, std::size_t cols, std::size_t nnz, std::size_t repetitions=1) {
  if (requested!=Backend::Auto) return requested;
  return repetitions>=8 && (rows>=512 || cols>=512 || nnz>=4096) ? Backend::CUDA : Backend::CPU;
}
}
