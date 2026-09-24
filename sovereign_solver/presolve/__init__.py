"""Safe, solver-independent presolve transformations."""

from .presolver import PresolveResult, Presolver, presolve

__all__ = ["PresolveResult", "Presolver", "presolve"]
