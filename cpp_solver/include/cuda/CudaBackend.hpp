#pragma once
#include "core/numerical/SparseMatrix.hpp"
#include "core/numerical/DenseMatrix.hpp"
#include "cuda/CudaError.hpp"
#include <stdexcept>
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
  bool solvePdhg(const nla::CSRMatrix& A, const std::vector<double>& b,
      const std::vector<double>& c, const std::vector<double>& tau,
      const std::vector<double>& sigma, std::size_t maxIterations, double tolerance,
      std::vector<double>& x, std::vector<double>& y, std::size_t& iterations,
      double& primalResidual, double& dualResidual, double& complementarity) const;
private:
  static Context* default_;
  bool available_=false;
#ifdef SOVEREIGN_HAS_CUDA
  void* stream_=nullptr; void* blas_=nullptr; void* sparse_=nullptr; void* solver_=nullptr;
#endif
};

inline Backend chooseBackend(Backend requested, std::size_t rows, std::size_t cols, std::size_t nnz, std::size_t repetitions=1) {
  if (requested!=Backend::Auto) return requested;
  return repetitions>=8 && (rows>=512 || cols>=512 || nnz>=4096) ? Backend::CUDA : Backend::CPU;
}

// Production auto-routing is deliberately stricter than the experimental
// work-size heuristic above. Only LARGE models and algorithms with real CUDA
// kernels are eligible, and a measured same-method speedup is required before
// AUTO selects CUDA. The current checked-in benchmark set has no verified
// CPU/CUDA pairs, so callers pass measuredSpeedup=false for now.
inline Backend chooseBackend(Backend requested, std::size_t rows, std::size_t cols,
    std::size_t nnz, std::size_t repetitions, const std::string& modelSize,
    const std::string& algorithm, bool measuredSpeedup) {
  const bool cudaCompatible = algorithm=="ipm" || algorithm=="qp" || algorithm=="pdhg";
  if (requested==Backend::CUDA && !cudaCompatible)
    throw std::invalid_argument("CUDA execution is not implemented for algorithm: "+algorithm);
  if (requested!=Backend::Auto) return requested;
  if (modelSize!="LARGE" || !cudaCompatible || !measuredSpeedup) return Backend::CPU;
  return chooseBackend(requested, rows, cols, nnz, repetitions);
}
}
