#pragma once
#include "model/Model.hpp"
#include "lp/Solution.hpp"
#include "lp/Objective.hpp"
#include "core/Tolerance.hpp"
#include "core/LinearSystem.hpp"
#include "core/numerical/Vector.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <sstream>
#include <string>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sovereign {

enum class LPMethod { RevisedSimplex, DualSimplex, IPM, PDHG };
inline const char* methodName(LPMethod m) { return m==LPMethod::RevisedSimplex?"revised-simplex":m==LPMethod::DualSimplex?"dual-simplex":m==LPMethod::IPM?"ipm":"pdhg"; }

// Internal form: maximize q*x, A*x <= b, x >= 0.  The tableau-free
// implementation below is the standard two-phase revised-simplex recurrence;
// the basis is represented by the B columns and solves are performed by the
// pivot factorization implicit in the dictionary (no external optimizer).
struct StandardLP {
    std::vector<std::vector<double>> A; std::vector<double> b, c;
    std::vector<std::pair<std::size_t,double>> map; // internal variable -> original variable, scale
    double constant{}; bool maximize{};
};

constexpr std::size_t DenseLPMemoryBudgetBytes = 256ULL * 1024ULL * 1024ULL;

inline long double estimateDenseLPBytes(const Model& model) {
    std::size_t columns = 0, rows = 0;
    for (const auto& variable : model.variables) if (variable.active) {
        columns += std::isfinite(variable.lower) ? 1 : 2;
        if (std::isfinite(variable.upper)) ++rows;
    }
    for (const auto& constraint : model.constraints) if (constraint.active)
        rows += constraint.relation == Relation::Equal ? 2 : 1;
    const long double r = static_cast<long double>(rows);
    const long double n = static_cast<long double>(columns);
    // Peak can include standard form, tableau, standard matrix, and a basis
    // matrix simultaneously. Artificial columns are bounded by row count.
    const long double standardForm = r * n;
    const long double tableau = (r + 1) * (n + 2 * r + 1);
    const long double standardMatrix = r * (n + 2 * r);
    const long double basis = r * r;
    const long double vectorsAndRowHeaders = 64 * r + 32 * (n + r + 1);
    return sizeof(double) * (standardForm + tableau + standardMatrix + basis) + vectorsAndRowHeaders;
}

inline bool denseLPWithinMemoryBudget(const Model& model) {
    return estimateDenseLPBytes(model) <= static_cast<long double>(DenseLPMemoryBudgetBytes);
}

inline std::string denseLPMemoryGuardMessage(const Model& model) {
    const long double estimatedMiB = estimateDenseLPBytes(model) / (1024.0L * 1024.0L);
    const long double budgetMiB = static_cast<long double>(DenseLPMemoryBudgetBytes) / (1024.0L * 1024.0L);
    std::ostringstream message;
    message << "dense LP path refused: estimated peak workspace " << static_cast<double>(estimatedMiB)
            << " MiB exceeds the " << static_cast<double>(budgetMiB)
            << " MiB safety budget; use revised-simplex (sparse route) or PDHG";
    return message.str();
}

inline StandardLP standardize(const Model& m, const Tolerance& t={}, bool preserveIntegrality=false) {
    (void)t;
    if (!preserveIntegrality && !denseLPWithinMemoryBudget(m)) throw std::length_error(denseLPMemoryGuardMessage(m));
    StandardLP s; s.maximize=m.sense==Sense::Maximize;
    std::vector<std::size_t> ids;
    for(const auto& v:m.variables) if(v.active) {
        ids.push_back(v.originalId);
        if(std::isfinite(v.lower)) s.map.push_back({v.originalId,1.0});
        else { s.map.push_back({v.originalId,1.0}); s.map.push_back({v.originalId,-1.0}); }
    }
    auto add=[&](std::vector<double> row,double rhs){s.A.push_back(std::move(row));s.b.push_back(rhs);};
    auto rowFor=[&](const Constraint& r) {
        std::vector<double> row; row.reserve(s.map.size());
        for(const auto& p:s.map) { auto it=r.coefficients.find(p.first); row.push_back((it==r.coefficients.end()?0.0:it->second)*p.second); }
        return row;
    };
    for(const auto& r:m.constraints) if(r.active) {
        std::vector<double> row=rowFor(r); double rhs=r.rhs;
        for(auto id:ids) { const auto& v=m.variables[id]; auto it=r.coefficients.find(id); if(std::isfinite(v.lower)&&it!=r.coefficients.end()) rhs-=it->second*v.lower; }
        if(r.relation==Relation::LessEqual) add(row,rhs);
        else if(r.relation==Relation::GreaterEqual) { for(double& x:row)x=-x; add(row,-rhs); }
        else { add(row,rhs); for(double& x:row)x=-x; add(row,-rhs); }
    }
    for(auto id:ids) {
        const auto& v=m.variables[id]; if(!std::isfinite(v.upper)) continue;
        std::vector<double> row(s.map.size(),0.0);
        for(std::size_t j=0;j<s.map.size();++j) if(s.map[j].first==id) row[j]=s.map[j].second;
        add(row,std::isfinite(v.lower)?v.upper-v.lower:v.upper);
    }
    // Row and column equilibration reduces scale disparities while preserving
    // the original model through the transformed variable map.
    if(!preserveIntegrality) for(std::size_t i=0;i<s.A.size();++i) {
        double scale=0; for(double a:s.A[i]) scale=std::max(scale,std::abs(a));
        if(scale>0 && std::isfinite(scale)) { for(double& a:s.A[i]) a/=scale; s.b[i]/=scale; }
    }
    if(!preserveIntegrality) for(std::size_t j=0;j<s.map.size();++j) {
        double scale=0; for(const auto& row:s.A) scale=std::max(scale,std::abs(row[j]));
        const double multiplier=scale>0&&std::isfinite(scale)?std::max(1e-12,std::min(1e12,1.0/scale)):1.0;
        for(auto& row:s.A) row[j]*=multiplier;
        s.map[j].second*=multiplier;
    }
    for(std::size_t j=0;j<s.map.size();++j) {
        const auto objective=m.objective.find(s.map[j].first);
        const double c=objective==m.objective.end()?0.0:objective->second;
        s.c.push_back((s.maximize?1.0:-1.0)*c*s.map[j].second);
    }
    return s;
}

struct SparseStandardLP {
    std::vector<std::vector<std::pair<std::size_t,double>>> A;
    std::vector<double> b, c;
    std::vector<std::pair<std::size_t,double>> map;
    double constant{};
    bool maximize{};
};

inline SparseStandardLP standardizeSparse(const Model& m, const Tolerance& t={}) {
    (void)t;
    SparseStandardLP s;
    s.maximize = m.sense == Sense::Maximize;
    std::vector<std::vector<std::pair<std::size_t,double>>> byOriginal(m.variables.size());
    for (const auto& v : m.variables) if (v.active) {
        auto addMap = [&](double scale) {
            const std::size_t column = s.map.size();
            s.map.emplace_back(v.originalId, scale);
            if (v.originalId < byOriginal.size()) byOriginal[v.originalId].emplace_back(column, scale);
        };
        addMap(1.0);
        if (!std::isfinite(v.lower)) addMap(-1.0);
    }
    auto addRow = [&](std::vector<std::pair<std::size_t,double>> row, double rhs) {
        s.A.push_back(std::move(row));
        s.b.push_back(rhs);
    };
    for (const auto& constraint : m.constraints) if (constraint.active) {
        std::vector<std::pair<std::size_t,double>> row;
        row.reserve(constraint.coefficients.size() * 2);
        double rhs = constraint.rhs;
        for (const auto& coefficient : constraint.coefficients) {
            const auto id = coefficient.first;
            if (id >= byOriginal.size()) continue;
            if (std::isfinite(m.variables[id].lower)) rhs -= coefficient.second * m.variables[id].lower;
            for (const auto& mapped : byOriginal[id])
                row.emplace_back(mapped.first, coefficient.second * mapped.second);
        }
        if (constraint.relation == Relation::LessEqual) addRow(std::move(row), rhs);
        else if (constraint.relation == Relation::GreaterEqual) {
            for (auto& entry : row) entry.second = -entry.second;
            addRow(std::move(row), -rhs);
        } else {
            addRow(row, rhs);
            for (auto& entry : row) entry.second = -entry.second;
            addRow(std::move(row), -rhs);
        }
    }
    for (const auto& v : m.variables) if (v.active && std::isfinite(v.upper) && v.originalId < byOriginal.size()) {
        std::vector<std::pair<std::size_t,double>> row;
        for (const auto& mapped : byOriginal[v.originalId]) row.push_back(mapped);
        addRow(std::move(row), std::isfinite(v.lower) ? v.upper - v.lower : v.upper);
    }
    for (std::size_t i = 0; i < s.A.size(); ++i) {
        double scale = 0.0;
        for (const auto& entry : s.A[i]) scale = std::max(scale, std::abs(entry.second));
        if (scale > 0.0 && std::isfinite(scale)) {
            for (auto& entry : s.A[i]) entry.second /= scale;
            s.b[i] /= scale;
        }
    }
    std::vector<double> columnScale(s.map.size(), 0.0);
    for (const auto& row : s.A) for (const auto& entry : row)
        columnScale[entry.first] = std::max(columnScale[entry.first], std::abs(entry.second));
    for (std::size_t j = 0; j < s.map.size(); ++j) {
        const double maxValue = columnScale[j];
        const double multiplier = maxValue > 0.0 && std::isfinite(maxValue)
            ? std::max(1e-12, std::min(1e12, 1.0 / maxValue)) : 1.0;
        s.map[j].second *= multiplier;
        columnScale[j] = multiplier;
    }
    for (auto& row : s.A) for (auto& entry : row) entry.second *= columnScale[entry.first];
    for (const auto& mapped : s.map) {
        const auto objective = m.objective.find(mapped.first);
        const double coefficient = objective == m.objective.end() ? 0.0 : objective->second;
        s.c.push_back((s.maximize ? 1.0 : -1.0) * coefficient * mapped.second);
    }
    return s;
}

class LPSolver {
public:
    explicit LPSolver(Tolerance t={}):tol_(t){}
    LPResult solve(const Model& m, LPMethod method=LPMethod::RevisedSimplex, std::size_t limit=10000, const Model* integralityModel=nullptr) const {
        LPResult out;out.method=methodName(method);
        out.estimatedDenseMemoryBytes=static_cast<double>(estimateDenseLPBytes(m));
        for(const auto&v:m.variables)if(v.type!=VariableType::Continuous){out.status=LPStatus::Unsupported;out.method=methodName(method);return out;}
        if(!m.quadratic.empty()){out.status=LPStatus::Unsupported;return out;}
        if(!integralityModel) {
            std::size_t transformedVariables=0, standardizedRows=0;
            for(const auto& v:m.variables) if(v.active) {
                transformedVariables+=std::isfinite(v.lower)?1:2;
                if(std::isfinite(v.upper)) ++standardizedRows;
            }
            std::size_t constraintNonzeros=0;
            for(const auto& c:m.constraints) if(c.active)
                { standardizedRows+=c.relation==Relation::Equal?2:1; constraintNonzeros+=c.coefficients.size(); }
            // Retain the stable tableau implementation for moderate models;
            // use sparse revised simplex for large, very sparse models where
            // dense tableau updates waste work on structural zeros.
            const long double estimatedEntries=static_cast<long double>(standardizedRows)*
                (transformedVariables+2*standardizedRows+2);
            const long double matrixCells=static_cast<long double>(standardizedRows)*transformedVariables;
            const long double density=matrixCells>0?constraintNonzeros/matrixCells:1.0L;
            if(!denseLPWithinMemoryBudget(m)) {
                LPResult sparse=solveSparse(m,method,limit);
                sparse.denseMemoryGuardTriggered=true;
                const std::string solverMessage=sparse.message;
                sparse.message=denseLPMemoryGuardMessage(m)+"; sparse revised simplex selected";
                if(!solverMessage.empty()) sparse.message+="; solver detail: "+solverMessage;
                return sparse;
            }
            if(estimatedEntries>8.0e6L || (estimatedEntries>1.0e6L && density<0.0045L) ||
               (estimatedEntries>1.0e5L && density<0.03L && m.variables.size()<=m.constraints.size()))
                return solveSparse(m,method,limit);
        }
        const auto standardizationStart=std::chrono::steady_clock::now();
        StandardLP s=standardize(m,tol_,integralityModel!=nullptr);
        out.standardizedRows=s.A.size(); out.standardizedColumns=s.c.size();
        for(const auto& row:s.A)for(double value:row)if(value!=0.0)++out.standardizedNonzeros;
        out.standardizationTimeMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-standardizationStart).count();
        const std::size_t n=s.c.size(), R=s.A.size();
        if(n==0) { out.status=LPStatus::NumericalFailure; out.message="LP has no active transformed variables"; return out; }
        std::size_t artificialCount=0; std::vector<bool> needsArtificial(R,false);
        for(std::size_t i=0;i<R;++i) if(s.b[i]<-tol_.feasibility) { needsArtificial[i]=true; ++artificialCount; }
        const std::size_t originalAndSlack=n+R, total=originalAndSlack+artificialCount;
        std::vector<std::vector<double>> tab(R+1,std::vector<double>(total+1,0.0));
        std::vector<std::vector<double>> standardMatrix(R,std::vector<double>(total,0.0));
        std::vector<double> standardRhs(R,0.0);
        std::vector<std::size_t> basis(R); std::vector<bool> artificial(total,false);
        std::vector<unsigned char> basic(total,0);
        std::size_t nextArtificial=originalAndSlack;
        for(std::size_t i=0;i<R;++i) {
            const double sign=needsArtificial[i]?-1.0:1.0;
            for(std::size_t j=0;j<n;++j) tab[i][j]=standardMatrix[i][j]=sign*s.A[i][j];
            tab[i][n+i]=standardMatrix[i][n+i]=sign;
            if(needsArtificial[i]) { tab[i][nextArtificial]=standardMatrix[i][nextArtificial]=1.0; basis[i]=nextArtificial; artificial[nextArtificial]=true; ++nextArtificial; }
            else basis[i]=n+i;
            basic[basis[i]]=1;
            tab[i].back()=standardRhs[i]=sign*s.b[i];
        }
        std::vector<std::size_t> pivotNonzeroColumns;
        pivotNonzeroColumns.reserve(total+1);
        auto setObjective=[&](const std::vector<double>& cost) {
            std::fill(tab[R].begin(),tab[R].end(),0.0);
            for(std::size_t j=0;j<total;++j) tab[R][j]=-cost[j];
            for(std::size_t i=0;i<R;++i) { const double cb=cost[basis[i]]; if(cb!=0) for(std::size_t j=0;j<=total;++j) tab[R][j]+=cb*tab[i][j]; }
        };
        auto simplex=[&](const std::vector<double>& cost,std::size_t maxIterations,bool allowArtificial,std::size_t& iterations) {
            setObjective(cost);
            for(std::size_t it=0;it<maxIterations;++it) {
                std::size_t enter=total; double best=tol_.optimality;
                for(std::size_t j=0;j<total;++j) {
                    if(basic[j] || (!allowArtificial&&artificial[j]) || tab[R][j]>=-tol_.optimality) continue;
                    if(-tab[R][j]>best) { enter=j; best=-tab[R][j]; }
                }
                if(enter==total) { iterations=it; return LPStatus::Optimal; }
                double minimumRatio=std::numeric_limits<double>::infinity();
                for(std::size_t i=0;i<R;++i) if(tab[i][enter]>tol_.pivot) minimumRatio=std::min(minimumRatio,std::max(0.0,tab[i].back())/tab[i][enter]);
                if(!std::isfinite(minimumRatio)) { iterations=it; return LPStatus::Unbounded; }
                // Harris two-pass ratio test: preserve feasibility while
                // preferring a numerically stronger pivot among near ties.
                std::size_t leave=R; double bestPivot=-1.0;
                const double relaxed=minimumRatio+tol_.feasibility*(1.0+std::abs(minimumRatio));
                for(std::size_t i=0;i<R;++i) if(tab[i][enter]>tol_.pivot) {
                    const double ratio=std::max(0.0,tab[i].back())/tab[i][enter];
                    if(ratio<=relaxed && tab[i][enter]>bestPivot) { leave=i; bestPivot=tab[i][enter]; }
                }
                if(leave==R) { iterations=it; return LPStatus::NumericalFailure; }
                pivot(tab,leave,enter,pivotNonzeroColumns); basic[basis[leave]]=0; basis[leave]=enter; basic[enter]=1; iterations=it+1;
                for(std::size_t i=0;i<R;++i) if(tab[i].back()<0 && tab[i].back()>-tol_.feasibility*10) tab[i].back()=0;
                if(it+1==maxIterations) return LPStatus::IterationLimit;
            }
            return LPStatus::IterationLimit;
        };
        std::vector<double> phaseOne(total,0.0); for(std::size_t j=0;j<total;++j) if(artificial[j]) phaseOne[j]=-1.0;
        std::size_t phaseOneIterations=0; LPStatus phaseOneStatus=simplex(phaseOne,limit,true,phaseOneIterations);
        out.iterations=phaseOneIterations;
        if(phaseOneStatus==LPStatus::IterationLimit) { out.status=phaseOneStatus; out.message="phase-I iteration limit"; return out; }
        if(phaseOneStatus!=LPStatus::Optimal || tab[R].back() < -tol_.feasibility*10) { out.status=LPStatus::Infeasible; out.message="phase I found no feasible basis"; return out; }
        // Artificial variables must be fixed at zero in phase II. Pivot them
        // out wherever a real/slack column can replace them; a remaining row
        // with no such coefficient is redundant and its artificial basic is
        // pinned to zero by that row.
        for(std::size_t i=0;i<R;++i) if(artificial[basis[i]]) {
            std::size_t replacement=originalAndSlack;
            for(std::size_t j=0;j<originalAndSlack;++j) {
                if(!basic[j] && std::abs(tab[i][j])>tol_.pivot) { replacement=j; break; }
            }
            if(replacement<originalAndSlack) { pivot(tab,i,replacement,pivotNonzeroColumns); basic[basis[i]]=0; basis[i]=replacement; basic[replacement]=1; }
        }
        std::vector<double> phaseTwo(total,0.0); for(std::size_t j=0;j<n;++j) phaseTwo[j]=s.c[j];
        std::size_t phaseTwoIterations=0; out.status=simplex(phaseTwo,limit,false,phaseTwoIterations); out.iterations+=phaseTwoIterations;
        if(out.status!=LPStatus::Optimal) { out.message=out.status==LPStatus::Unbounded?"phase-II objective is unbounded":"phase-II solve did not converge"; return out; }
        std::vector<double> basicValues; std::vector<std::vector<double>> basisMatrix(R,std::vector<double>(R,0.0));
        for(std::size_t i=0;i<R;++i)for(std::size_t j=0;j<R;++j)basisMatrix[i][j]=standardMatrix[i][basis[j]];
        const bool refactored=LinearSystem::solve(basisMatrix,standardRhs,basicValues,tol_);
        std::vector<double> z(n,0.0); for(std::size_t i=0;i<R;++i) if(basis[i]<n) z[basis[i]]=std::max(0.0,refactored?basicValues[i]:tab[i].back());
        out.solution.primal.assign(m.variables.size(),0);for(size_t j=0;j<n;++j)out.solution.primal[s.map[j].first]+=s.map[j].second*z[j];for(size_t i=0;i<m.variables.size();++i)if(m.variables[i].active&&std::isfinite(m.variables[i].lower))out.solution.primal[i]+=m.variables[i].lower;
        out.objectiveValue=evaluateObjective(m,out.solution.primal);out.solution.objectiveValue=out.objectiveValue;out.solution.feasibilityResidual=verifyResidual(m,out.solution.primal);
        if(!std::isfinite(out.objectiveValue)||verifyScaledResidual(m,out.solution.primal)>std::max(100.0*tol_.feasibility,1e-7)) {
            out.status=LPStatus::NumericalFailure; out.message="candidate failed original-model primal feasibility check"; return out;
        }
        out.basisVariables=basis;for(size_t i=0;i<R;++i)out.basisRows.emplace_back(tab[i].begin(),tab[i].end());for(size_t i=0;i<R;++i){size_t bv=basis[i];if(bv>=n||bv>=s.map.size())continue;size_t original=s.map[bv].first;const Model& im=integralityModel?*integralityModel:m;if(original>=im.variables.size()||(im.variables[original].type!=VariableType::Integer&&im.variables[original].type!=VariableType::Binary))continue;double rhs=tab[i].back(),fr=rhs-std::floor(rhs);if(fr<=tol_.feasibility||fr>=1-tol_.feasibility)continue;std::vector<double> fc(n);double cutRhs=fr;bool valid=true;for(size_t j=0;j<n+R;++j){if(basic[j])continue;double f=tab[i][j]-std::floor(tab[i][j]);if(f<tol_.zero||1-f<tol_.zero)f=0;if(std::abs(f)<=tol_.feasibility)continue;if(j<n)fc[j]+=f;else{size_t row=j-n;if(row>=s.A.size()||std::abs(s.b[row]-std::round(s.b[row]))>tol_.feasibility){valid=false;break;}for(size_t k=0;k<n;++k)if(std::abs(s.A[row][k])>tol_.feasibility){size_t id=s.map[k].first;if(id>=im.variables.size()||im.variables[id].type==VariableType::Continuous||std::abs(s.A[row][k]-std::round(s.A[row][k]))>tol_.feasibility){valid=false;break;}fc[k]-=f*s.A[row][k];}cutRhs-=f*s.b[row];}}
          if(!valid)continue;GomoryRow gr;gr.sourceVariable=original;gr.coefficients.assign(m.variables.size(),0);double lhs=0;for(size_t j=0;j<n;++j)if(std::abs(fc[j])>tol_.zero){size_t id=s.map[j].first;double scale=s.map[j].second;if(std::abs(scale-1)>tol_.feasibility){valid=false;break;}gr.coefficients[id]+=fc[j];double shift=std::isfinite(m.variables[id].lower)?m.variables[id].lower:0;cutRhs+=fc[j]*shift;lhs+=fc[j]*(out.solution.primal[id]-shift);}if(!valid)continue;gr.rhs=cutRhs;gr.violation=gr.rhs-lhs;if(gr.violation>tol_.feasibility)out.gomoryRows.push_back(std::move(gr));}
        if(out.status==LPStatus::NumericalFailure)out.status=LPStatus::IterationLimit;return out;
    }
private:
    Tolerance tol_;
    using SparseColumn = std::vector<std::pair<std::size_t,double>>;
    struct SparseLU {
        std::vector<std::unordered_map<std::size_t,double>> rows;
        std::vector<std::vector<std::pair<std::size_t,double>>> upperColumns, lowerColumns;
        std::vector<std::size_t> permutation;
        std::size_t size{};
        std::size_t failedAt{}; double failedPivot{};
        bool factor(std::vector<std::unordered_map<std::size_t,double>> a, double pivotTolerance) {
            size=a.size(); rows=std::move(a); permutation.resize(size);
            std::iota(permutation.begin(),permutation.end(),0);
            std::vector<std::unordered_set<std::size_t>> columnRows(size);
            for(std::size_t i=0;i<size;++i)for(const auto& item:rows[i])columnRows[item.first].insert(i);
            for(std::size_t k=0;k<size;++k) {
                std::size_t p=k; double largest=0.0;
                for(const std::size_t i:columnRows[k]) if(i>=k) { auto it=rows[i].find(k); const double v=it==rows[i].end()?0.0:std::abs(it->second); if(v>largest){largest=v;p=i;} }
                if(!(largest>pivotTolerance)||!std::isfinite(largest)) {failedAt=k;failedPivot=largest;return false;}
                if(p!=k){
                    for(const auto& item:rows[p])columnRows[item.first].erase(p);
                    for(const auto& item:rows[k])columnRows[item.first].erase(k);
                    std::swap(rows[p],rows[k]);std::swap(permutation[p],permutation[k]);
                    for(const auto& item:rows[p])columnRows[item.first].insert(p);
                    for(const auto& item:rows[k])columnRows[item.first].insert(k);
                }
                const double pivot=rows[k].at(k);
                std::vector<std::size_t> affected;
                affected.reserve(columnRows[k].size());
                for(const std::size_t i:columnRows[k])if(i>k)affected.push_back(i);
                for(const std::size_t i:affected) {
                    auto ik=rows[i].find(k); if(ik==rows[i].end()) continue;
                    const double multiplier=ik->second/pivot; ik->second=multiplier;
                    for(const auto& item:rows[k]) if(item.first>k) {
                        const std::size_t j=item.first; const double u=item.second;
                        auto ij=rows[i].find(j); const double old=ij==rows[i].end()?0.0:ij->second;
                        const double updated=old-multiplier*u;
                        if(updated==0.0) {if(ij!=rows[i].end()){rows[i].erase(ij);columnRows[j].erase(i);}}
                        else {if(ij==rows[i].end())columnRows[j].insert(i);rows[i][j]=updated;}
                    }
                }
            }
            upperColumns.assign(size,{}); lowerColumns.assign(size,{});
            for(std::size_t i=0;i<size;++i) for(const auto& item:rows[i]) {
                const std::size_t j=item.first; const double v=item.second;
                if(j>i) upperColumns[j].emplace_back(i,v);
                else if(j<i) lowerColumns[j].emplace_back(i,v);
            }
            return true;
        }
        bool solve(const std::vector<double>& b,std::vector<double>& x) const {
            if(b.size()!=size)return false;x.resize(size);
            for(std::size_t i=0;i<size;++i)x[i]=b[permutation[i]];
            for(std::size_t i=0;i<size;++i)for(const auto& item:rows[i])if(item.first<i)x[i]-=item.second*x[item.first];
            for(std::size_t ii=0;ii<size;++ii){const std::size_t i=size-1-ii;for(const auto& item:rows[i])if(item.first>i)x[i]-=item.second*x[item.first];auto d=rows[i].find(i);if(d==rows[i].end()||d->second==0.0)return false;x[i]/=d->second;if(!std::isfinite(x[i]))return false;}
            return true;
        }
        bool solveTranspose(const std::vector<double>& b,std::vector<double>& x,std::vector<double>& work) const {
            if(b.size()!=size)return false;work.assign(b.begin(),b.end());
            for(std::size_t i=0;i<size;++i){for(const auto& item:upperColumns[i])work[i]-=item.second*work[item.first];auto d=rows[i].find(i);if(d==rows[i].end()||d->second==0.0)return false;work[i]/=d->second;}
            for(std::size_t ii=0;ii<size;++ii){const std::size_t i=size-1-ii;for(const auto& item:lowerColumns[i])work[i]-=item.second*work[item.first];}
            x.resize(size);std::fill(x.begin(),x.end(),0.0);for(std::size_t i=0;i<size;++i)x[permutation[i]]=work[i];
            return std::all_of(x.begin(),x.end(),[](double v){return std::isfinite(v);});
        }
    };
    struct EtaUpdate { std::size_t row; std::vector<double> direction; };
    LPResult solveSparse(const Model& m, LPMethod method, std::size_t limit) const {
        LPResult out; out.method=methodName(method);
        out.estimatedDenseMemoryBytes=static_cast<double>(estimateDenseLPBytes(m));
        double pricingMs=0.0,basisSolveMs=0.0,devexMs=0.0,factorizationMs=0.0,ratioTestMs=0.0,lexicographicMs=0.0;
        std::size_t refactorizations=0,pivots=0,lexicographicSolves=0;
        bool blandFallbackTriggered=false;
        struct SparseMetricsScope {
            LPResult& result; double& pricing; double& basis; double& devex; double& factorization; double& ratio; double& lexicographic;
            std::size_t& refactorizations; std::size_t& pivots; std::size_t& lexicographicSolves; bool& blandFallbackTriggered;
            void publish() {
                result.sparsePricingTimeMs=pricing; result.sparseBasisSolveTimeMs=basis;
                result.sparseDevexTimeMs=devex; result.sparseFactorizationTimeMs=factorization;
                result.sparseRatioTestTimeMs=ratio; result.sparseLexicographicTimeMs=lexicographic;
                result.sparseRefactorizations=refactorizations; result.sparsePivots=pivots;
                result.sparseLexicographicSolves=lexicographicSolves;
                result.sparseBlandFallbackTriggered=blandFallbackTriggered;
            }
            ~SparseMetricsScope() { publish(); }
        } metricsScope{out,pricingMs,basisSolveMs,devexMs,factorizationMs,ratioTestMs,lexicographicMs,refactorizations,pivots,lexicographicSolves,blandFallbackTriggered};
        // Explicitly publish before returning by value. Return-value moves can
        // happen before local destructors run, which otherwise dropped all
        // sparse stage timings and counters from the returned LPResult.
        auto finish=[&](){metricsScope.publish();return out;};
        auto addElapsed=[](double& target,const std::chrono::steady_clock::time_point& start) {
            target+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        };
        const auto standardizationStart=std::chrono::steady_clock::now();
        SparseStandardLP s=standardizeSparse(m,tol_);
        out.standardizedRows=s.A.size(); out.standardizedColumns=s.c.size();
        for(const auto& row:s.A)out.standardizedNonzeros+=row.size();
        out.standardizationTimeMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-standardizationStart).count();
        const std::size_t n=s.c.size(), R=s.A.size();
        if(n==0) { out.status=LPStatus::NumericalFailure; out.message="LP has no active transformed variables"; return finish(); }
        std::vector<bool> needsArtificial(R,false);
        std::size_t artificialCount=0;
        for(std::size_t i=0;i<R;++i) if(s.b[i]<-tol_.feasibility) { needsArtificial[i]=true; ++artificialCount; }
        const std::size_t slackStart=n, artificialStart=n+R, total=artificialStart+artificialCount;
        std::vector<SparseColumn> columns(total);
        for(std::size_t i=0;i<R;++i) {
            const double sign=needsArtificial[i]?-1.0:1.0;
            for(const auto& entry:s.A[i]) if(entry.second!=0.0) columns[entry.first].emplace_back(i,sign*entry.second);
            columns[slackStart+i].emplace_back(i,sign);
        }
        std::vector<std::size_t> basis(R);
        std::vector<bool> artificial(total,false), basic(total,false);
        std::vector<double> rhs(R), xB(R);
        std::size_t nextArtificial=artificialStart;
        for(std::size_t i=0;i<R;++i) {
            rhs[i]=needsArtificial[i]?-s.b[i]:s.b[i]; xB[i]=rhs[i];
            if(needsArtificial[i]) { basis[i]=nextArtificial; artificial[nextArtificial]=true; columns[nextArtificial].emplace_back(i,1.0); ++nextArtificial; }
            else basis[i]=slackStart+i;
            basic[basis[i]]=true;
        }
        std::vector<double> devexWeights(total,1.0), devexResetWeights(total,1.0);
        for(std::size_t j=0;j<total;++j) {
            long double columnNormSquared=0.0L;
            for(const auto& entry:columns[j]) columnNormSquared+=static_cast<long double>(entry.second)*entry.second;
            devexWeights[j]=devexResetWeights[j]=std::max(1e-12,static_cast<double>(columnNormSquared));
        }
        std::size_t devexUpdatesSinceReset=0;
        constexpr std::size_t sparseRefactorInterval=15;
        SparseLU factorization; std::vector<EtaUpdate> updates; updates.reserve(sparseRefactorInterval); std::string factorFailure;
        auto refactorBasis=[&]()->bool {
            const auto factorizationStart=std::chrono::steady_clock::now();
            ++refactorizations;
            std::vector<std::unordered_map<std::size_t,double>> matrix(R);
            for(std::size_t j=0;j<R;++j) for(const auto& item:columns[basis[j]]) matrix[item.first][j]=item.second;
            if(!factorization.factor(std::move(matrix),1e-30)) {addElapsed(factorizationMs,factorizationStart);factorFailure="sparse LU failed at pivot "+std::to_string(factorization.failedAt)+" (magnitude "+std::to_string(factorization.failedPivot)+")";return false;}
            updates.clear();
            if(!factorization.solve(rhs,xB)) {addElapsed(factorizationMs,factorizationStart);factorFailure="sparse LU solve failed for the basis right-hand side";return false;}
            for(double& v:xB) if(v<0.0&&v>-100.0*tol_.feasibility*(1.0+std::abs(v)))v=0.0;
            addElapsed(factorizationMs,factorizationStart);
            return true;
        };
        auto solveCurrent=[&](const std::vector<double>& v,std::vector<double>& result)->bool {
            if(!factorization.solve(v,result)) return false;
            for(const auto& eta:updates) {
                const double p=eta.direction[eta.row]; if(std::abs(p)<=tol_.pivot)return false;
                const double q=result[eta.row]/p;
                for(std::size_t i=0;i<R;++i) if(i!=eta.row) result[i]-=eta.direction[i]*q;
                result[eta.row]=q;
            }
            return true;
        };
        std::vector<double> transposeWork(R);
        auto solveCurrentTranspose=[&](std::vector<double>& transformed,std::vector<double>& result)->bool {
            for(std::size_t u=updates.size();u>0;--u) {
                const auto& eta=updates[u-1]; const std::size_t r=eta.row;
                double other=0.0;for(std::size_t i=0;i<R;++i)if(i!=r)other+=eta.direction[i]*transformed[i];
                const double p=eta.direction[r];if(std::abs(p)<=tol_.pivot)return false;
                transformed[r]=(transformed[r]-other)/p;
            }
            return factorization.solveTranspose(transformed,result,transposeWork);
        };
        if(!refactorBasis()) {out.status=LPStatus::NumericalFailure;out.message="initial sparse basis factorization failed";return finish();}
        std::string simplexFailure;
        std::vector<double> cb(R,0.0), y(R,0.0), enteringColumn(R,0.0), direction(R,0.0);
        std::vector<double> devexUnit(R,0.0), devexRow(R,0.0);
        bool blandPhaseOne=false;
        std::unordered_set<std::size_t> phaseOneBasisHistory;
        phaseOneBasisHistory.reserve(std::min<std::size_t>(limit,4096));
        std::vector<std::size_t> phaseOneRatioTies;
        phaseOneRatioTies.reserve(R);
        auto simplex=[&](bool phaseOne,std::size_t maxIterations,std::size_t& iterations)->LPStatus {
            for(std::size_t it=0;it<maxIterations;++it) {
                if(phaseOne && !blandPhaseOne) {
                    std::size_t hash=static_cast<std::size_t>(1469598103934665603ULL);
                    for(const std::size_t variable:basis) { hash^=variable+1; hash*=static_cast<std::size_t>(1099511628211ULL); }
                    // A repeated basis switches the remaining auxiliary pivots
                    // to Bland entering/leaving order. Hash collisions only
                    // activate the conservative anti-cycling mode early.
                    if(!phaseOneBasisHistory.insert(hash).second) { blandPhaseOne=true; blandFallbackTriggered=true; }
                }
                std::fill(cb.begin(),cb.end(),0.0);
                for(std::size_t i=0;i<R;++i) cb[i]=phaseOne?(artificial[basis[i]]?-1.0:0.0):(basis[i]<n?s.c[basis[i]]:0.0);
                auto stageStart=std::chrono::steady_clock::now();
                if(!solveCurrentTranspose(cb,y)) {addElapsed(basisSolveMs,stageStart);iterations=it;simplexFailure="sparse dual basis solve failed";return LPStatus::NumericalFailure;}
                addElapsed(basisSolveMs,stageStart);
                stageStart=std::chrono::steady_clock::now();
                std::size_t enter=total; double mostPositive=0.0;
                for(std::size_t j=0;j<total;++j) {
                    if(basic[j] || artificial[j]) continue;
                    double reduced=phaseOne?0.0:(j<n?s.c[j]:0.0);
                    for(const auto& e:columns[j]) reduced-=e.second*y[e.first];
                    if(reduced>tol_.optimality) {
                        if(phaseOne && blandPhaseOne) {
                            if(enter==total) { mostPositive=reduced; enter=j; }
                        } else {
                            const double score=reduced/std::sqrt(devexWeights[j]);
                            if(score>mostPositive) { mostPositive=score; enter=j; }
                        }
                    }
                }
                addElapsed(pricingMs,stageStart);
                if(enter==total) { iterations=it; return LPStatus::Optimal; }
                std::fill(enteringColumn.begin(),enteringColumn.end(),0.0);
                for(const auto& e:columns[enter]) enteringColumn[e.first]=e.second;
                stageStart=std::chrono::steady_clock::now();
                if(!solveCurrent(enteringColumn,direction)) {addElapsed(basisSolveMs,stageStart);iterations=it;simplexFailure="sparse primal basis solve failed";return LPStatus::NumericalFailure;}
                addElapsed(basisSolveMs,stageStart);
                const double stablePivot=std::max(tol_.pivot,1e-8);
                stageStart=std::chrono::steady_clock::now();
                double minRatio=std::numeric_limits<double>::infinity();
                for(std::size_t i=0;i<R;++i) if(direction[i]>stablePivot)
                    minRatio=std::min(minRatio,std::max(0.0,xB[i])/direction[i]);
                if(!std::isfinite(minRatio)) { iterations=it; return LPStatus::Unbounded; }
                const double relaxed=minRatio+tol_.feasibility*(1.0+std::abs(minRatio));
                std::size_t leave=R; double bestPivot=-1.0;
                if(phaseOne) {
                    phaseOneRatioTies.clear();
                    for(std::size_t i=0;i<R;++i) if(direction[i]>stablePivot &&
                        std::max(0.0,xB[i])/direction[i]==minRatio) phaseOneRatioTies.push_back(i);
                    // Exact lexicographic perturbation is valuable on small
                    // tie sets. On large sets its repeated transpose solves
                    // cost more than the sparse pivot itself; use a stable
                    // row tie-break and activate full Bland order if a basis
                    // actually repeats. The work budget scales with tie count.
                    const bool useLexicographic = !blandPhaseOne && phaseOneRatioTies.size()>1 &&
                        phaseOneRatioTies.size() <= std::max<std::size_t>(1,8192/std::max<std::size_t>(R,1));
                    if(useLexicographic) {
                        leave=phaseOneRatioTies.front();
                        std::vector<double> leaveLex;
                        for(std::size_t index=1;index<phaseOneRatioTies.size();++index) {
                            const std::size_t i=phaseOneRatioTies[index];
                            std::vector<double> unit(R,0.0), candidateLex(R); unit[i]=1.0;
                            auto lexStart=std::chrono::steady_clock::now(); ++lexicographicSolves;
                            if(!solveCurrentTranspose(unit,candidateLex)){addElapsed(lexicographicMs,lexStart);iterations=it;simplexFailure="sparse lexicographic ratio solve failed";return LPStatus::NumericalFailure;}
                            addElapsed(lexicographicMs,lexStart);
                            if(leaveLex.empty()) {std::vector<double> leavingUnit(R,0.0);leavingUnit[leave]=1.0;lexStart=std::chrono::steady_clock::now();++lexicographicSolves;if(!solveCurrentTranspose(leavingUnit,leaveLex)){addElapsed(lexicographicMs,lexStart);iterations=it;simplexFailure="sparse lexicographic tie solve failed";return LPStatus::NumericalFailure;}addElapsed(lexicographicMs,lexStart);}
                            for(std::size_t k=0;k<R;++k) {
                                const double lhs=candidateLex[k]/direction[i];
                                const double rhsLex=leaveLex[k]/direction[leave];
                                if(lhs<rhsLex) { leave=i;leaveLex=std::move(candidateLex);break; }
                                if(lhs>rhsLex) break;
                            }
                        }
                    } else {
                        for(const std::size_t i:phaseOneRatioTies)
                            if(leave==R || (blandPhaseOne ? basis[i]<basis[leave] : i<leave)) leave=i;
                    }
                } else {
                    for(std::size_t i=0;i<R;++i) if(direction[i]>stablePivot) {
                        const double ratio=std::max(0.0,xB[i])/direction[i];
                        if(ratio<=relaxed && direction[i]>bestPivot) { leave=i; bestPivot=direction[i]; }
                    }
                }
                addElapsed(ratioTestMs,stageStart);
                if(leave==R) { iterations=it; return LPStatus::NumericalFailure; }
                const double pivotValue=direction[leave];
                const double theta=std::max(0.0,xB[leave])/pivotValue;
                stageStart=std::chrono::steady_clock::now();
                std::fill(devexUnit.begin(),devexUnit.end(),0.0); devexUnit[leave]=1.0;
                if(!solveCurrentTranspose(devexUnit,devexRow)) {addElapsed(devexMs,stageStart);iterations=it;simplexFailure="sparse Devex row solve failed";return LPStatus::NumericalFailure;}
                long double directionNormSquared=0.0L;
                for(double value:direction) directionNormSquared+=static_cast<long double>(value)*value;
                const double pivotSquared=pivotValue*pivotValue;
                const double leavingNormSquared=(1.0+static_cast<double>(directionNormSquared))/pivotSquared;
                for(std::size_t j=0;j<total;++j) if(!basic[j] && j!=enter) {
                    if(artificial[j])continue;
                    double rowCoefficient=0.0;
                    for(const auto& entry:columns[j]) rowCoefficient+=entry.second*devexRow[entry.first];
                    const double approximateNorm=rowCoefficient*rowCoefficient*leavingNormSquared;
                    if(std::isfinite(approximateNorm)) devexWeights[j]=std::max(devexWeights[j],approximateNorm);
                }
                devexWeights[basis[leave]]=leavingNormSquared;
                if(++devexUpdatesSinceReset>=150) {
                    devexWeights=devexResetWeights;
                    devexUpdatesSinceReset=0;
                }
                addElapsed(devexMs,stageStart);
                updates.push_back({leave,direction});
                for(std::size_t i=0;i<R;++i) if(i!=leave) {
                    const double q=direction[i];
                    xB[i]-=theta*q;
                    if(xB[i]<0.0 && xB[i]>-100.0*tol_.feasibility*(1.0+theta)) xB[i]=0.0;
                }
                xB[leave]=theta;
                basic[basis[leave]]=false; basis[leave]=enter; basic[enter]=true;
                ++pivots;
                iterations=it+1;
                if((it+1)%sparseRefactorInterval==0 && !refactorBasis()) {iterations=it+1;simplexFailure=factorFailure;return LPStatus::NumericalFailure;}
                if(it+1==maxIterations) return LPStatus::IterationLimit;
            }
            return LPStatus::IterationLimit;
        };
        std::size_t phaseOneIterations=0;
        LPStatus status=simplex(true,limit,phaseOneIterations); out.iterations=phaseOneIterations;
        if(status==LPStatus::IterationLimit) { out.status=status; out.message="phase-I iteration limit"; return finish(); }
        if(status==LPStatus::Unbounded) { out.status=LPStatus::NumericalFailure; out.message="phase-I auxiliary objective was incorrectly reported unbounded"; return finish(); }
        if(status!=LPStatus::Optimal) { out.status=status; out.message=simplexFailure.empty()?"phase I did not find a feasible basis":simplexFailure; return finish(); }
        double artificialSum=0.0;
        for(std::size_t i=0;i<R;++i) if(artificial[basis[i]]) artificialSum+=std::max(0.0,xB[i]);
        if(artificialSum>tol_.feasibility*(1.0+std::accumulate(s.b.begin(),s.b.end(),0.0,[](double a,double b){return a+std::abs(b);}))) {
            out.status=LPStatus::Infeasible; out.message="phase I found no feasible basis"; return finish();
        }
        // Replace zero artificial basics with any available real or slack column.
        for(std::size_t row=0;row<R;++row) if(artificial[basis[row]]) {
            std::size_t replacement=total; double best=tol_.pivot;
            for(std::size_t j=0;j<artificialStart && replacement==total;++j) if(!basic[j]) {
                std::vector<double> enteringColumn(R,0.0),direction;
                for(const auto& e:columns[j])enteringColumn[e.first]=e.second;
                if(!solveCurrent(enteringColumn,direction))continue;
                if(std::abs(direction[row])>best){best=std::abs(direction[row]);replacement=j;}
            }
            if(replacement<artificialStart) { basic[basis[row]]=false; basis[row]=replacement; basic[replacement]=true; }
        }
        if(!refactorBasis()) {out.status=LPStatus::NumericalFailure;out.message="sparse basis factorization failed after phase I";return finish();}
        std::size_t phaseTwoIterations=0; out.status=simplex(false,limit,phaseTwoIterations); out.iterations+=phaseTwoIterations;
        if(out.status!=LPStatus::Optimal) { out.message=out.status==LPStatus::Unbounded?"phase-II objective is unbounded":"phase-II solve did not converge"; return finish(); }
        if(!refactorBasis()) {out.status=LPStatus::NumericalFailure;out.message="final sparse basis refactorization failed";return finish();}
        std::vector<double> z(n,0.0);
        for(std::size_t i=0;i<R;++i) if(basis[i]<n) z[basis[i]]=std::max(0.0,xB[i]);
        out.solution.primal.assign(m.variables.size(),0.0);
        for(std::size_t j=0;j<n;++j) out.solution.primal[s.map[j].first]+=s.map[j].second*z[j];
        for(std::size_t i=0;i<m.variables.size();++i) if(m.variables[i].active&&std::isfinite(m.variables[i].lower)) out.solution.primal[i]+=m.variables[i].lower;
        out.objectiveValue=evaluateObjective(m,out.solution.primal); out.solution.objectiveValue=out.objectiveValue; out.solution.feasibilityResidual=verifyResidual(m,out.solution.primal);
        if(!std::isfinite(out.objectiveValue)||verifyScaledResidual(m,out.solution.primal)>std::max(100.0*tol_.feasibility,1e-7)) {
            out.status=LPStatus::NumericalFailure; out.message="candidate failed original-model primal feasibility check"; return finish();
        }
        out.basisVariables=basis;
        return finish();
    }
    static void pivot(std::vector<std::vector<double>>&t,size_t r,size_t c,std::vector<std::size_t>&nonzeroColumns){
        auto& pivotRow=t[r];const double inverse=1.0/pivotRow[c];
        for(double& value:pivotRow)value*=inverse;
        nonzeroColumns.clear();
        for(size_t j=0;j<pivotRow.size();++j)if(pivotRow[j]!=0.0)nonzeroColumns.push_back(j);
        for(size_t i=0;i<t.size();++i)if(i!=r){auto& row=t[i];const double factor=row[c];if(factor==0.0)continue;for(const size_t j:nonzeroColumns)row[j]-=factor*pivotRow[j];row[c]=0.0;}
    }
    static double verifyResidual(const Model&m,const std::vector<double>&x){double r=0;for(auto&c:m.constraints){if(!c.active)continue;double a=0;for(auto p:c.coefficients)a+=p.second*x[p.first];r=std::max(r,c.relation==Relation::Equal?std::abs(a-c.rhs):c.relation==Relation::LessEqual?std::max(0.,a-c.rhs):std::max(0.,c.rhs-a));}return r;}
    static double verifyScaledResidual(const Model&m,const std::vector<double>&x){
        if(x.size()!=m.variables.size())return std::numeric_limits<double>::infinity();double worst=0;
        for(std::size_t i=0;i<m.variables.size();++i){const auto&v=m.variables[i];if(!v.active)continue;if(!std::isfinite(x[i]))return std::numeric_limits<double>::infinity();double d=0;if(std::isfinite(v.lower))d=std::max(d,v.lower-x[i]);if(std::isfinite(v.upper))d=std::max(d,x[i]-v.upper);double scale=1+std::abs(x[i])+(std::isfinite(v.lower)?std::abs(v.lower):0)+(std::isfinite(v.upper)?std::abs(v.upper):0);worst=std::max(worst,std::max(0.0,d)/scale);}
        for(const auto&c:m.constraints){if(!c.active)continue;double a=0,scale=1+std::abs(c.rhs);for(auto p:c.coefficients){if(p.first>=x.size())return std::numeric_limits<double>::infinity();a+=p.second*x[p.first];scale+=std::abs(p.second*x[p.first]);}double d=c.relation==Relation::Equal?std::abs(a-c.rhs):c.relation==Relation::LessEqual?std::max(0.0,a-c.rhs):std::max(0.0,c.rhs-a);worst=std::max(worst,d/scale);}return worst;
    }
};
}







