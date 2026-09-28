"""File input layer. Detection is based on file content, never its filename."""
import json
import gzip
import re
from pathlib import Path
from typing import Dict
from .validation import validate_payload
from .mps import parse_mps


def parse_problem_file(path: str):
    try:
        source = Path(path)
        opener = gzip.open if source.suffix.lower() == ".gz" else open
        with opener(source, "rt", encoding="utf-8") as stream:
            text = stream.read()
    except (OSError, EOFError, UnicodeError) as exc:
        raise ValueError(f"Unable to read input file '{path}': {exc}") from exc
    if not text.strip():
        raise ValueError("Input file is empty.")
    first = next((line.strip().upper() for line in text.splitlines() if line.strip() and not line.lstrip().startswith("*")), "")
    if first.startswith("NAME") and (first == "NAME" or first.startswith("NAME ")):
        significant = [line.strip() for line in text.splitlines() if line.strip() and not line.lstrip().startswith("*")]
        if _looks_like_netlib_emps(significant):
            raise ValueError(
                "Netlib EMPS-compressed MPS is unsupported: the current decoder cannot "
                "verify coefficient fidelity. Use a standard MPS copy; no conversion was attempted."
            )
        return parse_mps(text)
    try:
        payload = json.loads(text)
    except json.JSONDecodeError:
        payload = _parse_text(text)
    if not isinstance(payload, dict):
        raise ValueError("The input file must contain one optimization problem object.")
    return validate_payload(payload)


def _looks_like_netlib_emps(lines: list[str]) -> bool:
    """Identify Netlib's statistics-prefixed compressed MPS, not standard MPS."""
    if len(lines) < 4 or not lines[0].upper().startswith("NAME"):
        return False
    first_stats = lines[1].split()
    second_stats = lines[2].split()
    return (len(first_stats) == 8 and len(second_stats) == 3
            and all(token.isdecimal() for token in first_stats + second_stats)
            and not any(token.upper() in {"ROWS", "COLUMNS", "RHS", "BOUNDS"} for token in lines[1:4]))


def _parse_text(text: str) -> Dict:
    """Parse the small, extension-independent text format documented in README."""
    lines = [line.strip() for line in text.splitlines() if line.strip() and not line.strip().startswith("#")]
    data = {"variables": [], "objective": {}, "constraints": [], "quadratic_terms": {}, "bounds": {}}
    for line in lines:
        lower = line.lower()
        if lower.startswith("name:"):
            data["name"] = line.split(":", 1)[1].strip()
        elif lower.startswith("objective:"):
            _, expression = line.split(":", 1)
            objective_match = re.match(r"(maximize|minimize)\s+(.+)$", expression.strip(), re.I)
            if not objective_match:
                raise ValueError("Objective must start with 'maximize' or 'minimize'.")
            data["objective_sense"] = objective_match.group(1).lower()
            data["objective"], data["quadratic_terms"] = _parse_expression(objective_match.group(2))
        elif lower.startswith("var ") or lower.startswith("variable "):
            match = re.match(r"(?:var|variable)\s+([A-Za-z_]\w*)\s*(?::|\s)\s*(continuous|integer|binary)", line, re.I)
            if not match:
                raise ValueError(f"Invalid variable declaration: {line}")
            data["variables"].append({"name": match.group(1), "type": match.group(2).lower()})
        elif lower.startswith("constraint:") or lower.startswith("subject to:"):
            expression = line.split(":", 1)[1].strip()
            match = re.match(r"(?:(\w+)\s*:\s*)?(.+?)\s*(<=|>=|=)\s*(-?(?:\d+(?:\.\d*)?|\.\d+))\s*$", expression)
            if not match:
                raise ValueError(f"Invalid constraint: {line}")
            coefficients, quadratic = _parse_expression(match.group(2))
            if quadratic:
                raise ValueError("Quadratic constraint terms are outside V1 scope; only the objective may be quadratic.")
            data["constraints"].append({"name": match.group(1) or f"c{len(data['constraints']) + 1}", "coefficients": coefficients, "operator": match.group(3), "rhs": float(match.group(4))})
        elif lower.startswith("bound:"):
            match = re.match(r"bound:\s*([A-Za-z_]\w*)\s*\[\s*([^,]+),\s*([^\]]+)\]", line, re.I)
            if not match:
                raise ValueError(f"Invalid bound: {line}")
            data["bounds"][match.group(1)] = [_bound_value(match.group(2)), _bound_value(match.group(3))]
        else:
            raise ValueError(f"Unrecognized input line: {line}")
    return data


def _bound_value(value):
    value = value.strip().lower()
    if value in {"-inf", "-infinity", "none", "null"}:
        return None
    if value in {"inf", "+inf", "infinity", "+infinity"}:
        return None
    try:
        return float(value)
    except ValueError as exc:
        raise ValueError(f"Invalid bound value: {value}") from exc


def _parse_expression(expression):
    expression = expression.replace("−", "-").replace("²", "^2").replace(" ", "")
    if not expression:
        raise ValueError("Expression cannot be empty.")
    terms = re.findall(r"[+-]?[^+-]+", expression)
    linear, quadratic = {}, {}
    consumed = "".join(terms)
    if consumed != expression:
        raise ValueError(f"Invalid expression: {expression}")
    for term in terms:
        match = re.fullmatch(r"([+-]?)(?:(\d+(?:\.\d*)?|\.\d+))?([A-Za-z_]\w*)(\^2)?", term)
        if not match:
            raise ValueError(f"Invalid expression term: {term}")
        sign = -1 if match.group(1) == "-" else 1
        coefficient = sign * float(match.group(2) or 1)
        target = quadratic if match.group(4) else linear
        name = match.group(3)
        target[name] = target.get(name, 0) + coefficient
    return linear, quadratic
