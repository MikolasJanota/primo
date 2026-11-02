# Integral bounds and integral nonbasic variables

**Status: Design checklist with partial implementation.** Prevalence counters
are implemented. Gap 1 is implemented behind `--lia-round-integer-bounds`
gap 2 behind `--lia-primitive-int-rows` and gap 3 behind
`--lia-integer-slacks`, all default off. Gap 4 is planned and will also be
default-off when it lands.

This note lists the primary sources behind one invariant of integer
completion. It maps each source step to the Primo function that would own it
and records the obligations an implementation must meet.

## The invariant and why Primo does not have it yet

> Every integer-sorted nonbasic tableau variable holds an integer value.

Bjørner and Nachmanson state this as an assumption in §4.1 of *Arithmetic
Solving in Z3* {cite:p}`BjornerN24`: nonbasic variables sit at their bounds,
“and therefore if they have integer sort they are assigned integer values”.
The step from “at a bound” to “integer” needs integral bounds, which the paper
does not state.

Primo's simplex never leaves a nonbasic variable at an arbitrary value. The
{source}`LraTableau class comment <src/theory/arithmetic/lra_tableau.hpp>`
explains that nonbasic variables sit within, not necessarily at, their bounds.
Without patching, a nonbasic value is its initial 0 or a bound value it was
moved to. `pop()` restores bounds but not values, so after backtracking a
nonbasic variable can sit strictly inside its interval. That stale value is
still an earlier bound value. If every bound on an integer variable is an
integer, the invariant therefore holds without patching.

Four preparation and completion choices break it:

| Gap | Current owner | Effect |
| --- | --- | --- |
| 1. Non-strict integer thresholds are not rounded | {source}`bound_assertion <src/smt/solver_engines.cpp>` rounds only strict bounds | `3*x <= 5` becomes the bound $x\le5/3$ |
| 2. Leading-coefficient-one rows | `canonical_polynomial` and `PreparedLraTheory::append_atom` | `2*x + 3*y` becomes $s=x+\tfrac32y$, which is not integer-valued; the slack is marked integer only for scale $\pm1$ |
| 3. Integer slacks are not integer obligations | `LiaComponentAdapter` builds `integer_vars` only from `Int` terms | patching, GCD, cuts and branching treat every slack as continuous |
| 4. Patch moves | `collect_candidates` in {source}`lia_patching.cpp <src/theory/arithmetic/lia_patching.cpp>` | a row move $\Delta=(n-\beta(x_b))/a$ can leave an integer nonbasic fractional |

Patching runs only on the private integer-completion copy of the tableau, so
gap 4 never reaches the online tableau. It affects only the later stages of
the same completion.

## Sources and the steps they contribute

| Source | Step used | Planned Primo owner |
| --- | --- | --- |
| King, thesis, §4.3, pp. 106–109 {cite:p}`king2014` | For an all-integer relation, clear denominators, turn strict into non-strict by $\pm1$, divide by the coefficient GCD $g$, and round the threshold: $\lfloor d/g\rfloor$ for $\le$, $\lceil d/g\rceil$ for $\ge$; an equation with $g\nmid d$ is false. The leading coefficient's sign is normalized for sharing. Negated integer bounds are tightened when asserted. King concludes that “the upper and lower bounds of all integer variables are always integer values”. Footnote 4: an auxiliary variable for an all-integer sum “may also be marked as an integer variable”. | Gaps 1–3: `bound_assertion`, `canonical_polynomial`, `append_atom`, the LIA adapter's `integer_vars_` |
| Achterberg, Bixby, Gu, Rothberg and Weninger, ZIB Report 16-44 version {cite:p}`achterberg2020presolve` | §3.5, equation (3.7): divide an all-integer row by the GCD and round the right side; its modular-inverse substitution. §4.2: rounding fractional bounds of integer variables is mandatory in Gurobi because its MIP algorithms rely on integral bounds. §7.6: primal implied-integer detection for a variable defined by an all-integer equation. | Gaps 1–3 as MIP counterparts; §3.5's modular inverse motivates the gap-4 lattice move |
| Bjørner and Nachmanson, §4.1 {cite:p}`BjornerN24` | Patching moves nonbasic values to make basic integer variables integral without breaking any bound. | Gap 4: `patch_integer_variables` |

The gap-4 **lattice move** is Primo's own inference, not a procedure from
these papers. For an integer nonbasic $y$ with coefficient $a=p/q$ ($p,q$
coprime, $q>0$) in the row of basic $x_b$, let $c=\beta(x_b)-a\beta(y)$.
Setting $y=k$ for integer $k$ gives $x_b=c+ak$. If $c$ has no infinitesimal
part and $qc\in\mathbb Z$, then $x_b$ is integral exactly when
$pk\equiv-qc \pmod q$. The solutions are $k=k_0+jq$ for integers $j$, with
$k_0$ obtained from the inverse of $p$ modulo $q$. Otherwise no integer value
of $y$ alone makes $x_b$ integral.

## Deviations from the sources

- King rewrites atoms during normalization. Primo keeps each SAT atom and its
  threshold, and applies the integer strengthening when bounds are prepared
  and asserted. Reasons stay the original atom literals.
- King's rewrite of odd right-hand sides for extra sharing is not planned.
- Real atoms keep leading-coefficient-one normalization; only `Int` atoms
  change, and only behind an option.
- Achterberg et al. report Gurobi measurements. They are not evidence for
  Primo.

## Proof obligations

1. **Rounding.** If $t$ is integer in every integer model, then $t\le r$ is
   equivalent to $t\le\lfloor r\rfloor$ and $t\ge r$ to $t\ge\lceil r\rceil$.
   Apply this only to a bound variable known to be integer.
2. **Slack integrality.** If every coefficient of $s=\sum_i c_ix_i$ is an
   integer and every $x_i$ is `Int`, then $s$ is integer in every integer
   model. Under leading-coefficient-one normalization the coefficients can
   be fractional (the `-5/2^32` comment in `append_atom`); the flag must
   follow the stored row, not the input atom.
3. **Shared atoms and both polarities.** Assertion, entailment checks,
   propagation and model-phase hints must use the same effective bound.
4. **Explanations.** A rounded bound keeps the original reason literal.
   Gomory, GCD and branching explanations remain valid when the integer
   variable is a slack.
5. **Patch feasibility.** Every move stays within the moved variable's
   freedom interval, as now.

Complexity: the coefficient GCD is linear in the row length; the modular
inverse costs one extended Euclid run in $q$.

## Counters

These counters measure how common each gap is. They do not change behavior.

| Statistic | Meaning |
| --- | --- |
| `:lia.prep-fractional-int-thresholds` | Atoms whose bounded variable is known to be integer but whose threshold is fractional (gap 1). |
| `:lia.prep-non-integer-int-slack-atoms` | Compound `Int` atoms whose slack is not marked integer (gap 2). |
| `:lia.prep-non-integer-int-slacks` | Distinct `Int` slacks whose stored row has a fractional coefficient (gap 2). |
| `:lia.patch-fractional-nonbasic-moves` | Accepted patch moves that left an integer nonbasic fractional (gap 4). |

### Prevalence on the corpora

A census at commit `18630fd` ran the default configuration with `--stats`
over the full QF_LIA division (13,306 files) and the QF_UFLIA corpus (659
files), 10 s per file on SLURM (jobs 334940 and 334941; run-log entry
`2026-11-02-lia-integrality-census-18630fd`). Most timed-out runs printed no
statistics, so these counts are lower bounds:

| Gap | QF_LIA | QF_UFLIA |
| --- | --- | --- |
| 1 (fractional threshold on an integer-flagged variable) | 44 files, 93 atoms | 20 files, 232 atoms |
| 2 (atoms on a non-integer `Int` slack) | 2,013 files, 162,540 atoms, 73,481 slacks; 18 of 32 families | 365 files (55%), 14,682 atoms, 2,892 slacks |
| 4 (patch moves leaving an integer nonbasic fractional) | 182 files, 10,013 moves, mostly `rings` and `rings_preprocessed` | none |

Gap 2 dominates. Gap 1 is rare today only because few bounded variables are
flagged integer: once gap 2 is closed, every `Int` slack becomes integer and
the rounding of gap 1 applies to its thresholds as well.

## Tests derived from the steps

Gap 1 is pinned by `rounding_integer_bounds_keeps_answers_and_skips_completion`
in {source}`qf_lia_solver_tests.cpp <tests/unit/qf_lia_solver_tests.cpp>`
(upper and lower thresholds, a negated `<`, a negative scale, an integral
model without patching) and `rounding_integer_bounds_reaches_the_combined_route`
in {source}`qf_uflia_solver_tests.cpp <tests/unit/qf_uflia_solver_tests.cpp>`.
The existing strict-bound regressions are unchanged.

Gap 2 is pinned by `primitive_integer_rows_keep_answers` (parity refutation
without completion, a strict compound atom, the large-leading-coefficient
case, proportional atoms of opposite sign, an exact model; no option
combination may contradict an answer) and
`integer_preparation_options_do_not_touch_real_arithmetic`.

Gap 3 is pinned by `branching_on_an_integer_slack_gives_a_bound_explanation`
in {source}`lia_completion_tests.cpp <tests/unit/lia_completion_tests.cpp>`
(obligation 4: the split on a slack drops out of the explanation, leaving the
two bound literals) and `integer_slacks_join_completion` in the QF_LIA solver
tests (refuted only when the slack is an obligation). Slacks are placed
before the input variables; appended after them they would never be split.

The counters are pinned by
{source}`qf_lia_solver_tests.cpp <tests/unit/qf_lia_solver_tests.cpp>`
(`integer_preparation_counters_report_bound_gaps`) and
{source}`lia_patching_tests.cpp <tests/unit/lia_patching_tests.cpp>`
(`fractional_nonbasic_move_is_counted`). Planned behavior tests:

- integer slacks: a cut from a basic slack gives a valid explanation;
- patching: the $s_1=s_2=\tfrac23y$ move to $y=3/2$ is rejected; the lattice
  finds $y=3$ when it is in range; a coefficient with $|p|>8$ that the fixed
  target window misses; a $c$ with an infinitesimal part is skipped.
