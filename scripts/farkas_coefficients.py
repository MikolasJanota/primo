"""Exact candidate generation for the separate Farkas optimization problem.

Algorithm/limitations: docs/leads/farkas-coefficient-minimization.md.
The SMTInterpol paper grounds the witness contract, not our cost objectives.
"""

from __future__ import annotations

import math
from fractions import Fraction

from check_lra_certificate import combine, parse


def weight_text(value):
    return (
        str(value.numerator)
        if value.denominator == 1
        else f"(/ {value.numerator} {value.denominator})"
    )


def cost(weights):
    return (
        max(
            (
                max(w.numerator.bit_length(), w.denominator.bit_length())
                for w in weights
                if w
            ),
            default=0,
        ),
        sum(len(weight_text(w)) for w in weights),
        sum(bool(w) for w in weights),
    )


def valid(premises, weights):
    if len(premises) != len(weights) or any(w < 0 for w in weights):
        return False
    polynomial = {}
    bound = Fraction(0)
    strict = False
    for (row, rhs, is_strict), weight in zip(premises, weights):
        if not weight:
            continue
        polynomial = combine(polynomial, row, weight)
        bound += rhs * weight
        strict |= is_strict
    return not polynomial and (bound < 0 or (bound == 0 and strict))


def better(candidate, incumbent, objective="size"):
    new, old = cost(candidate), cost(incumbent)
    order = (2, 0, 1) if objective == "support" else (0, 1, 2)
    return (
        new[0] <= old[0]
        and new[1] <= old[1]
        and tuple(new[i] for i in order) < tuple(old[i] for i in order)
    )


def integers(values, bits=4096):
    """Clear denominators and remove common content, with a construction budget."""
    denominator = 1
    for value in values:
        denominator = math.lcm(denominator, value.denominator)
        if denominator.bit_length() > bits:
            return None
    result = [v.numerator * (denominator // v.denominator) for v in values]
    divisor = math.gcd(*result) if result else 0
    if divisor:
        result = [v // divisor for v in result]
    return result if all(abs(v).bit_length() <= bits for v in result) else None


def cheap(premises, weights, bits=4096, objective="size"):
    """Bounded exact-checked heuristics; never increase bit height or printed bytes."""
    if not valid(premises, weights):
        raise ValueError("original weighted conflict is invalid")
    best = list(weights)

    def propose(candidate):
        nonlocal best
        if candidate is not None:
            candidate = list(map(Fraction, candidate))
            if better(candidate, best, objective) and valid(premises, candidate):
                best = candidate

    propose(integers(best, bits))
    propose([Fraction(bool(w)) for w in best])
    propose([Fraction(1)] * len(best))
    positive = [w for w in best if w]
    for scale in (min(positive), max(positive)):
        scaled = [w / scale for w in best]
        propose(scaled)
        for denominator in (1, 2, 4, 8, 16):
            propose([w.limit_denominator(denominator) for w in scaled])

    merged = [Fraction(0)] * len(best)
    groups = {}
    for i, (row, rhs, strict) in enumerate(premises):
        key = (tuple(sorted(row.items())), rhs, strict)
        first = groups.setdefault(key, i)
        merged[first] += best[i]
    propose(merged)
    propose(integers(merged, bits))

    # Opposite proportional rows: keep the tightest normalized bound per sign.
    opposite = {}
    for i, (row, rhs, strict) in enumerate(premises):
        if not row:
            candidate = [Fraction(0)] * len(best)
            candidate[i] = Fraction(1)
            propose(candidate)
            continue
        pivot = row[min(row)]
        key = tuple((name, value / pivot) for name, value in sorted(row.items()))
        sign = 1 if pivot > 0 else -1
        rank = (rhs / abs(pivot), not strict, i)
        entries = opposite.setdefault(key, {})
        if sign not in entries or rank < entries[sign][0]:
            entries[sign] = (rank, i, abs(pivot))
    for entries in opposite.values():
        if len(entries) != 2:
            continue
        candidate = [Fraction(0)] * len(best)
        for _, index, magnitude in entries.values():
            candidate[index] = 1 / magnitude
        propose(candidate)
        propose(integers(candidate, bits))
    propose(integers(best, bits))
    return best


def number(value):
    return str(value) if value >= 0 else f"(- {-value})"


def linear(values):
    terms = [f"(* {number(v)} w{i})" for i, v in enumerate(values) if v]
    return (
        "0"
        if not terms
        else terms[0]
        if len(terms) == 1
        else "(+ " + " ".join(terms) + ")"
    )


def support_objective(count):
    """Beautiful Interpolants, Appendix B.3: prefer zero Farkas multipliers."""
    return (
        "(minimize (+ " + " ".join(f"(ite (> w{i} 0) 1 0)" for i in range(count)) + "))"
    )


def optimization_problem(premises, incumbent, bits=4096, objective="size"):
    """Our bounded integer min-max/L1 model; all matrix entries are constants."""
    height = cost(incumbent)[0]
    if height > bits:
        return None
    lines = [
        "(set-logic QF_LIA)",
        "(set-option :produce-models true)",
        "(set-option :opt.priority lex)",
        "(declare-fun max_weight () Int)",
        "(assert (>= max_weight 0))",
        f"(assert (<= max_weight {(1 << height) - 1}))",
    ]
    for i in range(len(premises)):
        lines += [
            f"(declare-fun w{i} () Int)",
            f"(assert (>= w{i} 0))",
            f"(assert (<= w{i} max_weight))",
        ]
    variables = sorted({v for row, _, _ in premises for v in row})
    for variable in variables:
        column = integers(
            [row.get(variable, Fraction(0)) for row, _, _ in premises], bits
        )
        if column is None:
            return None
        lines.append(f"(assert (= {linear(column)} 0))")
    bounds = integers([rhs for _, rhs, _ in premises], bits)
    if bounds is None:
        return None
    constant = linear(bounds)
    strict = linear([int(s) for _, _, s in premises])
    lines.append(
        f"(assert (or (<= {constant} (- 1)) (and (= {constant} 0) (>= {strict} 1))))"
    )
    if objective == "support":
        lines.append(support_objective(len(premises)))
    lines += [
        "(minimize max_weight)",
        f"(minimize {linear([1] * len(premises))})",
        "(check-sat)",
        "(get-value (" + " ".join(f"w{i}" for i in range(len(premises))) + "))",
    ]
    return "\n".join(lines) + "\n"


def model_values(output, names):
    """Accept a complete integer get-value response, never a partial/unknown model."""
    head, separator, body = output.strip().partition("\n")
    if not separator or head.strip() != "sat":
        raise ValueError("optimizer did not return sat with a model")
    values = {}
    for pair in parse(body):
        if (
            not isinstance(pair, list)
            or len(pair) != 2
            or not isinstance(pair[0], str)
            or not isinstance(pair[1], str)
            or not pair[1].isdigit()
            or pair[0] in values
        ):
            raise ValueError("invalid optimizer assignment")
        values[pair[0]] = Fraction(int(pair[1]))
    if set(values) != set(names):
        raise ValueError("incomplete optimizer assignments")
    return [values[name] for name in names]


def model_weights(output, count):
    return model_values(output, [f"w{i}" for i in range(count)])
