#pragma once

#include "lp/LPSolver.hpp"
#include "cuda/CudaBackend.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace sovereign {

class PDHGSolver {
public:
    explicit PDHGSolver(Tolerance tolerance = {}) : tolerance_(tolerance) {}

    LPResult solve(const Model& model, std::size_t limit = 10000) const {
        LPResult result;
        result.method = "pdhg";
        result.estimatedDenseMemoryBytes = static_cast<double>(estimateDenseLPBytes(model));
        for (const auto& variable : model.variables) {
            if (variable.type != VariableType::Continuous) {
                result.status = LPStatus::Unsupported;
                result.message = "PDHG supports continuous linear programs only";
                return result;
            }
        }
        if (!model.quadratic.empty()) {
            result.status = LPStatus::Unsupported;
            result.message = "PDHG does not support quadratic objectives";
            return result;
        }
        if (limit == 0 || limit == std::numeric_limits<std::size_t>::max())
            limit = std::numeric_limits<std::size_t>::max();

        const auto standardizationStart=std::chrono::steady_clock::now();
        const SparseStandardLP standard = standardizeSparse(model, tolerance_);
        const std::size_t rows = standard.A.size(), columns = standard.c.size();
        result.standardizedRows=rows; result.standardizedColumns=columns;
        if (!columns) {
            result.status = LPStatus::NumericalFailure;
            result.message = "LP has no active transformed variables";
            return result;
        }

        std::vector<nla::Triplet> entries;
        std::size_t nonzeros = 0;
        for (const auto& row : standard.A) nonzeros += row.size();
        result.standardizedNonzeros=nonzeros;
        result.standardizationTimeMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-standardizationStart).count();
        entries.reserve(nonzeros);
        std::vector<double> rowNorm(rows, 0.0), columnNorm(columns, 0.0);
        for (std::size_t i = 0; i < rows; ++i) {
            for (const auto& entry : standard.A[i]) {
                entries.push_back({i, entry.first, entry.second});
                rowNorm[i] += std::abs(entry.second);
                columnNorm[entry.first] += std::abs(entry.second);
            }
        }
        nla::CSRMatrix matrix(rows, columns, entries, tolerance_.zero);
        // standardizeSparse exposes an equivalent MAX objective; PDHG uses
        // the minimization convention and therefore negates that vector.
        std::vector<double> cost(columns);
        double objectiveScale = 0.0;
        for (std::size_t j = 0; j < columns; ++j) {
            cost[j] = -standard.c[j];
            objectiveScale = std::max(objectiveScale, std::abs(cost[j]));
        }
        if (objectiveScale > 0.0)
            for (double& coefficient : cost) coefficient /= objectiveScale;

        for (std::size_t i = 0; i < rows; ++i)
            if (rowNorm[i] <= tolerance_.zero && standard.b[i] < -tolerance_.feasibility) {
                result.status = LPStatus::Infeasible;
                result.message = "PDHG detected an infeasible empty constraint row";
                return result;
            }
        for (std::size_t j = 0; j < columns; ++j)
            if (columnNorm[j] <= tolerance_.zero && cost[j] < -tolerance_.optimality) {
                result.status = LPStatus::Unbounded;
                result.message = "PDHG detected an improving variable with no constraint coefficients";
                return result;
            }

        std::vector<double> tau(columns, 1.0), sigma(rows, 1.0);
        for (std::size_t j = 0; j < columns; ++j)
            if (columnNorm[j] > tolerance_.zero) tau[j] = 0.99 / columnNorm[j];
        for (std::size_t i = 0; i < rows; ++i)
            if (rowNorm[i] > tolerance_.zero) sigma[i] = 0.99 / rowNorm[i];

        std::vector<double> primal(columns, 0.0), dual(rows, 0.0);
        std::vector<double> extrapolated(columns, 0.0), activity(rows), gradient(columns);
        double primalResidual = std::numeric_limits<double>::infinity();
        double dualResidual = std::numeric_limits<double>::infinity();
        double complementarity = std::numeric_limits<double>::infinity();
        std::size_t iterations = 0;
        constexpr std::size_t checkInterval = 25;
        constexpr double target = 1e-7;

        cuda::Context* gpu = cuda::Context::defaultContext();
        bool completedOnGpu = false;
        if (gpu && gpu->available()) {
            completedOnGpu = gpu->solvePdhg(matrix, standard.b, cost, tau, sigma,
                limit, target, primal, dual, iterations, primalResidual,
                dualResidual, complementarity);
        }

        if (!completedOnGpu) {
            const auto& rowPtr = matrix.rowPointers();
            const auto& columnIndex = matrix.columnIndices();
            const auto& values = matrix.values();
            const auto multiply = [&](const std::vector<double>& x, std::vector<double>& y) {
                std::fill(y.begin(), y.end(), 0.0);
                for (std::size_t i = 0; i < rows; ++i)
                    for (std::size_t k = rowPtr[i]; k < rowPtr[i + 1]; ++k)
                        y[i] += values[k] * x[columnIndex[k]];
            };
            const auto transposeMultiply = [&](const std::vector<double>& y, std::vector<double>& x) {
                std::fill(x.begin(), x.end(), 0.0);
                for (std::size_t i = 0; i < rows; ++i)
                    for (std::size_t k = rowPtr[i]; k < rowPtr[i + 1]; ++k)
                        x[columnIndex[k]] += values[k] * y[i];
            };

            const auto measure = [&]() {
                multiply(primal, activity);
                transposeMultiply(dual, gradient);
                for (std::size_t j = 0; j < columns; ++j) gradient[j] += cost[j];
                primalResidual = dualResidual = complementarity = 0.0;
                std::vector<double> stationarityScale(columns, 1.0);
                for (std::size_t j = 0; j < columns; ++j)
                    stationarityScale[j] += std::abs(cost[j]);
                for (std::size_t i = 0; i < rows; ++i) {
                    double activityScale = 1.0 + std::abs(standard.b[i]);
                    double dualScale = std::abs(standard.b[i]);
                    for (std::size_t k = rowPtr[i]; k < rowPtr[i + 1]; ++k) {
                        const auto j = columnIndex[k];
                        activityScale += std::abs(values[k] * primal[j]);
                        dualScale += std::abs(values[k] * primal[j]);
                        stationarityScale[j] += std::abs(values[k] * dual[i]);
                    }
                    primalResidual = std::max(primalResidual,
                        std::max(0.0, activity[i] - standard.b[i]) / activityScale);
                    complementarity = std::max(complementarity,
                        std::abs(dual[i] * (standard.b[i] - activity[i])) /
                        (1.0 + std::abs(dual[i]) * dualScale));
                }
                for (std::size_t j = 0; j < columns; ++j) {
                    const double residual = primal[j] > 1e-8
                        ? std::abs(gradient[j]) : std::max(0.0, -gradient[j]);
                    dualResidual = std::max(dualResidual, residual / stationarityScale[j]);
                }
            };

            for (iterations = 0; iterations < limit;) {
                ++iterations;
                multiply(extrapolated, activity);
                for (std::size_t i = 0; i < rows; ++i)
                    dual[i] = std::max(0.0, dual[i] + sigma[i] * (activity[i] - standard.b[i]));
                transposeMultiply(dual, gradient);
                for (std::size_t j = 0; j < columns; ++j) {
                    const double previous = primal[j];
                    primal[j] = std::max(0.0, previous - tau[j] * (cost[j] + gradient[j]));
                    extrapolated[j] = 2.0 * primal[j] - previous;
                }
                if (iterations % checkInterval == 0 || iterations == limit) {
                    measure();
                    if (primalResidual <= target && dualResidual <= target && complementarity <= target)
                        break;
                }
            }
        }

        result.iterations = iterations;
        result.solution.primal.assign(model.variables.size(), 0.0);
        for (std::size_t j = 0; j < columns; ++j)
            result.solution.primal[standard.map[j].first] += standard.map[j].second * primal[j];
        for (const auto& variable : model.variables)
            if (variable.active && std::isfinite(variable.lower))
                result.solution.primal[variable.originalId] += variable.lower;
        result.objectiveValue = evaluateObjective(model, result.solution.primal);
        result.solution.objectiveValue = result.objectiveValue;
        result.solution.optimalityResidual = dualResidual;
        result.solution.feasibilityResidual = originalResidual(model, result.solution.primal);
        if (primalResidual <= target && dualResidual <= target && complementarity <= target &&
            scaledOriginalResidual(model, result.solution.primal) <= 1e-7) {
            result.status = LPStatus::Optimal;
            result.message = completedOnGpu ? "PDHG converged on CUDA" : "PDHG converged";
        } else if (iterations >= limit) {
            result.status = LPStatus::IterationLimit;
            result.message = "PDHG reached the configured iteration limit before meeting primal, dual, and complementarity tolerances";
        } else {
            result.status = LPStatus::NumericalFailure;
            result.message = "PDHG failed to meet original-model feasibility and optimality checks";
        }
        return result;
    }

private:
    Tolerance tolerance_;

    static double originalResidual(const Model& model, const std::vector<double>& x) {
        double residual = 0.0;
        for (const auto& variable : model.variables) {
            if (variable.originalId >= x.size()) return std::numeric_limits<double>::infinity();
            if (std::isfinite(variable.lower)) residual = std::max(residual, variable.lower - x[variable.originalId]);
            if (std::isfinite(variable.upper)) residual = std::max(residual, x[variable.originalId] - variable.upper);
        }
        for (const auto& row : model.constraints) if (row.active) {
            double activity = 0.0;
            for (const auto& coefficient : row.coefficients) {
                const double term = coefficient.second * x[coefficient.first];
                activity += term;
            }
            double violation = row.relation == Relation::Equal ? std::abs(activity - row.rhs)
                : row.relation == Relation::LessEqual ? std::max(0.0, activity - row.rhs)
                : std::max(0.0, row.rhs - activity);
            residual = std::max(residual, violation);
        }
        return std::max(0.0, residual);
    }

    static double scaledOriginalResidual(const Model& model, const std::vector<double>& x) {
        double residual = 0.0;
        for (const auto& variable : model.variables) {
            const double value = x[variable.originalId];
            double violation = 0.0;
            if (std::isfinite(variable.lower)) violation = std::max(violation, variable.lower - value);
            if (std::isfinite(variable.upper)) violation = std::max(violation, value - variable.upper);
            residual = std::max(residual, std::max(0.0, violation) / (1.0 + std::abs(value)));
        }
        for (const auto& row : model.constraints) if (row.active) {
            double activity = 0.0, scale = 1.0 + std::abs(row.rhs);
            for (const auto& coefficient : row.coefficients) {
                const double term = coefficient.second * x[coefficient.first];
                activity += term;
                scale += std::abs(term);
            }
            const double violation = row.relation == Relation::Equal ? std::abs(activity - row.rhs)
                : row.relation == Relation::LessEqual ? std::max(0.0, activity - row.rhs)
                : std::max(0.0, row.rhs - activity);
            residual = std::max(residual, violation / scale);
        }
        return residual;
    }
};

} // namespace sovereign
