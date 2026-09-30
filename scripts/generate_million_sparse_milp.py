"""Generate a reproducible, synthetic MPS scaling smoke model.

This is intentionally a sparse ingestion/solver-path check, not a MIPLIB
benchmark or evidence of general million-variable MILP performance.
"""
from __future__ import annotations

import argparse
from pathlib import Path


def generate(path: Path, variable_count: int) -> None:
    if variable_count < 1:
        raise ValueError("variable_count must be positive")
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="ascii", newline="\n", buffering=1024 * 1024) as output:
        output.write("NAME MILLION_SPARSE\nROWS\n N OBJ\n L CAP\nCOLUMNS\n")
        for index in range(variable_count):
            name = f"X{index:07d}"
            if index == 0:
                output.write(f" {name} OBJ -1 CAP 1\n")
            else:
                output.write(f" {name} OBJ 0\n")
        output.write("RHS\n RHS1 CAP 0.5\nBOUNDS\n")
        for index in range(variable_count):
            output.write(f" BV BND X{index:07d}\n")
        output.write("ENDATA\n")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="path for generated MPS input")
    parser.add_argument("--variables", type=int, default=1_000_001)
    args = parser.parse_args()
    generate(args.output, args.variables)
    print(f"Generated synthetic sparse MILP with {args.variables:,} binary variables: {args.output}")


if __name__ == "__main__":
    main()
