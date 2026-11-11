"""Exact fixed-conclusion integer-cut simplification, independent of native search.

The linear floor-interval model is derived in
docs/papers/arithmetic-certificate-optimization.md. Sparsity is motivated by
Beautiful Interpolants, Appendix B.3; its interpolation algorithm is not used.
"""

from __future__ import annotations

import math
from fractions import Fraction

from check_lia_certificate import add
from farkas_coefficients import better, cost, linear, number, support_objective


def replay_cut(premises, weights, divisor):
    """Replay the version-3 rule, including negative mathematical floor."""
    if (
        len(premises) != len(weights)
        or not isinstance(divisor, int)
        or divisor < 1
        or any(not isinstance(w, int) or w < 0 for w in weights)
        or not any(weights)
    ):
        raise ValueError("invalid integer cut weights/divisor")
    coefficients, bound = {}, 0
    for (row, rhs), weight in zip(premises, weights):
        if not weight:
            continue
        coefficients = add(coefficients, row, weight)
        bound += rhs * weight
    if any(c % divisor for c in coefficients.values()):
        raise ValueError("inexact integer cut division")
    return {v: c // divisor for v, c in coefficients.items()}, bound // divisor


def cut_vector(weights, divisor):
    return list(map(Fraction, [*weights, divisor]))


def cheap_cut(premises, weights, divisor, bits=4096, objective="size"):
    target = replay_cut(premises, weights, divisor)
    best, best_divisor = list(weights), divisor

    def propose(candidate, d):
        nonlocal best, best_divisor
        if not better(
            cut_vector(candidate, d), cut_vector(best, best_divisor), objective
        ):
            return
        try:
            conclusion = replay_cut(premises, candidate, d)
        except ValueError:
            return
        if conclusion == target:
            best, best_divisor = list(candidate), d

    def reduce_content(candidate, d):
        common = math.gcd(d, *candidate)
        propose([w // common for w in candidate], d // common)

    reduce_content(best, best_divisor)
    propose([int(bool(w)) for w in best], best_divisor)
    propose([1] * len(best), best_divisor)
    propose(best, 1)
    merged, groups = [0] * len(best), {}
    for i, (row, rhs) in enumerate(premises):
        first = groups.setdefault((tuple(sorted(row.items())), rhs), i)
        merged[first] += best[i]
    # Avoid constructing an unbounded additional encoding after row merging.
    if all(w.bit_length() <= bits for w in merged):
        propose(merged, best_divisor)
        reduce_content(merged, best_divisor)
    reduce_content(best, best_divisor)
    return best, best_divisor


def cut_optimization_problem(premises, weights, divisor, bits=4096, objective="size"):
    """Preserve a fixed a*x<=b by sum(w*a_i)=d*a and d*b<=sum(w*b_i)<d*(b+1)."""
    target, bound = replay_cut(premises, weights, divisor)
    height = cost(cut_vector(weights, divisor))[0]
    constants = [bound, bound + 1, *target.values()]
    constants += [c for row, rhs in premises for c in (*row.values(), rhs)]
    if height > bits or any(abs(c).bit_length() > bits for c in constants):
        return None
    lines = [
        "(set-logic QF_LIA)",
        "(set-option :produce-models true)",
        "(set-option :opt.priority lex)",
        "(declare-fun max_weight () Int)",
        "(assert (>= max_weight 1))",
        f"(assert (<= max_weight {(1 << height) - 1}))",
        "(declare-fun divisor () Int)",
        "(assert (>= divisor 1))",
        "(assert (<= divisor max_weight))",
    ]
    for i in range(len(premises)):
        lines += [
            f"(declare-fun w{i} () Int)",
            f"(assert (>= w{i} 0))",
            f"(assert (<= w{i} max_weight))",
        ]
    variables = sorted({v for row, _ in premises for v in row})
    for variable in variables:
        column = linear([row.get(variable, 0) for row, _ in premises])
        lines.append(
            f"(assert (= {column} (* {number(target.get(variable, 0))} divisor)))"
        )
    total = linear([rhs for _, rhs in premises])
    lines += [
        f"(assert (>= {linear([1] * len(premises))} 1))",
        f"(assert (<= (* {number(bound)} divisor) {total}))",
        f"(assert (< {total} (* {number(bound + 1)} divisor)))",
    ]
    if objective == "support":
        lines.append(support_objective(len(premises)))
    lines += [
        "(minimize max_weight)",
        f"(minimize (+ divisor {linear([1] * len(premises))}))",
        "(check-sat)",
        "(get-value (" + " ".join(f"w{i}" for i in range(len(premises))) + " divisor))",
    ]
    return "\n".join(lines) + "\n"
