# Statistics instrumentation and build profiles

**Status: Open lead. Owner: unassigned.**

Typed counter IDs and the string-key boundary policy are implemented; see
[statistics](../reference/statistics.md#typed-instrumentation-policy). This lead concerns the still-unimplemented
build-time diagnostic switch, separation of control state and diagnostic work,
and their overhead measurements. The inspection began before the typed migration;
the statistics row describes current storage. The other rows identify contracts
to recheck, not an outstanding migration of all counters to typed IDs.

Primo has counters and timing statements throughout solver code. Investigate
whether making diagnostic instrumentation optional at compile time improves
readability and performance for release, competition, and library builds.

## What the code currently does

| Mechanism | Concrete cost or contract to audit |
| --- | --- |
| {download}`Statistics <../../src/util/statistics.hpp>` and its {download}`reporting/dynamic updates <../../src/util/statistics.cpp>` | Enum keys directly update value and presence arrays. String keys perform hash lookups; new dynamic keys allocate map storage. Fixed keys print in registry order, followed by sorted dynamic names. |
| {source}`EUF merge/use-list counters <src/theory/equality/incremental_congruence_closure.cpp#L233-L280>` | Plain field increments occur inside solver operations. These are cheaper than string-key updates, but their total cost and effect on generated code need measurement. |
| {source}`ScopedStatisticsTimer <src/smt/solver_engines.cpp#L376-L401>` and {source}`CombinationTimer <src/smt/component_solver.cpp#L26-L41>` | Clock reads and publication of elapsed time; removing only the final increment would leave timing work behind. |
| {source}`Ackermann cost diagnostics <src/smt/solver_engines.cpp#L404-L483>` | A dedicated traversal allocates scratch storage and groups applications to compute counters. A useful disabled mode must omit diagnostic computation, not just its output. |
| {source}`CLI statistics printing <src/main.cpp#L87-L119>` | `--stats` controls printing. It does not disable the instrumentation that produced the result. |
| {source}`SolveTerminator::terminate <src/smt/solver_runtime.cpp#L12-L27>` | Reads `StatKey::TheoryConflicts` to enforce a resource limit. This count is operational state and must survive disabling diagnostics. |
| {source}`EUF scheduling <src/smt/solver_engines.cpp#L2243-L2251>` | Reads `EufPropagationRounds` to decide whether to run propagation. This is solver-control state stored alongside diagnostics. |

The conflict budget also controls when the
{source}`QF_UF session portfolio <src/smt/session_portfolio.cpp#L19-L48>` falls
back to another configuration. Replacing every statistics operation with a
no-op would therefore change solver behavior. Audit readers of local `stats_`
structures as well as `util::Statistics`; this table is an initial sample,
not a completed whole-tree classification.

## Proposed boundary

See also the concrete {ref}`quantified-UF counter-publication gap <quantified-uf-counter-publication>`
below: availability must be distinguished from a reported zero.

**Design preference:** avoid string-key lookups on solver execution paths.
Use typed counter IDs or component-local fields, resolving names when reporting.
String-key diagnostics are acceptable only behind a build-time boundary that
provably removes them from production builds. Do not rely on disabling
printing or assumed compiler optimization.

Separate always-on **solver-control state** from optional **diagnostic
instrumentation**. Resource budgets, cancellation/deadline clocks, adaptive
heuristic inputs, and any counter required for correctness belong to the
first category even when they are also reported as statistics. Optional
reports may copy those values; they must not own their operational meaning.

Use an explicit build setting, provisionally `PRIMO_ENABLE_DIAGNOSTIC_STATS`,
rather than deriving instrumentation from `NDEBUG` or `CMAKE_BUILD_TYPE`.
Optimized builds with diagnostics are needed for development and experiments;
optimized builds without them are candidates for deployment. Competition and
library profiles should select the setting explicitly once both modes are
qualified. This name and these profiles are proposals, not supported options.

A narrow macro can remove a diagnostic statement entirely, including its
argument computation. A no-op function alone does not guarantee this:
`stats.record(expensive_diagnostic())` still evaluates the argument unless the
compiler can prove it removable. A compile-time conditional or lazy callable
can also serve the purpose. Keep the public operations few and typed, with
consistent helpers for increment, accumulation, and scoped timing. Use typed
IDs for timer publication too, rather than moving the same string lookup into
a helper. Avoid scattered preprocessor branches throughout algorithm bodies. Move substantial
diagnostic traversals into named helpers so the main algorithm remains clear.

Whichever mechanism is chosen, disabled instrumentation must skip all work
done solely for statistics: argument construction, clock reads, scratch
allocation, traversals, and aggregation. Diagnostic arguments must be free of
solver side effects, and enabled operations must evaluate them exactly once.
Keep invariant checks and solver mutations outside these wrappers. Preserve
the existing backend boundary: this switch would govern Primo instrumentation;
CaDiCaL's own counters require a separate dependency-policy audit.

For the [library API](library-api.md), keep the public type/layout contract
consistent between the library and consumer. Prefer hiding diagnostic storage
behind an implementation boundary; an internal build macro must not silently
give callers a different public class layout. Report build capabilities and
unavailable counters explicitly, rather than returning plausible zero counts
for measurements that were never collected.

## Investigation and acceptance

1. Classify every counter/timer and its readers as operational or diagnostic.
   Start with hot-loop updates, string-key publication, scoped timers, and
   diagnostic-only passes. Preserve algorithm-specific counters in the
   instrumented profile for paper-to-code and performance evidence.
2. Prototype the boundary in one representative component. Compare readability
   and inspect optimized code to confirm disabled diagnostic computations are
   absent. Exercise enabled and disabled modes, including non-evaluation of
   diagnostic arguments and exact-once evaluation when enabled.
3. Build and test both profiles in CI. Verify identical logical results and
   valid models, incremental behavior, conflict-budget exhaustion, portfolio
   fallback, and cancellation contracts. Apply the repository's solver fuzz
   and model-validation gates to any implementation; inspect memory-sensitive
   layout changes with ASan. Wall-clock timeout outcomes can differ with speed.
4. Measure matching optimized builds from the same source revision/compiler
   and backend on representative UF, arithmetic, mixed, and incremental
   workloads. Repeat runs; record time/PAR2, memory, binary size, and profile
   metadata. Use external timing for comparisons so stripped measurements do
   not depend on the instrumentation under test. Do not infer overhead from
   the number of statistics statements alone.
5. Record the instrumentation setting in benchmark provenance and build/version
   output. Document counter availability and update report readers/tests that
   require diagnostics. Choose release/competition/library defaults only after
   correctness and performance evidence; keep a reproducible instrumented build.

The proposal is based on Primo's instrumentation and its consumers identified
above. Measurements must establish whether their overhead warrants a change.

(quantified-uf-counter-publication)=
## Quantified UF can leave EUF counters unpublished

The {ref}`October 27 paired measurement <uf-rollback-resource-measurement>`
reports zero `:euf.incremental.terms` and `:euf.incremental.backtracks` on two
successful quantified-UF UNSAT cases. These zeros cannot establish that no EUF
work occurred. The {diagnosticsource}`quantified solver recipe <src/smt/quantifiers/quantified_solver.cpp#L134-L154>`
uses online EUF. Its {diagnosticsource}`incremental closure <src/smt/solver_engines.cpp#L1673-L1680>`
buffers statistics with `flush_live_stats=false`. In
{diagnosticsource}`EufTheoryComponent::check <src/smt/solver_engines.cpp#L9977-L10000>`,
partial checks return before `flush_incremental_stats`; only full checks flush.
The {diagnosticsource}`shared solve loop <src/smt/component_solver.cpp#L379-L398>`
can return UNSAT directly after the SAT backend, before a full theory check.
{diagnosticsource}`Registering new atoms <src/smt/solver_engines.cpp#L9931-L9959>`
also resets the online execution object, so buffered statistics need a
publication policy across both reset and final exit.

Audit all exit and replacement paths and add a flush contract before relying on
these counters for quantified search comparisons. A regression should exercise
an UNSAT result reached before a full model check and a lemma-growth reset.
Keep this separate from collecting peak resident node counts or allocated bytes:
the existing term counter is cumulative. This is an observed reporting gap and
an implementation follow-up, not a solver fix included in the measurement.
