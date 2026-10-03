import os
import sys
import gzip
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from sovereign_solver.parser import parse_problem_file
from sovereign_solver.classification import classify_model
from sovereign_solver.presolve import presolve

def test_datasets():
    datasets = {
        "MIPLIB": ROOT / "benchmarks" / "miplib" / "small",
        "Netlib": ROOT / "benchmarks" / "netlib" / "small",
        "QPLIB": ROOT / "benchmarks" / "qplib",
        "Mittelmann": ROOT / "benchmarks" / "mittelmann" / "lp",
    }
    
    summary = []
    print("=" * 70)
    print(f"{'Dataset':<12} | {'File':<24} | {'Class':<6} | {'Vars':<6} | {'Rows':<6} | {'Presolve'}")
    print("=" * 70)
    
    for name, folder in datasets.items():
        if not folder.exists():
            continue
        files = sorted(list(folder.glob("**/*.mps")) + list(folder.glob("**/*.mps.gz")) + list(folder.glob("**/*.json")))
        for f in files:
            rel = f.name
            try:
                model = parse_problem_file(str(f))
                cls = classify_model(model).problem_type
                nvar = len(model.variables)
                nrow = len(model.constraints)
                
                # Test presolve on LPs
                presolve_status = "N/A"
                if cls == "LP":
                    try:
                        presolve_result = presolve(model)
                        p_model = presolve_result.model
                        presolve_status = f"{presolve_result.status}: {len(p_model.variables)}v/{len(p_model.constraints)}c"
                    except Exception as pe:
                        presolve_status = f"err: {pe}"
                
                print(f"{name:<12} | {rel:<24} | {cls:<6} | {nvar:<6} | {nrow:<6} | {presolve_status}")
                summary.append({"dataset": name, "file": rel, "class": cls, "vars": nvar, "rows": nrow, "status": "OK"})
            except Exception as e:
                print(f"{name:<12} | {rel:<24} | {'ERROR':<6} | {'-':<6} | {'-':<6} | {e}")
                summary.append({"dataset": name, "file": rel, "class": "ERROR", "error": str(e), "status": "FAIL"})
                
    print("=" * 70)
    print(f"Total instances tested: {len(summary)}")
    ok_count = sum(1 for s in summary if s["status"] == "OK")
    print(f"Successfully parsed & classified: {ok_count}/{len(summary)}")

if __name__ == "__main__":
    test_datasets()
