# More selective and cheaper care-pair generation

**Status: Open lead. Owner: unassigned.**

Investigate whether proposing fewer care pairs, or producing them on demand,
reduces total solving time. This is the first investigation to try before
[using care information to reduce combination obligations](care-guided-combination.md).
No prototype is implemented. The {ref}`10 November QF_UFLIA investigation <uflia-gap-certora>`
provides fresh selected profiling evidence, not acceptance for a refinement.

## Implemented baseline

The [combination chapter](../implementation/combination.md#care-pairs-which-equality-questions-matter)
explains the current heuristic. The
{source}`EUF generator <src/smt/solver_engines.cpp#L2969-L3072>` groups flattened
applications by the congruence class of their function term, deduplicates shared
arguments, and enumerates distinct argument pairs within each group. A group
with `k` arguments can still generate `k(k - 1)/2` pairs.

The general component driver already
{source}`prioritizes disagreements on care pairs <src/smt/component_model_coordinator.cpp#L347-L423>`.
It retains complete model-partition comparison and complete-arrangement
fallback. QF_UFLIA
{source}`collects care pairs for each checked EUF model <src/smt/solver_engines.cpp#L9672-L9688>`.
The dedicated Real route already
{source}`caches care computations for an unchanged EUF assignment <src/smt/solver_engines.cpp#L11406-L11420>`;
adding that cache is not new work. Its
{source}`earlier optimization and measurements <docs/papers/nelson-oppen-closure-methods.md#L1544-L1580>`
remain historical evidence, not a prediction for another change.

## Paper basis and candidate changes

Dejan Jovanović and Clark Barrett, [*Being Careful about Theory Combination*](https://theory.stanford.edu/~barrett/pubs/JB13.pdf),
Formal Methods in System Design 42(1), 2013, pp. 67–90, §4.2, describes EUF
care pairs using congruence and known disequalities. It expands their
[*Sharing is Caring: Combination of Theories*](https://cvc4.github.io/publications/2011/JB11-FroCoS.pdf),
FroCoS 2011, pp. 195–210.

Audit which of the paper's exclusions can help Primo's heuristic: argument
pairs already known equal or disequal, application results already in the same
congruence class, and application pairs separated by another known-disequal
argument. Map these conditions to the curried representation before using them;
the current generator does not implement the full §4.2 construction.

Separately investigate lazy generation within large function classes. This is
an engineering proposal, not an implemented paper mechanism. Account for the
cost of identifying the next useful pair, scope validation, and the current
deterministic choice order. If an alternative changes decision ordering, record
that as a search-policy change rather than attributing all effects to reduced
generation work.

Keep complete model comparison, explained consequences, and fallback intact.
A filtered-out care suggestion does not authorize omitting an equality required
for acceptance. Any additional cache must account for closure changes,
backtracking, new terms, and the model token to which its results belong.

## First experiment and acceptance

Profile the current generator and consumer separately. Compare the unchanged
baseline, a control without care priority, and one refinement at a time.
Measure generated and inspected pairs, generation time, peak memory, SAT choices
and calls, fallback frequency, solved count, total runtime, and PAR2. Fewer
generated pairs alone do not establish an improvement.

Extend the existing
{source}`generation regressions <tests/unit/qf_lra_solver_tests.cpp#L2068-L2140>`
and {source}`selection and validation tests <tests/unit/component_combination_tests.cpp#L652-L701>`
for duplicate arguments, settled pairs and results, multiargument applications,
newly relevant pairs after merges or rollback, and disagreements outside the
care list. Run the applicable fresh [fuzzing gates](../development/correctness.md)
and independent SAT-model validation before accepting performance results.

Use paired evaluations on representative QF_UFLRA and QF_UFLIA inputs,
including both large care classes and cases with little care work. Reject or
restrict a refinement if its bookkeeping or changed search loses the savings,
or if gains on a few cases hide substantial broader regressions. Record the
decision and reproducible evidence before closing the lead.

## Fresh selected QF_UFLIA evidence

The default Certora `39657…_48` run takes 24.487 s, against archived cvc5’s
1.108 s. Care-pair construction appears in 22 of 161 inclusive native stack
observations; pivoting appears in 34 and GMP in 25. These overlap and are not
exclusive CPU percentages. See the [diagnostic record](../evidence/uflia-gaps-20261110.md).
First count unchanged EUF assignment/closure epochs, rebuilt candidate pairs
and allocation volume. A cache preserving the exact pair list and ordering
is a separate experiment from changing the care heuristic. Invalidate on
relevant closure merges, rollback, added terms and model-token changes; retain
complete model comparison and fallback. The profile does not establish which
cache would be valid or that caching alone repays its bookkeeping cost.
