# Integral bounds and integral nonbasic variables

**Status: Design checklist with partial implementation.** Prevalence counters
are implemented. Gap 1 is implemented behind `--lia-round-integer-bounds`
gap 2 behind `--lia-primitive-int-rows`, gap 3 behind
`--lia-integer-slacks` and gap 4 behind `--lia-patch-integral-moves`, all
default off. The {ref}`evaluation <integrality-evaluation>` found no wrong
answers and did not qualify any option for the default.

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

(integrality-evaluation)=
## Evaluation at `2e120fa`

The fuzz gates and timing runs used one Release binary on SLURM; the unit
tests also ran under an ASan Debug build. They are recorded in
`docs/run-log.json` as `2026-11-02-lia-integrality-fuzz-gates-2e120fa` and
`2026-11-02-lia-integrality-ab-2e120fa`.

**Correctness.** No gate found a wrong answer, crash, invalid model or
reference disagreement:

- 4,000 QF_LIA FuzzSMT cases with all options, and 1,000 per option,
  against z3. The per-option gates reuse inputs of the all-options gate, so
  they compare options rather than adding independent cases;
- 3,000 QF_UFLIA cases with all options;
- Dolmen checks of every SAT model from these gates;
- opfuzz and typefuzz on QF_LIA seeds and typefuzz on QF_UFLIA seeds. These
  ran only until their time limits, so coverage of every seed is not
  established;
- Release and ASan unit tests.

In the timing runs below, every QF_LIA answer agrees with z3 on the 12,144
files z3 decided, and every QF_UFLIA answer agrees with z3 and cvc5.
Dolmen accepts every SAT model printed in those runs with all options
(6,543 QF_LIA, 374 QF_UFLIA) and with M1+M2 (6,483 QF_LIA); none of these
answers turns UNSAT on replay (run-log entry
`2026-11-03-lia-integrality-model-validation-2e120fa`).

**Timing**, 60 s per file, each configuration adding one option to the
previous one. M1 to M4 are the options for gaps 1 to 4. PAR2 is in seconds,
counting 120 s for each unsolved file:

| Configuration | QF_LIA solved (of 13,306) | QF_LIA PAR2 | QF_UFLIA solved (of 659) | QF_UFLIA PAR2 |
| --- | --- | --- | --- | --- |
| all off | 9,662 | 474,705 | 575 | 11,375 |
| + round bounds (M1) | 9,662 | 474,766 | 575 | 11,376 |
| + primitive rows (M2) | 9,766 | 463,085 | 545 | 14,902 |
| + integer slacks (M3) | 9,465 | 498,325 | 544 | 14,972 |
| + integral patch moves (M4) | 9,798 | 459,979 | 544 | 14,934 |

- **M1** is neutral alone.
- **M2** sharply reduces patching on QF_LIA: generalized patch lemmas fall
  from 177,808 to 14,585, and patch sweeps from 184,996 to 21,537. This
  suggests that more relaxation solutions are already integral.
- **Adding M3 without M4** is harmful.
- **M4** with all the others gives the best QF_LIA result (+411 −275 files
  against all off) and accepts 528 lattice moves.
- **On QF_UFLIA**, every configuration with M2 loses 30 `wisas` files. Adding
  M3 also loses one Certora file.
- The per-file churn is large, and no rerun of the all-off configuration
  measured the timing noise.

**The `wisas` regression.** The file `xs_28_48.smt2` states
`arg1 = arg0 + 4*s_count(p) + 4*x_count(p)` for one position `p`. After
purification, $a$ and $b$ are the constants for `s_count(p)` and
`x_count(p)`, and $t=a+b$ counts format specifiers. Preprocessing produces 25
pairs of threshold atoms $4t-48\le k$ and $4t-48\ge k$ for $k=0,\dots,24$,
plus the strict atom $4t-48<24$, all on the one slack $t$:

- The default rule marks this slack non-integer because its scale is 4.
  Its strict bounds then use the infinitesimal $\delta$ of the rational
  relaxation: $t<18$ becomes $t\le18-\delta$.
- M2 marks it integer, so strict bounds on it are asserted as integer bounds.
- A temporary local experiment, since reverted, removed this rounding from
  the combined route's online bound assertion only, and the run time went
  from a timeout back to 0.4 s. Removing it from the model-phase hints,
  propagation or the equality checks did not help.
- Off, the whole run takes 385 SAT conflicts. With M2 alone there are
  62,873 by the 30 s timeout.

**Root cause of the `wisas` regression.** It is a search-order effect, not a
soundness or implementation defect in M2. A temporary trace, since reverted,
recorded the value and bounds of $t$ at every point where CaDiCaL asks the
arithmetic solver for a conflict.

1. The bounds asserted first are $t\ge12$ and the strict $4t-48<24$. Off,
   the strict bound is $t\le18-\delta$. M2 rounds it to $t\le17$.
2. CaDiCaL's first decisions on the threshold atoms push $t$ to the top of
   its range. Off, the top threshold below $18-\delta$ is $4t-48=23$, so $t$
   is pinned at $71/4$. That value is infeasible over the integers and was
   quickly refuted; the mechanism was not traced. The search then settles at $t=12$, its
   lower bound, which is feasible. In one run, 9,360 of the 9,407 samples
   are $t=12$, and the file is SAT in 0.3 s.
3. With M2 the top value is $t=17$, which is integral, so integer
   completion accepts it. In an 8 s run, 82,420 of the 82,508 samples have
   $t=17$. A second traced run has both bounds at 17 in 71,794 samples.
   Model-based combination then adds equalities between function arguments
   that share these integral values, such as $4t-48$ and an index of value
   20.
4. About 85% of the resulting theory conflicts (35,334 of 41,327 in 6 s)
   are 55 or 56 literals long. The 56-literal ones name one increment
   pattern of both counter chains, so each such clause rules out one
   pattern rather than the bound on $t$.

The region $t\ge14$ is hard for every solver tried, within 25 to 30 s.
Adding `(assert (>= arg1 (+ arg0 4k)))` to `xs_28_48.smt2` gives a SAT file
for $k=13$ (Primo 0.35 s, yices 0.05 s, cvc5 9.6 s). For each $k$ from 14
to 17, Primo and z3 time out at 30 s. cvc5 and yices were run for $k=14$ and
$k=17$ only, and time out at 25 s. The original file appears easy only when
the search does not commit to an integral $t\ge14$. Off avoids that because
its first commitment is fractional and cheap to refute.

Phase hints do not explain the difference. `--no-lra-model-phase` leaves
both runs as they were. Forcing model-guided phases with
`--lra-model-phase` makes the off run time out as well.

The lost files `xs_24_44`, `xs_33_53` and `xs_40_60` were traced for the
most frequent value of $t$ only. In `xs_A_B`, $t$ ranges from $B/4$ to below
$(A+B-4)/4$. In each file, off settles at the smallest integer in that
range, and M2 at the largest:

| File | Range of $t$ | Off: settled value | M2: settled value |
|---|---|---|---|
| `xs_24_44` | $[11, 16)$ | 11 | 15 |
| `xs_33_53` | $[53/4, 41/2)$ | 14 | 20 |
| `xs_40_60` | $[15, 24)$ | 15 | 23 |

The remaining 26 lost files were not traced.

Removing the rounding at that call site is sound, since it only weakens a
bound. It restores the run time only by bringing back the fractional top
value, so it is not a principled fix. The regression is a family-specific
search sensitivity, and it still counts against promoting M2.

**Reruns and seed mutation (2026-11-03).** Both corpora were rerun at
`2e120fa` with the options off and with all four on, at 60 s per file (jobs
339382 and 339383):

- **QF_UFLIA reproduces exactly.** Both arms solve the same files as before,
  575 and 544, so the 31-file loss is stable.
- **The options-off QF_LIA arm reproduces within five files** each way (9,662
  both times).
- **The all-options QF_LIA arm solved 9,745, 53 fewer.** All 60 files it lost
  had solved in 53 to 60 s before; 57 are `nec-smt`. Counting only files that
  behave the same in both runs, the options gain 348 and lose 274 against off,
  a net of +74 rather than the +136 of the first run. The 57 lost `nec-smt`
  files all sat near the cutoff.

The seed-mutation campaign ran one task per fuzzer and seed with all four
options (jobs 339380 and 339381, {source}`slurm_seed_mutation_per_seed.sbatch
<scripts/slurm_seed_mutation_per_seed.sbatch>`). Opfuzz ran 200 and typefuzz
100 mutants per seed against z3, at 5 s per solver call. It found no bug
files:

- On the 32 QF_UFLIA seeds (8 per family), every pair was covered.
- On the 59 QF_LIA seeds within the 200 kB limit, 116 of 118 pairs were
  covered. Typefuzz on the two `tropical-matrix` seeds made no solver call
  before the 90-minute wall. Opfuzz covered both seeds.

**Decision.** No option is promoted to the default. M2 and M4 together are
the promising part on QF_LIA, though their stable net gain there is smaller
than the first run suggested. They lose 31 QF_UFLIA files reproducibly, which
puts Primo's QF_UFLIA count back to roughly its September level, 544
against 543 (see [QF_UFLIA progress](../evidence/qf-uflia-progress.md)).
The two uncovered typefuzz pairs are accepted as a tool stall rather than a
solver gap, because no solver call was made; they would need a rerun with
a larger wall before any promotion. Recovering the `wisas`
files is now an open lead on [search
diversification](../leads/search-diversification.md); a time-sliced
[portfolio](../leads/portfolio.md) is a separate project.

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
two bound literals), `gomory_cut_from_an_integer_slack_row` (one cut refutes
a slack row with no integer in range, explained by the bound literals only) and `integer_slacks_join_completion` in the QF_LIA solver
tests (refuted only when the slack is an obligation). Slacks are placed
before the input variables; appended after them they would never be split.

Gap 4 is pinned in {source}`lia_patching_tests.cpp <tests/unit/lia_patching_tests.cpp>`:
the $s_1=s_2=\tfrac23y$ move is rejected; the lattice finds $y=3$ for
$s=\tfrac{11}{3}y$ where the target window cannot; an offset with an
infinitesimal part yields no lattice candidate; a 500-trial brute force over
$s=(p/q)y+bw$ ($|p|\le30$, $q\le7$) checks that integral patching makes $s$
integral exactly when some integer $y$ in range does; a 300-trial randomized
run checks feasibility, no fractional integer nonbasic, and a non-increasing
fractional count; and an explicit `false` reproduces the established patcher.
Injected faults in the residue sign and in the rejection filter are caught.
`integral_patch_moves_reach_completion` checks the solver wiring and runs the
earlier cases with all four options on. The `Rational::numerator()` and
`denominator()` accessors this relies on are checked against GMP and their
algebraic contract over all sample values and pairwise results, including
promotion to and demotion from the big form.

The counters are pinned by
{source}`qf_lia_solver_tests.cpp <tests/unit/qf_lia_solver_tests.cpp>`
(`integer_preparation_counters_report_bound_gaps`) and
{source}`lia_patching_tests.cpp <tests/unit/lia_patching_tests.cpp>`
(`fractional_nonbasic_move_is_counted`).
