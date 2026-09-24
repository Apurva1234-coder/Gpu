#pragma once
#include "lp/LPSolver.hpp"
namespace sovereign { class RevisedSimplex { LPSolver core_; public: explicit RevisedSimplex(Tolerance t={}):core_(t){} LPResult solve(const Model&m,std::size_t limit=10000)const{return core_.solve(m,LPMethod::RevisedSimplex,limit);} }; }
