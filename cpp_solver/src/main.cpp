#include "presolve/Presolver.hpp"
#include "model/Input.hpp"
#include "model/Classification.hpp"
#include "lp/LPSolver.hpp"
#include "lp/DualSimplex.hpp"
#include "lp/MehrotraIPM.hpp"
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
using namespace sovereign;
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
 if(argc>=2&&std::string(argv[1])=="--device-info"){int device=0;for(int i=2;i+1<argc;++i)if(std::string(argv[i])=="--device")device=std::stoi(argv[i+1]);std::cout<<cuda::deviceInfoText(device);return 0;}
 if(argc<3||std::string(argv[1])!="--input"){std::cerr<<"usage: --input <file> [--method revised-simplex|dual-simplex|ipm|qp] [--max-iterations N (0 = unlimited)]\n";return 2;}
 try{std::size_t maxIterations=10000,maxNodes=10000;cuda::Backend requestedBackend=cuda::Backend::CPU;int device=0;bool iterative=false,presolveEnabled=true,sparsePrimal=false;for(int i=3;i<argc;++i){if(i+1<argc&&std::string(argv[i])=="--max-iterations"){maxIterations=std::stoull(argv[i+1]);if(maxIterations==0)maxIterations=std::numeric_limits<std::size_t>::max();}if(i+1<argc&&std::string(argv[i])=="--max-nodes")maxNodes=std::stoull(argv[i+1]);if(i+1<argc&&std::string(argv[i])=="--backend"){std::string b=argv[i+1];if(b!="cpu"&&b!="cuda"&&b!="auto")throw std::invalid_argument("--backend must be cpu, cuda, or auto");requestedBackend=cuda::parseBackend(b);}if(i+1<argc&&std::string(argv[i])=="--device")device=std::stoi(argv[i+1]);if(i+1<argc&&((std::string(argv[i])=="--method"&&(std::string(argv[i+1])=="ipm"||std::string(argv[i+1])=="qp"))||(std::string(argv[i])=="--lp-method"&&std::string(argv[i+1])=="ipm")))iterative=true;if(std::string(argv[i])=="--no-presolve")presolveEnabled=false;if(std::string(argv[i])=="--sparse-primal")sparsePrimal=true;}auto parseStart=std::chrono::steady_clock::now();Model model=parseInput(argv[2]);double parseMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-parseStart).count();std::size_t nnz=0;for(const auto& c:model.constraints)nnz+=c.coefficients.size();auto selectedBackend=cuda::chooseBackend(requestedBackend,model.constraints.size(),model.variables.size(),nnz,iterative?8:1);cuda::Context gpu(device);if(selectedBackend==cuda::Backend::CUDA&&!gpu.available())throw std::runtime_error("CUDA backend requested but no usable CUDA device is available");if(gpu.available()&&selectedBackend==cuda::Backend::CUDA)cuda::Context::setDefault(&gpu);auto cls=classify(model);auto presolveStart=std::chrono::steady_clock::now();auto red=presolveEnabled?Presolver{}.run(model):PresolveResult{model};double presolveMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-presolveStart).count();LPMethod method=LPMethod::RevisedSimplex;
  for(int i=3;i+1<argc;++i)if(std::string(argv[i])=="--method"){std::string x=argv[i+1];if(x=="dual-simplex")method=LPMethod::DualSimplex;else if(x=="ipm")method=LPMethod::IPM;}
  std::cout<<std::setprecision(17)<<"MODEL "<<model.name<<"\nBackend: "<<cuda::backendName(selectedBackend)<<"\nBuild compiler: "<<buildCompiler()<<"\nBuild type: "<<buildType()<<"\nC++ standard: "<<__cplusplus<<"\nPresolve: "<<(presolveEnabled?"ON":"OFF")<<"\nParse time ms: "<<parseMs<<"\nPresolve time ms: "<<presolveMs<<"\nVariables: "<<model.variables.size()<<"\nConstraints: "<<model.constraints.size()<<"\nPRESOLVE\nFinal variables: "<<red.model.variables.size()<<"\nFinal constraints: "<<red.model.constraints.size()<<"\nPresolve reductions: "<<red.stats.boundTightenings+red.stats.fixedVariables+red.stats.eliminatedVariables+red.stats.redundantRows+red.stats.aggregations+red.stats.substitutions+red.stats.singletonReductions<<"\n";
  std::cout<<std::flush;
  bool qp=false,relax=false;LPMethod lpMethod=LPMethod::RevisedSimplex;for(int i=3;i+1<argc;++i){if(std::string(argv[i])=="--method"&&std::string(argv[i+1])=="qp")qp=true;if(std::string(argv[i])=="--method"&&std::string(argv[i+1])=="lp-relaxation")relax=true;if(std::string(argv[i])=="--lp-method"){std::string z=argv[i+1];if(z=="dual-simplex")lpMethod=LPMethod::DualSimplex;else if(z=="ipm")lpMethod=LPMethod::IPM;}}
  bool milp=false,cutting=false,fp=false;for(int i=3;i+1<argc;++i){if(std::string(argv[i])=="--method"&&std::string(argv[i+1])=="milp")milp=true;if(std::string(argv[i])=="--method"&&std::string(argv[i+1])=="cutting-plane")cutting=true;if(std::string(argv[i])=="--method"&&std::string(argv[i+1])=="feasibility-pump")fp=true;}
  if(fp){std::size_t lim=100;double tol=1e-7;for(int i=3;i+1<argc;++i){if(std::string(argv[i])=="--max-fp-iterations")lim=std::stoul(argv[i+1]);if(std::string(argv[i])=="--fp-tolerance")tol=std::stod(argv[i+1]);}auto s=FeasibilityPump{tol,lim}.solve(model);std::cout<<"Problem Type: MILP\nMethod: Feasibility Pump\nStatus: "<<(s.status==FpStatus::Feasible?"FEASIBLE":s.status==FpStatus::NoFeasibleSolution?"NO_FEASIBLE_SOLUTION":s.status==FpStatus::LPFailure?"LP_FAILURE":"NUMERICAL_FAILURE")<<"\nIterations: "<<s.iterations<<"\nRounding Attempts: "<<s.roundingAttempts<<"\nProjections: "<<s.projections<<"\nPerturbations: "<<s.perturbations<<"\nObjective: "<<s.objective<<"\nInteger Feasible: "<<(s.status==FpStatus::Feasible?"YES":"NO")<<"\nVerification: "<<(s.verified?"PASS":"N/A")<<"\nPrimal:";for(double v:s.solution)std::cout<<" "<<v;std::cout<<"\n";printPrimalNames(model);return 0;}
  if(cutting){std::size_t cuts=100,its=100;for(int i=3;i+1<argc;++i){if(std::string(argv[i])=="--max-cuts")cuts=std::stoul(argv[i+1]);if(std::string(argv[i])=="--max-iterations")its=std::stoul(argv[i+1]);}auto s=CuttingPlaneSolver{1e-8,cuts,its}.solve(model);const char*status=s.status==CuttingStatus::OptimalInteger?"OPTIMAL_INTEGER":s.status==CuttingStatus::Infeasible?"INFEASIBLE":s.status==CuttingStatus::CutLimitReached?"CUT_LIMIT_REACHED":s.status==CuttingStatus::IterationLimitReached?"ITERATION_LIMIT_REACHED":s.status==CuttingStatus::LPSolveFailed?"LP_SOLVE_FAILED":"NUMERICAL_FAILURE";std::cout<<"Problem Type: MILP\nMethod: Cutting Plane\nCut Type: Gomory\nStatus: "<<status<<"\nIterations: "<<s.iterations<<"\nCuts Generated: "<<s.cutsGenerated<<"\nCuts Accepted: "<<s.cutsAccepted<<"\nCuts Rejected: "<<s.cutsRejected<<"\nLP Solves: "<<s.lpSolves<<"\nObjective: "<<s.objective<<"\nFractional Variables: "<<s.fractionalVariables<<"\nVerification: "<<(s.verified?"PASS":"FAIL")<<"\nPrimal:";for(double v:s.solution)std::cout<<" "<<v;std::cout<<"\n";printPrimalNames(model);return 0;}
  if (milp) {
      auto solveStart = std::chrono::steady_clock::now();
      auto s = BranchAndBound{1e-8, maxNodes, maxIterations}.solve(model, lpMethod);
      double solveMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - solveStart).count();
      const bool hasCandidate = s.incumbentFound && !s.solution.empty();
      const char* status = s.status == MILPStatus::Optimal ? "OPTIMAL" :
          s.status == MILPStatus::Infeasible ? "INFEASIBLE" : s.status == MILPStatus::Unbounded ? "UNBOUNDED" :
          s.status == MILPStatus::NodeLimit ? "NODE_LIMIT" :
          s.status == MILPStatus::IterationLimit ? "ITERATION_LIMIT" : "NUMERICAL_FAILURE";
      std::cout << "Problem Type: MILP\nMethod: Branch-and-Bound\nNode limit: "
          << (maxNodes == 0 ? "unlimited" : std::to_string(maxNodes))
          << "\nLP iteration limit per relaxation: "
          << (maxIterations == std::numeric_limits<std::size_t>::max() ? "unlimited" : std::to_string(maxIterations))
          << "\nStatus: " << status << "\nSolve time ms: " << solveMs
          << "\nObjective: " << (hasCandidate ? std::to_string(s.objective) : "N/A")
          << "\nPrimal Bound: " << (hasCandidate ? std::to_string(s.primalBound) : "N/A")
          << "\nDual Bound: " << (s.nodesCreated ? std::to_string(s.dualBound) : "N/A")
          << "\nAbsolute Gap: " << (hasCandidate ? std::to_string(s.absoluteGap) : "N/A")
          << "\nRelative Gap: " << (hasCandidate ? std::to_string(s.relativeGap) : "N/A")
          << "\nNodes Created: " << s.nodesCreated << "\nNodes Processed: " << s.nodesProcessed
          << "\nNodes Pruned: " << s.nodesPruned << "\nLP Solves: " << s.lpSolves
          << "\nLP Iterations: " << s.totalLPIterations
          << "\nVerification: " << (s.verified ? "PASS" : hasCandidate ? "FAIL" : "N/A")
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
  if(qp){auto q=checkQPConvexity(model);std::cout<<"Problem Type: QP\nConvexity: "<<(q.isConvex?"CONVEX":"NON-CONVEX")<<"\nHessian: "<<convexityName(q.classification)<<"\n";auto s=GeneralQPInteriorPoint{}.solve(model,maxIterations);std::cout<<"Method: QP Newton Barrier\nStatus: "<<(s.status==QPStatus::Optimal?"OPTIMAL":s.status==QPStatus::UnsupportedNonconvex?"UNSUPPORTED_NONCONVEX":s.status==QPStatus::IterationLimit?"ITERATION_LIMIT":s.status==QPStatus::Unsupported?"UNSUPPORTED":"NUMERICAL_FAILURE")<<"\nIteration limit: "<<(maxIterations==std::numeric_limits<std::size_t>::max()?"unlimited":std::to_string(maxIterations))<<"\nIterations: "<<s.iterations<<"\nObjective: "<<s.objectiveValue<<"\nPrimal residual: "<<s.primalResidual<<"\nDual residual: "<<s.dualResidual<<"\nComplementarity: "<<s.complementarity<<"\nVerification: "<<(s.status==QPStatus::Optimal&&s.primalResidual<=1e-7?"PASS":"FAIL")<<"\nMessage: "<<s.message<<"\nPrimal:";for(double v:s.x)std::cout<<" "<<v;std::cout<<"\n";printPrimalNames(model);return 0;}
  if(cls.type!=ProblemType::LP){std::cout<<"SOLVER Status: UNSUPPORTED\n";return 0;}if(red.status==PresolveStatus::Infeasible){std::cout<<"SOLVER Status: INFEASIBLE\n";return 0;}if(red.status==PresolveStatus::Unbounded){std::cout<<"SOLVER Status: UNBOUNDED\n";return 0;}
  auto solveStart=std::chrono::steady_clock::now();
  LPResult solved=method==LPMethod::DualSimplex?DualSimplex{}.solve(red.model):method==LPMethod::IPM?MehrotraIPM{}.solve(red.model):LPSolver{}.solve(red.model,method,maxIterations);
  double solveMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-solveStart).count();
  auto postsolveStart=std::chrono::steady_clock::now(); Solution sol=postsolve(model,red,solved.solution);
  double postsolveMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-postsolveStart).count();
  auto verificationStart=std::chrono::steady_clock::now(); PrimalCheck check=checkPrimal(model,sol);
  bool verified=solved.status==LPStatus::Optimal&&check.feasible; double vr=check.absoluteResidual;
  double verificationMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-verificationStart).count();
  bool presolveFallbackUsed=false;
  if(!verified&&solved.status==LPStatus::Optimal&&presolveEnabled){
   auto retrySolveStart=std::chrono::steady_clock::now(); PresolveResult originalModel{model};
   LPResult retry=method==LPMethod::DualSimplex?DualSimplex{}.solve(model):method==LPMethod::IPM?MehrotraIPM{}.solve(model):LPSolver{}.solve(model,method,maxIterations);
   solveMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-retrySolveStart).count();
   auto retryPostsolveStart=std::chrono::steady_clock::now(); Solution retrySolution=postsolve(model,originalModel,retry.solution);
   postsolveMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-retryPostsolveStart).count();
   auto retryVerifyStart=std::chrono::steady_clock::now(); PrimalCheck retryCheck=checkPrimal(model,retrySolution);
   verificationMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-retryVerifyStart).count();
   if(retry.status==LPStatus::Optimal&&retryCheck.feasible){solved=retry;sol=retrySolution;check=retryCheck;vr=check.absoluteResidual;verified=true;presolveFallbackUsed=true;}
  }
  const bool hasCandidate=solved.status==LPStatus::Optimal;
  const double reportedObjective=hasCandidate?evaluateObjective(model,sol.primal):solved.objectiveValue;
  const char* status=solved.status==LPStatus::Infeasible?"INFEASIBLE":solved.status==LPStatus::Unbounded?"UNBOUNDED":hasCandidate&&!verified?"NUMERICAL_FAILURE":hasCandidate?"OPTIMAL":solved.status==LPStatus::IterationLimit?"ITERATION_LIMIT":"NUMERICAL_FAILURE";
  std::cout<<"METHOD: "<<(method==LPMethod::DualSimplex?"DUAL SIMPLEX":method==LPMethod::IPM?"MEHROTRA IPM":"REVISED SIMPLEX")<<"\nIteration limit: "<<(maxIterations==std::numeric_limits<std::size_t>::max()?"unlimited":std::to_string(maxIterations))<<"\nStatus: "<<status<<"\nIterations: "<<solved.iterations<<"\nSolve time ms: "<<solveMs<<"\nPostsolve time ms: "<<postsolveMs<<"\nVerification time ms: "<<verificationMs<<"\nPresolve fallback: "<<(presolveFallbackUsed?"YES":"NO")<<"\nObjective: "<<reportedObjective<<"\nFeasibility: "<<vr<<"\nVerification: "<<(hasCandidate?(verified?"PASS":"FAIL"):"N/A")<<"\nPrimal:";
  if(hasCandidate)for(double v:sol.primal)std::cout<<" "<<v; std::cout<<"\n"; printPrimalNames(model); if(!solved.message.empty())std::cout<<"Message: "<<solved.message<<"\n";
  if(hasCandidate&&!verified){std::cout<<"PRIMAL";for(double x:sol.primal)std::cout<<" "<<x;std::cout<<"\nREDUCED";for(double x:solved.solution.primal)std::cout<<" "<<x;std::cout<<"\n";for(const auto& c:model.constraints){double a=0,scale=1+std::abs(c.rhs);for(auto p:c.coefficients){a+=p.second*sol.primal[p.first];scale+=std::abs(p.second*sol.primal[p.first]);}double e=c.relation==Relation::Equal?std::abs(a-c.rhs):c.relation==Relation::LessEqual?std::max(0.,a-c.rhs):std::max(0.,c.rhs-a);if(e/scale>1e-7)std::cout<<"VIOLATION "<<c.name<<" "<<a<<" "<<c.rhs<<"\n";}}
 }catch(const std::exception& e){std::cerr<<"error: "<<e.what()<<"\n";return 1;}return 0;}
