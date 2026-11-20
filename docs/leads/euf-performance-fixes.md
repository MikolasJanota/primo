# EUF performance and demand-atom implementation checklist

**Document purpose: completed implementation and evaluation checklist.** Current behavior belongs in [EUF](../implementation/euf.md); remaining generation/retention questions belong in [open research](theory-lemmas.md).

Status: integrated repaired source `6e53a710` passes native/ASan, fresh generated and real-seed correctness gates. The audited paired evaluation shows a modest targeted UF gain for the mechanical fixes, nearly neutral arithmetic controls and demand-atom regressions. Additional paired repetitions confirm the demand regressions; defaults stay unchanged. See the [qualification evidence](../evidence/euf-performance-fixes.md).
Baseline: `690ddc7f`; qualified pre-change native `13230b6d`.

## Basis and obligations

The existing flat closure follows Nieuwenhuis and Oliveras (2007), §§3–5
{cite:p}`nieuwenhuis2007`; see [EUF implementation](../implementation/euf.md).
The mechanical changes below preserve its representative, signature, causal
explanation and rollback invariants. The empty-set union fast path and moving
undo buffers are Primo engineering changes, not new paper algorithms.
Conflict-selected clauses follow Dutertre and de Moura (2006), §2 p.3
{cite:p}`dutertre2006yices`; the primary
paper was read for the [prototype](conflict-ackermann-prototype.md). Fresh atoms
name equalities, without asserting them. No other solver source is used.

1. Preserve the immutable baseline and measure milestones separately.
2. Enforce typed/numeric statistics in recurring solver operations. Keep printed
   names and meanings stable; explicit versioning is required for changed policy
   counters. Audit helpers as well as direct statistics calls.
3. Skip empty-losing-list disequality union and undo payloads. Move losing use
   buffers into owning undo storage. Iterate saved payload only while no helper
   can grow its outer container; current helpers enqueue but never recurse into
   merging. No borrowed term/arena storage crosses allocation.
4. Reuse owning conflict and semantic scratch. Preserve nested checkpoints while
   inconsistent, disabled tracing and all supported explanation modes.
5. Separate bounded negative caching from admitted pairs; filter and deduplicate
   complete semantic witnesses before the 64-witness budget. Keep limits explicit.
6. Add demand equality requests containing owning semantic IDs. Finish callback
   protocols and ordinary conflict delivery before safe-boundary registration.
   Reuse clausification, SAT observation, arithmetic preparation and component
   rebuild machinery; do not relax Nelson–Oppen interface membership checks.
7. Demand mode remains opt-in; default existing mode, 4096 new atoms/query,
   zero unlimited. Budget exhaustion skips optional clauses, never asserts an
   equality or establishes UNSAT. Query-owned requests and counts survive rebuild.
8. Verify both polarities, n-ary disjunction, scope retirement, repeated requests,
   model invalidation, cancellation, allocation boundaries and resource limits.
9. Run helper/SLURM builds, native/ASan checks, fresh differential and real-seed
   gates, paired frozen comparisons and separate all-SAT Dolmen validation.
   Publish no new performance claim before qualification.

## Ownership and delivery

`FlatCongruenceClosure` owns live and undo vectors; `FlatTermIndex` owns semantic
flat-node metadata. `Statistics` owns published counters. SMT owns all SAT atom
registration, request scheduling and copied clauses. The selector owns admitted
pairs and queued SAT clauses for one backend, surviving execution rebuilds;
a fresh backend requires a fresh SAT-literal index. Semantic requests and
budgets are query-owned across complete-recipe fallback. No callback creates terms
or SAT variables. Pending requests must settle by registration, reuse, clause
installation or an explicit skip, never by an endless retry.

Mechanical fixes, selection changes and demand atoms are separate milestones.
Demand requests with Boolean results or differing Boolean arguments,
optimized proof tracing, assumptions sessions and
any default promotion remain outside the demand milestone. Implementation docs,
run-log provenance, independent documentation review and verified commits are
required before completion. Integrated correctness qualification is recorded below; performance qualification remains separate.

The intended `AckermannGrowthState` is query-owned in the SMT driver and keeps
semantic requests, settled-request deduplication and generated-atom consumption
outside propagator lifetimes. Standalone UF drains `AckermannGrowthState`'s query-owned request
queue after the backend has unwound; the mixed component route hands requests
to the recipe's existing growth boundary. Existing atoms cost zero budget;
rebuilding a component preserves query consumption, while a new query resets it.

The {eufgrowthsource}`standalone registration callers <src/smt/solver_engines.cpp>`,
{eufgrowthsource}`component recipe driver <src/smt/component_solver.cpp>` and
{eufgrowthsource}`AckermannGrowthState::drain <src/smt/conflict_ackermann.cpp>`
own this integration. The component driver delegates shared-pair naming to its
existing interface materialization path only when both terms are eligible;
other equalities use ordinary clausifier registration. Query-owned growth hooks
in `CombinationRecipe` carry requests across execution rebuilds. Integrated
correctness qualification passes on `6e53a710`; the linked current source also
contains the separately qualified symmetry repair.


## Mechanical milestone qualified for correctness

Empty losing disequality lists bypass union/snapshot work. Losing use-list buffers
move into `flat_vector_trail_`; queued congruence work cannot grow that payload
arena until iteration returns. Trace resets and checkpoint restoration preserve
live capacity, and semantic extraction reuses an owning scratch vector. Native semantic/rollback/capacity regressions pass on SLURM, including ASan. The fresh nine-profile gate audited 9000 generated and 2855 mutated inputs: 8980 and 2843 pass, respectively, with 20 timeouts and 12 incomplete mutations. No wrong answer or crash was observed; 8617 successful SAT models passed separate Dolmen validation. Source `66ebec78`, build 414784, check/ASan 414785, gates 414787/414796/414797.

The first statistics migration registered fixed names in runtime implementation
files. It used an enum-or-string extra-order vector to preserve their prior
public display position without constructing strings on typed updates. The
current [simplified representation](../reference/statistics.md#typed-instrumentation-policy)
uses registry order and sorted dynamic names. The fixed arrays grow with the key
registry; closure checkpoints contain `IncrementalCongruenceClosureStats`, not
`Statistics`, so that change does not multiply the array growth by SAT depth.
The integrated migration uses explicit cold mutation APIs and typed timer/group keys; its qualification on `6e53a710` is separate from this mechanical snapshot.

Current mechanical owners are the
{download}`closure implementation <../../src/theory/equality/flat_congruence_closure.cpp>`
and {download}`assignment-state scratch <../../src/smt/solver_engines.cpp>`.
The {download}`Statistics representation <../../src/util/statistics.hpp>` and
{download}`implementation <../../src/util/statistics.cpp>` own typed storage
and deterministic print ordering. The
{download}`inventory checker <../../scripts/check_statistics_policy.py>` checks
call expressions and counts against its manifest; manual helper/coldness review
remains necessary. Cold summaries carry phase-specific manifest rationales. These downloads describe current source, not the older
prototype's source pin.


## Revised admission and demand milestone

The [EUF representation](../implementation/euf.md#conflict-selected-ackermann-prototype)
and [counter reference](../reference/statistics.md#conflict-selected-ackermann-prototype)
describe the current V2 selection, query-owned demand queue and internal-yield
protocol. The implementation is original engineering around the cited congruence
clause; caps, negative-cache FIFO, fixed witness table, proportional budgets of
other components and scheduling choices are not claims about Yices internals.
Demand requests with Boolean results or differing Boolean arguments, and
optimized proof-forest tracing, are unsupported. Existing-atom clauses retain
the older Boolean coverage; this is an explicit limitation of demand naming.
Every newly registered atom must have exactly one component owner; shared Int
pairs require the existing interface bridges. Arbitrary persistent tableau row
growth remains prohibited. A full preparation rebuild is allowed when needed.


## Mixed model-fact invariant found during qualification

The `abb4d19c` mutation campaign exposed a mismatch between online purity
filtering and complete component model export. Fresh optional equalities can
appear only positively, so the online filter could omit a false assignment
while `EufTheoryComponent::export_model` later retained that disequality. The
full checker already uses all assignments, but complete-arrangement online
checks reuse the execution closure. The fix disables filtering in this mixed
component's owned `online_options_`, matching the full-check/export contract.
Standalone UF retains its filtered-fact reconstruction. Three failing real-seed
model replays pass direct Dolmen when filtering is disabled (SLURM 414934);
the repaired source passes the integrated gate: 21,000 generated checks, 6,826 mutations, 318 regression measurements and 19,125 separate SAT-model validations. Two generated resource limits, 12 generated timeouts and 31 mutation timeouts remain unsolved, with no unresolved correctness failure.
