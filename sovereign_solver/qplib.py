"""Parser for QPLIB (Quadratic Programming Library) format instances."""
from __future__ import annotations
import math
from typing import Any, Dict

def parse_qplib(text: str) -> Dict[str, Any]:
    """Parse standard QPLIB format text into Sovereign problem dictionary."""
    lines = [line.strip() for line in text.splitlines() if line.strip() and not line.strip().startswith("#")]
    if not lines:
        raise ValueError("QPLIB file is empty.")
    
    tokens = []
    for line in lines:
        tokens.extend(line.split())
    
    if len(tokens) < 5:
        raise ValueError("Invalid QPLIB format: insufficient header tokens.")
        
    idx = 0
    name = tokens[idx]; idx += 1
    prob_type = tokens[idx]; idx += 1
    sense_token = tokens[idx].lower(); idx += 1
    sense = "minimize" if "min" in sense_token else "maximize"
    
    try:
        n_vars = int(tokens[idx]); idx += 1
    except ValueError:
        raise ValueError(f"Invalid variable count in QPLIB header: {tokens[idx-1]}")
        
    # Variables definition
    variables = []
    for i in range(n_vars):
        v_name = f"x{i+1}"
        v_type = "continuous"
        variables.append({"name": v_name, "type": v_type})
        
    objective = {}
    quadratic_terms = {}
    constraints = []
    bounds = {}
    
    # Read objective constant if present
    if idx < len(tokens):
        try:
            obj_const = float(tokens[idx]); idx += 1
        except ValueError:
            obj_const = 0.0
            
    # Read non-zero count of linear objective terms
    if idx < len(tokens):
        try:
            n_linear = int(tokens[idx]); idx += 1
            for _ in range(n_linear):
                if idx + 1 < len(tokens):
                    v_idx = int(tokens[idx]) - 1; idx += 1
                    coeff = float(tokens[idx]); idx += 1
                    if 0 <= v_idx < n_vars:
                        objective[f"x{v_idx+1}"] = coeff
        except (ValueError, IndexError):
            pass

    # Read non-zero count of quadratic objective terms
    if idx < len(tokens):
        try:
            n_quad = int(tokens[idx]); idx += 1
            for _ in range(n_quad):
                if idx + 2 < len(tokens):
                    r_idx = int(tokens[idx]) - 1; idx += 1
                    c_idx = int(tokens[idx]) - 1; idx += 1
                    q_val = float(tokens[idx]); idx += 1
                    if r_idx == c_idx and 0 <= r_idx < n_vars:
                        quadratic_terms[f"x{r_idx+1}"] = q_val
        except (ValueError, IndexError):
            pass
            
    return {
        "name": name,
        "objective_sense": sense,
        "variables": variables,
        "objective": objective,
        "quadratic_terms": quadratic_terms,
        "constraints": constraints,
        "bounds": bounds
    }
