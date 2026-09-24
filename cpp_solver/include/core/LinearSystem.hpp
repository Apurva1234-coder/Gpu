#pragma once
#include "core/Tolerance.hpp"
#include "core/numerical/LinearSolver.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace sovereign {
// Small dense CPU linear-system abstraction shared by the LP algorithms.
class LinearSystem {
public:
    static bool solve(std::vector<std::vector<double>> a, std::vector<double> b,
                      std::vector<double>& x, Tolerance t = {}) {
        const std::size_t n = b.size(); if (a.size() != n) return false;
        nla::DenseMatrix matrix(n,n);for(std::size_t i=0;i<n;++i){if(a[i].size()!=n)return false;for(std::size_t j=0;j<n;++j)matrix(i,j)=a[i][j];}
        return nla::DenseLinearSolver(t).solve(matrix,b,x);
    }
};
}
