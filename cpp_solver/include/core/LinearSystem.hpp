#pragma once
#include "core/Tolerance.hpp"
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
        const std::size_t n = b.size();
        if (a.size() != n) return false;
        for (auto& r : a) if (r.size() != n) return false;
        for (std::size_t k=0;k<n;++k) {
            std::size_t p=k; for (std::size_t i=k+1;i<n;++i) if(std::abs(a[i][k])>std::abs(a[p][k])) p=i;
            if (std::abs(a[p][k]) <= t.pivot) return false;
            std::swap(a[p],a[k]); std::swap(b[p],b[k]);
            for(std::size_t i=k+1;i<n;++i){ double q=a[i][k]/a[k][k]; for(std::size_t j=k;j<n;++j)a[i][j]-=q*a[k][j]; b[i]-=q*b[k]; }
        }
        x.assign(n,0.0); for(std::size_t ii=0;ii<n;++ii){std::size_t i=n-1-ii; double v=b[i];for(std::size_t j=i+1;j<n;++j)v-=a[i][j]*x[j];x[i]=v/a[i][i];}
        return true;
    }
};
}
