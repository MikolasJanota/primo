# Arithmetic efficiency follow-ups

**Status: open; ordered by the 8 November diagnostic review.** These leads
follow the interval-cache and CITE comparison experiments. None is implemented
by those changes, and neither micro-optimization closes the reference-solver
gap. Measurements below are historical evidence from
[the frozen twelve-case review](../evidence/arithmetic-efficiency-20261108.md),
not acceptance evidence for a future implementation. The
[Bromberger checklist](../papers/bromberger-linear-arithmetic.md#efficiency-review-checklist)
records the primary-source reading and the implemented subsets.

## 1. Incremental shared-model regrouping

On diagnostic case 10 (`parport`), native time was 5.48 s versus Z3's 1.16 s;
38 of 55 native stack samples contained `group_by_model_value`. Its 15 mutation
calls accepted 1,291 updates and pruned 260,008 candidate pairs. These inclusive
samples establish a repeated cost, not an exclusive time percentage.

{cubesource}`LraTableau::diversify_shared_model <src/theory/arithmetic/lra_tableau.cpp>`
should own exact `DeltaRational` bucket counts in its scratch model. Bucket-count
maintenance touches only affected shared variables, while scratch-assignment
updates still cover every affected basic variable, including nonshared rows
needed by later freedom queries;
a rejected trial must restore every changed value and count. Count singleton
buckets, preserve forbidden-pair checks and nondecreasing class counts, and
export classes in the existing deterministic order. The boundary excludes EUF,
SAT arrangement policy and the live tableau's assignment. Bromberger §5.4
supplies the Nelson–Oppen context and §§5.2–5.3 the equality-basis machinery;
§7.1.4 motivates derived-state
maintenance. This proposed bookkeeping is a Primo adaptation, not the thesis's
complete equality-basis algorithm.

Next, count regrouped values and allocations on the frozen `parport` case and
a synthetic large interface with a small affected column. Include accepted and
rejected moves, duplicate IDs, infinitesimals and multiple affected shared
variables. Compare exported classes and trial choices to the baseline; reject
on any changed decision or lost forbidden separation before timing wider suites.

## 2. Sharing-sensitive Boolean flattening

On diagnostic NEC case 0, caps 0, 256 and 2048 produced 1.60, 2.32 and 8.72
million clauses respectively. Two 60-second runs at cap 256 took 12.88/12.19 s,
versus 24.76/24.87 s at 2048. Disabling flattening slowed the LassoRanker
control from about 1.15 s to 6.1 s. These four-case results do not qualify a
replacement default.

Bromberger §8.2.3, p. 275 stops ITE compression at shared tails; §8.2.5 describes
the NEC interaction with CITE simplification. A Primo experiment would modify
{cubesource}`flatten_associative_booleans <src/smt/preprocessing.cpp>` in preprocessing,
retaining shared tails using
parent occurrence counts. It must preserve exact Boolean meaning, deterministic
child order and shared DAG structure. The existing 2048 threshold stays fixed.
Next compare retained tails, encoded clauses and preprocessing time against the
unchanged threshold on NEC and the LassoRanker/Labyrinth controls, then on a
broader mixed suite. Reject on correctness failures or a reproducible total-time
regression even if clause counts shrink.

## 3. Common row denominators

In the review's ring cases 2/3 and slacked-cut case 5, GMP occurred in 77–373
native samples depending on profile and case; pivoting dominated many stacks.
Case 5 remained in root Gomory work rather than branching. Capped Callgrind
prefixes exposed division/GCD normalization, but are not completed native timings.

Bromberger §7.1.4, pp. 223–225 and Figure 7.17 motivate integer numerators with
one positive denominator per row.
{cubesource}`LraTableau <src/theory/arithmetic/lra_tableau.hpp>` would own the representation:
pivoting, row/column index maintenance, assignment updates, bound queries,
normalization and SOI rollback must all agree on it. Preserve sorted variable
IDs, occurrence backlinks, exact row equations and valid trail snapshots;
normalizing a shared GCD must preserve the represented coefficients. A pivot-only
prototype cannot establish this contract.

Next instrument numerator/denominator sizes and normalization counts on the
three frozen cases. Prototype complete row operations behind a private control,
then compare exact rows, assignment updates and push/pop restoration before
paired timing. The existing inline-small/GMP `Rational` is already a hybrid;
a library substitution alone is a separate experiment. Reject the prototype
on any exact-state/rollback mismatch or reproducible paired total-time
regression; deprioritize it if normalization savings do not reduce total work.

## 4. Simultaneous rounding and per-node numerical propagation

The review's worked $0\le2x+y\le1$, $0\le x+2y\le1$ example distinguishes
simultaneous rounding from strictly improving single-variable patching. It is a
static reachability example, not a measured frequency estimate. The ring and
root-cut profiles above do not establish that either missing mechanism is the
next dominant cost.

Treat these as separate mechanisms. Bromberger §§2.7.4 and 7.2.4 motivate
rounding semantic integer coordinates followed by original-constraint/model
validation; rejection must leave the working tableau unchanged and cannot
prove UNSAT. Sections 7.2.3–7.2.4 motivate bounded numerical propagation at each
integer-search node. Its tightened exact bounds need explanations, a work budget
and rollback separate from propagation of existing SAT atoms.

The modification points are {cubesource}`integer completion
<src/theory/arithmetic/lia_completion.cpp>` and
{cubesource}`try_branch <src/theory/arithmetic/lia_branch_and_bound.cpp>`.
Next count valid simultaneous-rounding candidates at completion boundaries on
the frozen LIA development set without adopting them. Independently count useful
per-node tightened bounds on cases that actually reach branching. Specify model
reconstruction and scheduling contracts before implementing either adoption path.
Continue rounding only if valid candidates avoid meaningful downstream work at
a cost below that work; otherwise deprioritize it. Independently continue
propagation only if useful explained tightenings can repay their traversal and
rollback cost. Reject either adoption mechanism on invalid models, explanations
or restoration, or on reproducible paired total-time regression.

## 5. Lower-priority coefficient access, violations and branching

The [column-coefficient lead](lra-column-coefficients.md) records searches through
affected rows during updates and freedom queries. The frozen patching case has
25/43 default samples in interval work; interval reuse addresses repeated calls
before replacing coefficient access. Few selected samples showed bounds-only
`pop` or leaving selection, so those mechanisms lack fresh dominance evidence.

Bromberger §§7.1.2–7.1.4 motivate direct entry access and changed-variable
maintenance; §7.2.2 motivates most-fractional branching/preferred children.
Keep ownership inside {cubesource}`LraTableau <src/theory/arithmetic/lra_tableau.cpp>`
for incidence and violation state, and inside
integer branching for heuristic order. Preserve backlinks across pivot/swap
removal, exact violation membership across bound rollback, and explanation and
termination contracts. Heuristic changes require separate comparisons because
they change the search rather than merely remove repeated work.

Next count coefficient searches/entries inspected, violation rebuild sizes and
branch-selection work separately on current binaries. Retain lower priority
unless profiles show material cost. Do not infer a branching remedy from a
case still processing root cuts.
