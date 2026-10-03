#include "presolve/Presolver.hpp"
#include "model/Input.hpp"
#include "model/Classification.hpp"
#include "lp/LPSolver.hpp"
#include "lp/DualSimplex.hpp"
#include "lp/MehrotraIPM.hpp"
#include "lp/PDHGSolver.hpp"
#include "presolve/Postsolve.hpp"
#include "qp/Convexity.hpp"
#include "qp/QPIPM.hpp"
#include "qp/GeneralQP.hpp"
#include "milp/LPRelaxation.hpp"
#include "milp/BranchAndBound.hpp"
#include "milp/CuttingPlane.hpp"
#include "milp/FeasibilityPump.hpp"
#include "cuda/CudaBackend.hpp"
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
using namespace sovereign;

static std::string optionalMetric(double value, bool available) {
  if (!available) return "N/A";
  std::ostringstream text;
  text << std::setprecision(17) << value;
  return text.str();
}
static void printPrimalNames(const Model& model){std::cout<<"Primal Names:";for(const auto& variable:model.variables)std::cout<<" "<<variable.name;std::cout<<"\n";}
struct PrimalCheck { bool feasible{true}; double absoluteResidual{}, scaledResidual{}; };
static PrimalCheck checkPrimal(const Model& model,const Solution& solution,double tolerance=1e-7){
 PrimalCheck check;
 if(solution.primal.size()!=model.variables.size()){check.feasible=false;check.scaledResidual=std::numeric_limits<double>::infinity();return check;}
 for(std::size_t i=0;i<model.variables.size();++i){const auto& v=model.variables[i];const double x=solution.primal[i];if(!std::isfinite(x)){check.feasible=false;check.scaledResidual=std::numeric_limits<double>::infinity();return check;}if(!v.active)continue;double violation=0;if(std::isfinite(v.lower))violation=std::max(violation,v.lower-x);if(std::isfinite(v.upper))violation=std::max(violation,x-v.upper);violation=std::max(0.0,violation);const double scale=1.0+std::abs(x)+(std::isfinite(v.lower)?std::abs(v.lower):0.0)+(std::isfinite(v.upper)?std::abs(v.upper):0.0);check.absoluteResidual=std::max(check.absoluteResidual,violation);check.scaledResidual=std::max(check.scaledResidual,violation/scale);}
 for(const auto& row:model.constraints){if(!row.active)continue;double activity=0,scale=1.0+std::abs(row.rhs);for(const auto& p:row.coefficients){if(p.first>=solution.primal.size()){check.feasible=false;check.scaledResidual=std::numeric_limits<double>::infinity();continue;}activity+=p.second*solution.primal[p.first];scale+=std::abs(p.second*solution.primal[p.first]);}double violation=row.relation==Relation::Equal?std::abs(activity-row.rhs):row.relation==Relation::LessEqual?std::max(0.0,activity-row.rhs):std::max(0.0,row.rhs-activity);check.absoluteResidual=std::max(check.absoluteResidual,violation);check.scaledResidual=std::max(check.scaledResidual,violation/scale);}
 check.feasible=check.feasible&&check.scaledResidual<=tolerance;return check;
}
static const char* buildCompiler(){
#if defined(_MSC_VER)
 return "MSVC";
#elif defined(__clang__)
 return "Clang " __clang_version__;
#elif defined(__GNUC__)
 return "GCC " __VERSION__;
#else
 return "unknown";
#endif
}
static const char* buildType(){
#ifdef NDEBUG
 return "optimized (NDEBUG)";
#else
 return "configuration unspecified";
#endif
}
int main(int argc,char** argv){
  const auto processStart=std::chrono::steady_clock::now();
 if(argc>=2&&std::string(argv[1])=="--build-info"){
#ifdef NDEBUG
  constexpr const char* mode="RELEASE";
#else
  constexpr const char* mode="DEBUG";
#endif
  std::cout<<"Build compiler: "<<buildCompiler()<<"\nBuild type: "<<buildType()<<"\nBuild mode: "<<mode<<"\n";
  return 0;
 }
 if(argc>=2&&std::string(argv[1])=="--device-info"){int device=0;for(int i=2;i+1<argc;++i)if(std::string(argv[i])=="--device")device=std::stoi(argv[i+1]);std::cout<<cuda::deviceInfoText(device);return 0;}
 if(argc>=2&&std::string(argv[1])=="--select-backend"){
  try{
   cuda::Backend requested=cuda::Backend::Auto;
   std::size_t rows=0,cols=0,nnz=0,repetitions=1;
   int device=0;
   std::string modelSize="UNKNOWN",algorithm="revised-simplex";
   for(int i=2;i<argc;++i){
    if(i+1<argc&&std::string(argv[i])=="--backend")requested=cuda::parseBackend(argv[i+1]);
    if(i+1<argc&&std::string(argv[i])=="--model-size")modelSize=argv[i+1];
    if(i+1<argc&&std::string(argv[i])=="--method")algorithm=argv[i+1];
    if(i+1<argc&&std::string(argv[i])=="--rows")rows=std::stoull(argv[i+1]);
    if(i+1<argc&&std::string(argv[i])=="--cols")cols=std::stoull(argv[i+1]);
    if(i+1<argc&&std::string(argv[i])=="--nnz")nnz=std::stoull(argv[i+1]);
    if(i+1<argc&&std::string(argv[i])=="--repetitions")repetitions=std::stoull(argv[i+1]);
    if(i+1<argc&&std::string(argv[i])=="--device")device=std::stoi(argv[i+1]);
   }
   constexpr bool measuredCudaSpeedup=false;
   auto selected=cuda::chooseBackend(requested,rows,cols,nnz,repetitions,modelSize,algorithm,measuredCudaSpeedup);
   cuda::Context gpu(device);
   const bool fallback=selected==cuda::Backend::CUDA&&!gpu.available()&&requested==cuda::Backend::Auto;
   if(fallback)selected=cuda::Backend::CPU;
   const bool milpCpuOnly=algorithm=="milp"||algorithm=="cutting-plane"||algorithm=="feasibility-pump";
   const char* reason=requested!=cuda::Backend::Auto?"An explicit backend override was requested.":milpCpuOnly?"MILP branch-and-bound and node LP relaxations run on CPU; CUDA is available but is not used for this solve.":modelSize=="SMALL"?"Small model — CPU execution selected to avoid GPU overhead.":modelSize=="MEDIUM"?"Medium model — CPU retained until CUDA benefit is measured for this method and model size.":algorithm!="ipm"&&algorithm!="qp"&&algorithm!="pdhg"?"The selected algorithm has no production CUDA execution path; CPU was selected.":fallback?"The CUDA build has no usable CUDA device; the C++ selector fell back to CPU.":selected==cuda::Backend::CUDA?"Large workload suitable for GPU acceleration based on the measured backend policy.":"Large CUDA-compatible model, but no verified same-method CUDA speedup is recorded; CPU is preferred for this prototype.";
   std::cout<<"Backend: "<<cuda::backendName(selected)<<"\nBackend reason: "<<reason<<"\nModel size: "<<modelSize<<"\nSelected algorithm: "<<algorithm<<"\nVariables: "<<cols<<"\nConstraints: "<<rows<<"\nNonzeros: "<<nnz<<"\n";
   return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 2;}
 }
 if(argc<3||std::string(argv[1])!="--input"){std::cerr<<"usage: --input <file> [--method revised-simplex|dual-simplex|ipm|pdhg|qp] [--max-iterations N (0 = unlimited)]\n";return 2;}
 try {
  std::size_t maxIterations=10000,maxNodes=10000;
  double milpTimeLimitMs=0.0;
  std::chrono::steady_clock::time_point inputDeadlineStorage;
  const std::chrono::steady_clock::time_point* inputDeadline=nullptr;
  double presolveTimeBudgetMs=0.0;
  cuda::Backend requestedBackend=cuda::Backend::CPU;
  int device=0;
  bool iterative=false,presolveEnabled=true,sparsePrimal=false,printPrimal=false,selectBackendOnly=false,milpWarmStart=false;
  std::string modelSize="UNKNOWN",selectedAlgorithm="revised-simplex";
  for(int i=3;i<argc;++i){
   if(i+1<argc&&std::string(argv[i])=="--max-iterations"){maxIterations=std::stoull(argv[i+1]);if(maxIterations==0)maxIterations=std::numeric_limits<std::size_t>::max();}
   if(i+1<argc&&std::string(argv[i])=="--max-nodes")maxNodes=std::stoull(argv[i+1]);
   if(i+1<argc&&std::string(argv[i])=="--time-limit-ms")milpTimeLimitMs=std::max(0.0,std::stod(argv[i+1]));
   if(i+1<argc&&std::string(argv[i])=="--presolve-time-ms")presolveTimeBudgetMs=std::stod(argv[i+1]);
   if(i+1<argc&&std::string(argv[i])=="--backend"){std::string b=argv[i+1];if(b!="cpu"&&b!="cuda"&&b!="auto")throw std::invalid_argument("--backend must be cpu, cuda, or auto");requestedBackend=cuda::parseBackend(b);}
   if(i+1<argc&&std::string(argv[i])=="--device")device=std::stoi(argv[i+1]);
   if(i+1<argc&&std::string(argv[i])=="--model-size")modelSize=argv[i+1];
   if(i+1<argc&&std::string(argv[i])=="--method")selectedAlgorithm=argv[i+1];
   if(std::string(argv[i])=="--select-backend")selectBackendOnly=true;
   if(i+1<argc&&((std::string(argv[i])=="--method"&&(std::string(argv[i+1])=="ipm"||std::string(argv[i+1])=="pdhg"||std::string(argv[i+1])=="qp"))||(std::string(argv[i])=="--lp-method"&&(std::string(argv[i+1])=="ipm"||std::string(argv[i+1])=="pdhg"))))iterative=true;
   if(std::string(argv[i])=="--no-presolve")presolveEnabled=false;
   if(std::string(argv[i])=="--sparse-primal")sparsePrimal=true;
   if(std::string(argv[i])=="--print-primal")printPrimal=true;
   if(i+1<argc&&std::string(argv[i])=="--warm-start"){const std::string setting=argv[i+1];if(setting!="0"&&setting!="1")throw std::invalid_argument("--warm-start must be 0 or 1");milpWarmStart=setting=="1";}
  }
  if(selectedAlgorithm=="milp"&&milpTimeLimitMs>0.0){
   inputDeadlineStorage=processStart+std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double,std::milli>(milpTimeLimitMs));
   inputDeadline=&inputDeadlineStorage;
  }
  auto parseStart=std::chrono::steady_clock::now();
  Model model;
  double parseMs=0.0;
  try {
   model=parseInput(argv[2],inputDeadline);
  } catch(const InputTimeLimitExceeded& e) {
   parseMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-parseStart).count();
   if(selectedAlgorithm!="milp")throw;
   std::cout<<std::setprecision(17)<<"Problem Type: MILP\nMethod: Branch-and-Bound\nMILP time limit ms: "<<milpTimeLimitMs
    <<"\nStatus: TIME_LIMIT_NO_INCUMBENT\nSolve time ms: 0\nMILP Model Preparation time ms: "<<parseMs
    <<"\nRoot LP time ms: 0\nRoot LP iterations: 0\nRoot LP method: N/A\nFeasibility Pump time ms: 0"
    <<"\nFeasibility Pump LP solves: 0\nFeasibility Pump iterations: 0\nBranch-and-Bound time ms: 0"
    <<"\nVerification time ms: N/A\nObjective: N/A\nPrimal Bound: N/A\nDual Bound: N/A\nAbsolute Gap: N/A\nRelative Gap: N/A"
    <<"\nNodes Created: 0\nNodes Processed: 0\nNodes Pruned: 0\nLP Solves: 0\nLP Iterations: 0\nVerification: N/A\nMessage: "<<e.what()<<"\n";
   return 0;
  }
  parseMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-parseStart).count();
  std::size_t nnz=0;for(const auto& c:model.constraints)nnz+=c.coefficients.size();
  // No checked-in record currently demonstrates a verified same-model
  // speedup for a production CUDA method. Keep AUTO on CPU until that evidence
  // exists; the experimental kernels and the lower-level size heuristic stay
  // available for internal testing.
  constexpr bool measuredCudaSpeedup=false;
  const auto policyBackend=cuda::chooseBackend(requestedBackend,model.constraints.size(),model.variables.size(),nnz,iterative?8:1,modelSize,selectedAlgorithm,measuredCudaSpeedup);
  cuda::Context gpu(device);
  auto selectedBackend=policyBackend;
  const bool cudaFallback=policyBackend==cuda::Backend::CUDA&&!gpu.available()&&requestedBackend==cuda::Backend::Auto;
  if(policyBackend==cuda::Backend::CUDA&&!gpu.available()){
   if(requestedBackend==cuda::Backend::Auto)selectedBackend=cuda::Backend::CPU;
   else throw std::runtime_error("CUDA backend requested but no usable CUDA device is available");
  }
  const bool milpCpuOnly=selectedAlgorithm=="milp"||selectedAlgorithm=="cutting-plane"||selectedAlgorithm=="feasibility-pump";
  const char* backendReason=requestedBackend!=cuda::Backend::Auto?"An explicit backend override was requested.":milpCpuOnly?"MILP branch-and-bound and node LP relaxations run on CPU; CUDA is available but is not used for this solve.":modelSize=="SMALL"?"Small model — CPU execution selected to avoid GPU overhead.":modelSize=="MEDIUM"?"Medium model — CPU retained until CUDA benefit is measured for this method and model size.":!iterative?"The selected algorithm has no production CUDA execution path; CPU was selected.":cudaFallback?"The CUDA build has no usable CUDA device; the C++ selector fell back to CPU.":policyBackend==cuda::Backend::CUDA?"Large workload suitable for GPU acceleration based on the measured backend policy.":"Large CUDA-compatible model, but no verified same-method CUDA speedup is recorded; CPU is preferred for this prototype.";
  if(selectBackendOnly){std::cout<<"Backend: "<<cuda::backendName(selectedBackend)<<"\nBackend reason: "<<backendReason<<"\nModel size: "<<modelSize<<"\nSelected algorithm: "<<selectedAlgorithm<<"\nVariables: "<<model.variables.size()<<"\nConstraints: "<<model.constraints.size()<<"\nNonzeros: "<<nnz<<"\n";return 0;}
  if(gpu.available()&&selectedBackend==cuda::Backend::CUDA)cuda::Context::setDefault(&gpu);
  auto cls=classify(model);
  auto presolveStart=std::chrono::steady_clock::now();
  auto red=presolveEnabled?Presolver({},10,cls.type==ProblemType::LP?presolveTimeBudgetMs:0.0,cls.type==ProblemType::LP).run(model):PresolveResult{model};
  double presolveMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-presolveStart).count();
  LPMethod method=LPMethod::RevisedSimplex;
  for(int i=3;i+1<argc;++i)if(std::string(argv[i])=="--method"){std::string x=argv[i+1];if(x=="dual-simplex")method=LPMethod::DualSimplex;else if(x=="ipm")method=LPMethod::IPM;else if(x=="pdhg")method=LPMethod::PDHG;}
  std::cout<<std::setprecision(17)<<"MODEL "<<model.name<<"\nBackend: "<<cuda::backendName(selectedBackend)<<"\nBackend reason: "<<backendReason<<"\nModel size: "<<modelSize<<"\nSelected algorithm: "<<selectedAlgorithm<<"\nResolved algorithm: "<<selectedAlgorithm<<"\nBuild compiler: "<<buildCompiler()<<"\nBuild type: "<<buildType()<<"\n";
#ifdef NDEBUG
  std::cout<<"Build mode: RELEASE\n";
#else
  std::cout<<"Build mode: DEBUG\n";
#endif
  std::cout<<"C++ standard: "<<__cplusplus<<"\nPresolve: "<<(presolveEnabled?"ON":"OFF")<<"\nPresolve time budget ms: "<<presolveTimeBudgetMs<<"\nPresolve termination: "<<red.terminationReason<<"\nParse time ms: "<<parseMs<<"\nPresolve time ms: "<<presolveMs<<"\nVariables: "<<model.variables.size()<<"\nConstraints: "<<model.constraints.size()<<"\nNonzeros: "<<nnz<<"\nPRESOLVE\nFinal variables: "<<activeVariableCount(red.model)<<"\nFinal constraints: "<<activeConstraintCount(red.model)<<"\nPresolve reductions: "<<red.stats.boundTightenings+red.stats.fixedVariables+red.stats.eliminatedVariables+red.stats.redundantRows+red.stats.aggregations+red.stats.substitutions+red.stats.singletonReductions<<"\n";
  if(cls.type==ProblemType::QP)std::cout<<"Hessian nonzeros: "<<model.quadratic.size()<<"\n";
  for(const auto& pass:red.passStats)std::cout<<"Presolve pass: "<<pass.pass<<" time_ms="<<pass.timeMs<<" variables="<<pass.variablesBefore<<"->"<<pass.variablesAfter<<" constraints="<<pass.constraintsBefore<<"->"<<pass.constraintsAfter<<" nnz="<<pass.nonzerosBefore<<"->"<<pass.nonzerosAfter<<" bound_tightenings="<<pass.boundTightenings<<" fixed_variables="<<pass.fixedVariables<<" substitutions="<<pass.substitutions<<" singleton_reductions="<<pass.singletonReductions<<" redundant_rows="<<pass.redundantRowsRemoved<<" reduction_percent="<<pass.percentageReduction<<"\n";
  std::cout<<std::flush;
  bool qp=false,relax=false;LPMethod lpMethod=LPMethod::RevisedSimplex;for(int i=3;i+1<argc;++i){if(std::string(argv[i])=="--method"&&std::string(argv[i+1])=="qp")qp=true;if(std::string(argv[i])=="--method"&&std::string(argv[i+1])=="lp-relaxation")relax=true;if(std::string(argv[i])=="--lp-method"){std::string z=argv[i+1];if(z=="dual-simplex")lpMethod=LPMethod::DualSimplex;else if(z=="ipm")lpMethod=LPMethod::IPM;else if(z=="pdhg")lpMethod=LPMethod::PDHG;}}
  bool milp=false,cutting=false,fp=false;for(int i=3;i+1<argc;++i){if(std::string(argv[i])=="--method"&&std::string(argv[i+1])=="milp")milp=true;if(std::string(argv[i])=="--method"&&std::string(argv[i+1])=="cutting-plane")cutting=true;if(std::string(argv[i])=="--method"&&std::string(argv[i+1])=="feasibility-pump")fp=true;}
  if(fp){std::size_t lim=100;double tol=1e-7;for(int i=3;i+1<argc;++i){if(std::string(argv[i])=="--max-fp-iterations")lim=std::stoul(argv[i+1]);if(std::string(argv[i])=="--fp-tolerance")tol=std::stod(argv[i+1]);}auto s=FeasibilityPump{tol,lim}.solve(model);std::cout<<"Problem Type: MILP\nMethod: Feasibility Pump\nStatus: "<<(s.status==FpStatus::Feasible?"FEASIBLE":s.status==FpStatus::NoFeasibleSolution?"NO_FEASIBLE_SOLUTION":s.status==FpStatus::LPFailure?"LP_FAILURE":"NUMERICAL_FAILURE")<<"\nIterations: "<<s.iterations<<"\nRounding Attempts: "<<s.roundingAttempts<<"\nProjections: "<<s.projections<<"\nPerturbations: "<<s.perturbations<<"\nObjective: "<<s.objective<<"\nInteger Feasible: "<<(s.status==FpStatus::Feasible?"YES":"NO")<<"\nVerification: "<<(s.verified?"PASS":"N/A")<<"\nPrimal:";for(double v:s.solution)std::cout<<" "<<v;std::cout<<"\n";printPrimalNames(model);return 0;}
  if(cutting){std::size_t cuts=100,its=100;for(int i=3;i+1<argc;++i){if(std::string(argv[i])=="--max-cuts")cuts=std::stoul(argv[i+1]);if(std::string(argv[i])=="--max-iterations")its=std::stoul(argv[i+1]);}auto s=CuttingPlaneSolver{1e-8,cuts,its}.solve(model);const char*status=s.status==CuttingStatus::OptimalInteger?"OPTIMAL_INTEGER":s.status==CuttingStatus::Infeasible?"INFEASIBLE":s.status==CuttingStatus::CutLimitReached?"CUT_LIMIT_REACHED":s.status==CuttingStatus::IterationLimitReached?"ITERATION_LIMIT_REACHED":s.status==CuttingStatus::LPSolveFailed?"LP_SOLVE_FAILED":"NUMERICAL_FAILURE";std::cout<<"Problem Type: MILP\nMethod: Cutting Plane\nCut Type: Gomory\nStatus: "<<status<<"\nIterations: "<<s.iterations<<"\nCuts Generated: "<<s.cutsGenerated<<"\nCuts Accepted: "<<s.cutsAccepted<<"\nCuts Rejected: "<<s.cutsRejected<<"\nLP Solves: "<<s.lpSolves<<"\nObjective: "<<s.objective<<"\nFractional Variables: "<<s.fractionalVariables<<"\nVerification: "<<(s.verified?"PASS":"FAIL")<<"\nPrimal:";for(double v:s.solution)std::cout<<" "<<v;std::cout<<"\n";printPrimalNames(model);return 0;}
  if (milp) {
      auto solveStart = std::chrono::steady_clock::now();
      double remainingMILPTimeMs = milpTimeLimitMs;
      if (milpTimeLimitMs > 0.0) {
          const double elapsedBeforeMILP = std::chrono::duration<double, std::milli>(solveStart - processStart).count();
          remainingMILPTimeMs = std::max(0.0, milpTimeLimitMs - elapsedBeforeMILP);
      }
      auto s = BranchAndBound{1e-8, maxNodes, maxIterations, remainingMILPTimeMs, milpWarmStart}.solve(model, lpMethod);
      if (auto* context = cuda::Context::defaultContext()) context->synchronize();
      double solveMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - solveStart).count();
      const bool hasCandidate = s.incumbentFound && !s.solution.empty();
      double verificationMs = 0.0;
      bool independentlyVerified = false;
      if (hasCandidate) {
          const auto verificationStart = std::chrono::steady_clock::now();
          Solution candidate;
          candidate.primal = s.solution;
          const auto check = checkPrimal(model, candidate);
          bool integral = true;
          for (const auto& variable : model.variables) {
              if ((variable.type == VariableType::Integer || variable.type == VariableType::Binary) &&
                  (variable.originalId >= s.solution.size() ||
                   std::abs(s.solution[variable.originalId] - std::round(s.solution[variable.originalId])) > 1e-7)) {
                  integral = false;
                  break;
              }
          }
          independentlyVerified = check.feasible && integral;
          verificationMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - verificationStart).count();
      }
      const char* status = s.status == MILPStatus::Optimal ? "OPTIMAL" :
          s.status == MILPStatus::Infeasible ? "INFEASIBLE" : s.status == MILPStatus::Unbounded ? "UNBOUNDED" :
          s.status == MILPStatus::TimeLimit ? "TIME_LIMIT" : s.status == MILPStatus::TimeLimitNoIncumbent ? "TIME_LIMIT_NO_INCUMBENT" :
          s.status == MILPStatus::NodeLimit ? "NODE_LIMIT" :
          s.status == MILPStatus::IterationLimit ? "ITERATION_LIMIT" : "NUMERICAL_FAILURE";
      std::cout << "Problem Type: MILP\nMethod: Branch-and-Bound\nNode limit: "
          << (maxNodes == 0 ? "unlimited" : std::to_string(maxNodes))
          << "\nMILP time limit ms: " << milpTimeLimitMs
          << "\nLP iteration limit per relaxation: "
          << (maxIterations == std::numeric_limits<std::size_t>::max() ? "unlimited" : std::to_string(maxIterations))
          << "\nStatus: " << status << "\nSolve time ms: " << solveMs
          << "\nMILP Model Preparation time ms: " << parseMs + presolveMs
          << "\nRoot LP time ms: " << s.rootLPTimeMs
          << "\nRoot LP method: " << s.rootLPMethod
          << "\nRoot LP iterations: " << s.rootLPIterations
          << "\nRoot LP standardization time ms: " << s.rootLPStandardizationTimeMs
          << "\nRoot LP sparse pricing time ms: " << s.rootLPSparsePricingTimeMs
          << "\nRoot LP sparse basis solve time ms: " << s.rootLPSparseBasisSolveTimeMs
          << "\nRoot LP sparse factorization time ms: " << s.rootLPSparseFactorizationTimeMs
          << "\nRoot LP sparse refactorizations: " << s.rootLPSparseRefactorizations
          << "\nRoot LP sparse pivots: " << s.rootLPSparsePivots
          << "\nRoot fractional integer variables: " << s.rootFractionalVariables
          << "\nFeasibility Pump time ms: " << s.feasibilityPumpTimeMs
          << "\nFeasibility Pump LP solves: " << s.feasibilityPumpLPSolves
          << "\nFeasibility Pump iterations: " << s.feasibilityPumpIterations
          << "\nBranch-and-Bound time ms: " << s.branchAndBoundTimeMs
          << "\nNode selection time ms: " << s.nodeSelectionTimeMs
          << "\nNode model/update time ms: " << s.nodeModelUpdateTimeMs
          << "\nNode LP time ms: " << s.nodeLPTimeMs
          << "\nNode LP iterations: " << s.nodeLPIterations
          << "\nNode LP standardization time ms: " << s.nodeLPStandardizationTimeMs
#if defined(SOVEREIGN_PROFILE_DENSE_SIMPLEX)
          << "\nNode LP dense setup/tableau time ms: " << s.nodeLPDenseSetupTimeMs
          << "\nNode LP dense pricing time ms: " << s.nodeLPDensePricingTimeMs
          << "\nNode LP dense ratio test time ms: " << s.nodeLPDenseRatioTestTimeMs
          << "\nNode LP dense pivot time ms: " << s.nodeLPDensePivotTimeMs
          << "\nNode LP dense warm basis rebuild time ms: " << s.nodeLPDenseWarmBasisRebuildTimeMs
          << "\nNode LP dense solution recovery time ms: " << s.nodeLPDenseSolutionRecoveryTimeMs
          << "\nNode LP dense verification time ms: " << s.nodeLPDenseVerificationTimeMs
          << "\nNode LP dense cut generation time ms: " << s.nodeLPDenseCutTimeMs
#endif
          << "\nNode LP sparse factorization time ms: " << s.nodeLPSparseFactorizationTimeMs
          << "\nBranching time ms: " << s.branchingTimeMs
          << "\nPruning time ms: " << s.pruningTimeMs
          << "\nIncumbent updates: " << s.incumbentUpdates
          << "\nRoot rounding heuristic attempts: " << s.rootRoundingHeuristicAttempts
          << "\nRoot rounding heuristic accepted: " << s.rootRoundingHeuristicAccepted
          << "\nFirst incumbent node: " << s.firstIncumbentNode
          << "\nFirst incumbent time ms: " << s.firstIncumbentTimeMs
          << "\nMaximum depth: " << s.maxDepth
          << "\nPeak open nodes: " << s.peakOpenNodes
          << "\nCuts generated: " << s.cutsGenerated
          << "\nCuts accepted: " << s.cutsAccepted
          << "\nCuts rejected: " << s.cutsRejected
          << "\nWarm starts attempted: " << s.warmStartsAttempted
          << "\nWarm starts successful: " << s.warmStartsSuccessful
          << "\nWarm starts failed: " << s.warmStartsFailed
          << "\nCold fallbacks: " << s.coldFallbacks
          << "\nCold starts: " << s.coldStarts
          << "\nVerification internal time ms: " << s.verificationTimeMs
          << "\nTotal MILP solver time ms: " << s.totalSolverTimeMs
          << "\nPostsolve time ms: N/A\nVerification time ms: " << (hasCandidate ? std::to_string(verificationMs) : "N/A")
          << "\nObjective: " << (hasCandidate ? std::to_string(s.objective) : "N/A")
          << "\nPrimal Bound: " << (hasCandidate ? std::to_string(s.primalBound) : "N/A")
          << "\nDual Bound: " << (s.hasDualBound ? std::to_string(s.dualBound) : "N/A")
          << "\nAbsolute Gap: " << (hasCandidate ? std::to_string(s.absoluteGap) : "N/A")
          << "\nRelative Gap: " << (hasCandidate ? std::to_string(s.relativeGap) : "N/A")
          << "\nNodes Created: " << s.nodesCreated << "\nNodes Processed: " << s.nodesProcessed
          << "\nNodes Pruned: " << s.nodesPruned
          << "\nNodes Pruned Before LP: " << s.nodesPrunedBeforeLP
          << "\nNode LP Solves Avoided by Bound: " << s.nodeLPSolvesAvoidedByBound
          << "\nLP Solves: " << s.lpSolves
          << "\nLP Iterations: " << s.totalLPIterations
          << "\nVerification: " << (s.verified && independentlyVerified ? "PASS" : hasCandidate ? "FAIL" : "N/A")
          << "\nMessage: " << s.message << "\n";
      if (sparsePrimal) {
          if (hasCandidate) {
              std::cout << "Primal Sparse:";
              for (std::size_t i = 0; i < s.solution.size(); ++i)
                  if (std::abs(s.solution[i]) > 1e-12) std::cout << " " << i << "=" << std::setprecision(17) << s.solution[i];
              std::cout << "\n";
          }
      } else {
          std::cout << "Primal:";
          if (hasCandidate) for (double v : s.solution) std::cout << " " << v;
          std::cout << "\n";
          if (hasCandidate) printPrimalNames(model);
      }
      return 0;
  }
  if(relax){auto s=solveLPRelaxation(model,lpMethod);std::cout<<"Problem Type: MILP\nMethod: LP Relaxation\nRelaxed Problem Type: LP\nLP Method: "<<s.method<<"\nStatus: "<<(s.status==LPStatus::Optimal?"OPTIMAL":s.status==LPStatus::Infeasible?"INFEASIBLE":s.status==LPStatus::Unbounded?"UNBOUNDED":"FAILED")<<"\nObjective: "<<s.objectiveValue<<"\nBound Type: "<<s.boundType<<"\nLP Bound: "<<s.bound<<"\nFractional Integer Variables: "<<s.fractionalIntegerVariables<<"\nFractional Solution: "<<(s.fractionalSolution?"true":"false")<<"\nIntegral Within Tolerance: "<<(s.integralWithinTolerance?"true":"false")<<"\nIterations: "<<s.iterations<<"\nVerification: "<<(s.status==LPStatus::Optimal&&s.primalResidual<=1e-7?"PASS":"FAIL")<<"\nPrimal:";for(double v:s.solution)std::cout<<" "<<v;std::cout<<"\n";printPrimalNames(model);return 0;}
  if(qp){auto q=checkQPConvexity(model);std::cout<<"Problem Type: QP\nConvexity: "<<(q.isConvex?"CONVEX":"NON-CONVEX")<<"\nHessian: "<<convexityName(q.classification)<<"\n";auto s=GeneralQPInteriorPoint{}.solve(model,maxIterations);std::cout<<"Method: QP Newton Barrier\nStatus: "<<(s.status==QPStatus::Optimal?"OPTIMAL":s.status==QPStatus::UnsupportedNonconvex?"UNSUPPORTED_NONCONVEX":s.status==QPStatus::IterationLimit?"ITERATION_LIMIT":s.status==QPStatus::Unsupported?"UNSUPPORTED":"NUMERICAL_FAILURE")<<"\nIteration limit: "<<(maxIterations==std::numeric_limits<std::size_t>::max()?"unlimited":std::to_string(maxIterations))<<"\nIterations: "<<s.iterations<<"\nLinear system solves: "<<s.linearSystemSolves<<"\nPCG iterations: "<<s.linearSolverIterations<<"\nConvexity check time ms: "<<s.convexityCheckTimeMs<<"\nTransformation time ms: "<<s.transformationTimeMs<<"\nScaling time ms: "<<s.scalingTimeMs<<"\nInitialization time ms: "<<s.initializationTimeMs<<"\nResidual computation time ms: "<<s.residualComputationTimeMs<<"\nNewton assembly time ms: "<<s.newtonAssemblyTimeMs<<"\nLinear system build time ms: "<<s.linearSystemBuildTimeMs<<"\nLinear solve time ms: "<<s.linearSolveTimeMs<<"\nNewton update time ms: "<<s.updateTimeMs<<"\n";if(s.solverInvoked)std::cout<<"Solve time ms: "<<s.solverTimeMs<<"\n";std::cout<<"Postsolve time ms: "<<(s.solverInvoked?std::to_string(s.postsolveTimeMs):"N/A")<<"\nVerification time ms: "<<(s.solverInvoked?std::to_string(s.verificationTimeMs):"N/A")<<"\nObjective: "<<s.objectiveValue<<"\nPrimal residual: "<<s.primalResidual<<"\nDual residual: "<<s.dualResidual<<"\nComplementarity: "<<s.complementarity<<"\nVerification: "<<(s.kktVerified?"PASS":"FAIL")<<"\nMessage: "<<s.message<<"\n";if(printPrimal||s.x.size()<=100){std::cout<<"Primal:";for(double v:s.x)std::cout<<" "<<v;std::cout<<"\n";printPrimalNames(model);}else std::cout<<"Primal: omitted ("<<s.x.size()<<" values; pass --print-primal to display)\n";return 0;}
  if(cls.type!=ProblemType::LP){std::cout<<"SOLVER Status: UNSUPPORTED\n";return 0;}if(red.status==PresolveStatus::Infeasible){std::cout<<"SOLVER Status: INFEASIBLE\n";return 0;}if(red.status==PresolveStatus::Unbounded){std::cout<<"SOLVER Status: UNBOUNDED\n";return 0;}
  auto solveStart=std::chrono::steady_clock::now();
  LPResult solved=method==LPMethod::DualSimplex?DualSimplex{}.solve(red.model,maxIterations):method==LPMethod::IPM?MehrotraIPM{}.solve(red.model,maxIterations):method==LPMethod::PDHG?PDHGSolver{}.solve(red.model,maxIterations):LPSolver{}.solve(red.model,method,maxIterations);
  if(auto* context=cuda::Context::defaultContext())context->synchronize();
  double solveMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-solveStart).count();
  double standardizationMs=solved.standardizationTimeMs;
  auto postsolveStart=std::chrono::steady_clock::now(); Solution sol=postsolve(model,red,solved.solution);
  double postsolveMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-postsolveStart).count();
  auto verificationStart=std::chrono::steady_clock::now(); PrimalCheck check=checkPrimal(model,sol);
  bool verified=solved.status==LPStatus::Optimal&&check.feasible; double vr=check.absoluteResidual;
  double verificationMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-verificationStart).count();
  const bool hasCandidate=solved.status==LPStatus::Optimal;
  const double reportedObjective=hasCandidate?evaluateObjective(model,sol.primal):solved.objectiveValue;
  const char* status=solved.status==LPStatus::Unsupported?"UNSUPPORTED":solved.status==LPStatus::Infeasible?"INFEASIBLE":solved.status==LPStatus::Unbounded?"UNBOUNDED":hasCandidate&&!verified?"NUMERICAL_FAILURE":hasCandidate?"OPTIMAL":solved.status==LPStatus::IterationLimit?"ITERATION_LIMIT":"NUMERICAL_FAILURE";
  if(method==LPMethod::PDHG)std::cout<<"PDHG primal weight: "<<solved.pdhgPrimalWeight<<"\n";
  std::cout<<"METHOD: "<<(method==LPMethod::DualSimplex?"DUAL SIMPLEX":method==LPMethod::IPM?"MEHROTRA IPM":method==LPMethod::PDHG?"PDHG": "REVISED SIMPLEX")<<"\nIteration limit: "<<(maxIterations==std::numeric_limits<std::size_t>::max()?"unlimited":std::to_string(maxIterations))<<"\nStatus: "<<status<<"\nIterations: "<<solved.iterations<<"\nAttempt count: 1\nEstimated dense memory bytes: "<<solved.estimatedDenseMemoryBytes<<"\nDense memory budget bytes: "<<DenseLPMemoryBudgetBytes<<"\nDense memory guard: "<<(solved.denseMemoryGuardTriggered?"TRIGGERED":"NOT TRIGGERED")<<"\nSparse pricing time ms: "<<solved.sparsePricingTimeMs<<"\nSparse basis solve time ms: "<<solved.sparseBasisSolveTimeMs<<"\nSparse Devex time ms: "<<solved.sparseDevexTimeMs<<"\nSparse factorization time ms: "<<solved.sparseFactorizationTimeMs<<"\nSparse ratio test time ms: "<<solved.sparseRatioTestTimeMs<<"\nSparse lexicographic time ms: "<<solved.sparseLexicographicTimeMs<<"\nSparse refactorizations: "<<solved.sparseRefactorizations<<"\nSparse pivots: "<<solved.sparsePivots<<"\nSparse lexicographic solves: "<<solved.sparseLexicographicSolves<<"\nSparse Bland fallback: "<<(solved.sparseBlandFallbackTriggered?"YES":"NO")<<"\nSolve pipeline time ms: "<<solveMs<<"\nStandardization time ms: "<<standardizationMs<<"\nSolve time ms: "<<std::max(0.0,solveMs-standardizationMs)<<"\nStandardized rows: "<<solved.standardizedRows<<"\nStandardized columns: "<<solved.standardizedColumns<<"\nStandardized nonzeros: "<<solved.standardizedNonzeros<<"\nPostsolve time ms: "<<postsolveMs<<"\nVerification time ms: "<<verificationMs<<"\nPresolve fallback: NO\nFallback reason: C++ runs one solver attempt per process; automatic retry is disabled.\nObjective: "<<reportedObjective<<"\nFeasibility: "<<vr<<"\nPrimal residual: "<<optionalMetric(solved.primalResidual,method==LPMethod::PDHG)<<"\nDual residual: "<<optionalMetric(solved.dualResidual,method==LPMethod::PDHG)<<"\nComplementarity residual: "<<optionalMetric(solved.complementarityResidual,method==LPMethod::PDHG)<<"\nVerification: "<<(hasCandidate?(verified?"PASS":"FAIL"):"N/A")<<"\nPrimal:";
  if(hasCandidate)for(double v:sol.primal)std::cout<<" "<<v; std::cout<<"\n"; printPrimalNames(model); if(!solved.message.empty())std::cout<<"Message: "<<solved.message<<"\n";
  if(hasCandidate&&!verified){std::cout<<"PRIMAL";for(double x:sol.primal)std::cout<<" "<<x;std::cout<<"\nREDUCED";for(double x:solved.solution.primal)std::cout<<" "<<x;std::cout<<"\n";for(const auto& c:model.constraints){double a=0,scale=1+std::abs(c.rhs);for(auto p:c.coefficients){a+=p.second*sol.primal[p.first];scale+=std::abs(p.second*sol.primal[p.first]);}double e=c.relation==Relation::Equal?std::abs(a-c.rhs):c.relation==Relation::LessEqual?std::max(0.,a-c.rhs):std::max(0.,c.rhs-a);if(e/scale>1e-7)std::cout<<"VIOLATION "<<c.name<<" "<<a<<" "<<c.rhs<<"\n";}}
 }catch(const std::exception& e){std::cerr<<"error: "<<e.what()<<"\n";return 1;}return 0;}
