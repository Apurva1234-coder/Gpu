#pragma once
#include <cmath>
namespace sovereign {
struct Tolerance {
    double zero = 1e-9, feasibility = 1e-8, bound = 1e-9, substitution = 1e-9;
    double pivot = 1e-10, singularity = 1e-12, optimality = 1e-8, convergence = 1e-8;
    double linearResidual = 1e-8;
    bool equal(double a, double b) const { return std::abs(a - b) <= bound; }
    bool nearZero(double x) const { return std::abs(x) <= zero; }
};
}
