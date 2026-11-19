# Detecting bounds before arithmetic search

**Document purpose: completed diagnostic implementation and measurement record.** The diagnostic is implemented and does not strengthen the goal. Current controls belong in [options](../reference/options.md#preprocessing-strengthening-and-diagnostics).

This milestone measures opportunities only. It does not strengthen the goal,
install tableau bounds, generate SAT atoms, or change a solving answer.

## Basis and implementation checklist

The algebraic basis is Dutertre and de Moura, *A Fast Linear-Arithmetic Solver
for DPLL(T)*, CAV 2006, §4.5, read before implementation from the
[author's copy](https://leodemoura.github.io/files/cav06.pdf). That section
describes deriving bounds from a linear equation and bounds on its terms,
including equations outside the current tableau basis. This detector is an
original, limited diagnostic using that interval calculation; it is not a
complete implementation of the paper's theory propagation.

- The SMT layer owns traversal of the original assertion conjunction, before
  preprocessing changes its shape. Only positive conjunctions and arithmetic
  literals are inspected. Facts inside disjunctions are not unconditional.
- {rootboundsource}`record_root_bound_diagnostics
  <src/smt/root_bound_diagnostics.hpp>` reads a const term manager and assertion IDs.
  Local owned polynomials, bound maps and counters die after each query. No
  term creation, SAT literal, tableau variable, or persistent state is needed.
  A polynomial leaf may be an arithmetic constant or an opaque UF application;
  the latter's arguments are not inspected for extended arithmetic.
- Collect explicit single-term lower and upper bounds first, independent of
  assertion order. Negated comparisons reverse direction; disequalities do
  not supply interval bounds. Integer endpoints use exact floor/ceiling;
  Real endpoints retain strictness through symbolic infinitesimals.
- For each multi-term constraint, isolate each target using only those
  explicit bounds. In a normalized inequality `sum(a_i*x_i) <= b`, subtract
  the minimum contribution of every other term and divide by the target
  coefficient, reversing direction when it is negative. Equalities supply
  both inequality directions. Strictness is preserved, then integer results
  are rounded. Missing required endpoints prevent that direction's inference.
- Keep the strongest candidate for each term and direction, and compare it
  with its explicit bound. Count distinct improved endpoints, newly finite
  endpoints, affected terms and constraints, and integer rounding events.
  Repeated constraints must not inflate the distinct endpoint count.
- This is one pass, with no candidate fed into another derivation. Limit
  multi-term rows to 64 distinct terms; within that cap, repeated isolation
  costs at most quadratic work per row. Report skipped wide rows and
  unsupported extended arithmetic. Cooperative cancellation is polled during
  conjunction traversal, supported-expression checks, and row/target iteration.
  Polynomial normalization and final aggregation do not poll. The row cap
  applies after normalization, so it limits isolation work, not parsing,
  normalization, or wall time. An external deadline is still needed for a
  wall-time bound on a diagnostic batch.
- All premises must belong to the current assertion scope. Rebuild sessions
  recompute per query; assumptions sessions reject this diagnostic rather
  than silently omit or retain stale counts. Quantified queries skip it.
- Test positive and negative coefficients and endpoints, integer rounding,
  strict Real bounds, order and duplicate independence, missing premises,
  guarded constraints, scopes, unsupported terms, the width cap and
  cancellation. Check that enabling detection leaves solving unchanged.

For example, `3*x+4*y=13`, `x>=0`, `y>=0` gives
`3*x=13-4*y<=13`, hence `x<=13/3` and integer `x<=4`.
Similarly `4*y=13-3*x<=13`, hence `y<=13/4` and integer `y<=3`.
Deriving lower bounds in this way would require an upper bound on the other
variable, which the explicit input does not supply. The two new upper bounds
are counted but never reused as premises. If instead `y>=-2`, the first
calculation gives `x<=floor((13-4*(-2))/3)=7`; negative endpoints use the same
rule. Negative coefficients change which endpoint is required and may reverse
the resulting inequality.

The {rootboundsource}`implementation and local Bounds/Constraint owners
<src/smt/root_bound_diagnostics.cpp>` contain the calculation;
{rootboundsource}`the session call site <src/smt/solver_session.cpp>` schedules it
before rewriting and after the quantified-input routing gate.
{rootboundsource}`Assumptions-session validation <src/smt/smt_session.cpp>` rejects
the flag, and {rootboundsource}`focused tests
<tests/unit/root_bound_diagnostics_tests.cpp>` exercise the listed boundaries.

The output is an opportunity count relative to explicit input bounds, not a
claim that the solver would otherwise miss the consequence or benefit from
installing it. It excludes Boolean root propagation, combinations of different
rows, fixed-point refinement, and any bounds exposed only after lowering or
rewriting. There is no new proof or termination claim.

## Measured opportunities on the existing selections

The detection-only SLURM scan uses the same content-pinned 200 QF_LIA and
300 QF_UFLIA inputs as the earlier hybrid comparison, without the repeated
Certora diagnostics. Its [summary and provenance](../../benchmarks/root-bound-diagnostics-20261113/summary.json)
and [per-input counters](../../benchmarks/root-bound-diagnostics-20261113/rows.jsonl)
describe qualified source snapshot `6032c0f1` and its exact binary hash.
The implementation commit is `084ba5b2`; source links target that commit.
The [qualification record](../../benchmarks/root-bound-diagnostics-20261113/qualification.json)
pins source identities, gate commands, seeds, scheduler jobs and limitations.
The final helper adds direct includes and an ownership comment;
[identical preprocessed C++](../../benchmarks/root-bound-diagnostics-20261113/header-equivalence.json)
establishes that this cleanup does not change the qualified calculation.

| Selection | Completed scans | Inputs with improved bounds | Distinct stronger endpoints | Newly finite endpoints | Median detector time |
| --- | ---: | ---: | ---: | ---: | ---: |
| QF_LIA | 188/200 | 61/188 | 4,618 | 1,935 | 298.5 µs |
| QF_UFLIA | 288/300 | 156/288 | 11,468 | 11,468 | 87 µs |

Each invocation uses `--quiet --arithmetic-bound-diagnostics --preprocess-only
--stats`, one requested CPU, a 10 GiB allocation, and a five-second subprocess
deadline with two seconds of termination grace. Twelve inputs in each logic
time out; their counters are excluded from every table total, even when a
partial output exists. No SAT search runs. Two completed LIA scans are refuted
by the existing preprocessing passes; the other 474 completed scans answer
`unknown` as this diagnostic mode requires. None produces a SAT model, so this
scan has no SAT rows needing separate model validation.

Detector time excludes parsing, frontend translation, and subsequent
preprocessing. These are selected-cohort opportunities, not full-corpus
coverage, new solver solves, or a performance comparison. Among completed
scans, 51 LIA rows exceed the width cap; 174 LIA and 6,091 UFLIA arithmetic
atoms contain unsupported extended structure. Facts hidden under Boolean
guards and any opportunities requiring repeated inference remain outside the
count. Bounds already obtained later by the solver can also appear here.

All four arithmetic logics have a fresh 1,000-case differential gate with the
flag enabled. There are 3,986 passes and 14 native LIA timeouts; twelve of
those references answer SAT and two also time out. There are no crashes,
parser/type rejections, wrong answers, or reference disagreements. The LRA
gate independently validates all its 1,000 SAT models with Dolmen. The full
`make check` passes 48 CTest groups and 336 Python tests; `make asan-test`
passes all 48 groups. These gates establish diagnostic correctness coverage,
not a benefit from installing any bound.

Whole-tree clang-tidy and include-cleaner scans cover 137 non-generated C++
translation units with no tool or compilation errors. Advisory warnings remain:
the new helper's captured structured-binding warning was independently checked
as a false positive because its owned polynomial map is never mutated during
the capture's lifetime. The three direct-header advisories in that helper were
fixed and its include-cleaner rescan is clear. A small string-allocation warning
in the bounded 65-term test fixture is retained; it is outside solver execution.
These scans are not a claim of a warning-free project.
