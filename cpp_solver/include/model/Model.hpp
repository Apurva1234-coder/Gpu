#pragma once
#include <algorithm>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>
#include <cmath>
namespace sovereign {
constexpr double INF = std::numeric_limits<double>::infinity();
enum class VariableType { Continuous, Integer, Binary };
enum class Sense { Minimize, Maximize };
enum class Relation { Equal, LessEqual, GreaterEqual };
struct Variable { std::size_t originalId{}, activeId{}; std::string name; VariableType type{VariableType::Continuous}; double lower{0}, upper{INF}; bool active{true}; };
struct Constraint { std::size_t originalId{}, activeId{}; std::string name; std::unordered_map<std::size_t,double> coefficients; Relation relation{Relation::Equal}; double rhs{0}; bool active{true}; };
struct Model {
    std::string name;
    Sense sense{Sense::Minimize};
    std::vector<Variable> variables;
    std::unordered_map<std::size_t,double> objective;
    std::vector<Constraint> constraints;
    std::unordered_map<std::size_t,double> quadratic;
    std::unordered_map<std::size_t,std::unordered_map<std::size_t,double>> quadraticMatrix;
    double objectiveConstant{0}; std::unordered_map<std::size_t,std::size_t> originalToActive, activeToOriginal, constraintOriginalToActive, constraintActiveToOriginal;
    void rebuildMappings(){ originalToActive.clear(); activeToOriginal.clear(); constraintOriginalToActive.clear(); constraintActiveToOriginal.clear(); std::size_t a=0; for(auto& v:variables) if(v.active){v.activeId=a;originalToActive[v.originalId]=a;activeToOriginal[a++]=v.originalId;} a=0; for(auto& c:constraints) if(c.active){c.activeId=a;constraintOriginalToActive[c.originalId]=a;constraintActiveToOriginal[a++]=c.originalId;} }
};
}
