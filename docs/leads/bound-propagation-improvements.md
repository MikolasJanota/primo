# Bound propagation improvements: implementation checklist

Baseline: `ff303826`. Notification and integer-rounding repairs and scan-local
endpoint reuse are implemented. The later preprocessing-placement trial was
rejected and its source changes reverted. Defaults, private branch limits and
UF fallback remain unchanged.

The exact candidate `07ec5ce5` has
{boundrepairfinalevidence}`fresh correctness qualification <benchmarks/bound-repairs-20261114/final-qualification.json>`:
49 native test groups, 354 Python tests and 7,069 direct Dolmen model passes for
selected SAT results from 10,000 generated inputs. Those inputs produced 9,980
reference matches, 18 timeouts, one diagnosed private-budget unknown and one
reference timeout. The resource outcomes remain separate from matches. Canonical
UF/arithmetic tests, tracked regression sessions and real-seed mutations also
have retained receipts; ASan and static evidence explicitly use identical C++
source from the earlier cache snapshot.

The full
{boundrepaircomparison}`paired comparison <benchmarks/bound-repairs-20261114/final-protocol.json>`
is complete: 697 instances, four profiles, two binaries and three repetitions,
with all 9,038 successful SAT timing rows independently validated. The
[qualified results](../evidence/bound-repairs.md) are mixed: row mode loses one
median solved instance, combined mode keeps the same count, and aggregate PAR2
changes are small. Follow-up runs reproduce the regressions and isolate a
repeatable cache benefit. Both row modes remain opt-in; no default promotion.

## Basis and obligations

Dutertre and de Moura, *A Fast Linear-Arithmetic Solver for DPLL(T)*,
[CAV 2006, §4.5](https://leodemoura.github.io/files/cav06.pdf), describes
explained refinement of tableau bounds against existing atoms. Section 5
supplies the ordered infinitesimal representation. The section was read before
implementation. Integer rounding is the elementary implication that an integer
bounded above/below by a rational endpoint is bounded by its floor/ceiling;
Primo's existing delta-aware integer-value helpers implement that extension.
This is ordinary interval refinement, not exhaustive closure or a new complete
integer procedure.

- {boundrepairsource}`OnlineLraPropagator <src/smt/solver_engines.cpp#L7116>`
  and {boundrepairsource}`ComponentLraExecution <src/smt/solver_engines.cpp#L8897>`
  own their tableau and pending consequences. Each `PendingPropagation` owns
  its `reasons` vector. The
  {boundrepairsource}`preparation policy <src/smt/solver_engines.cpp#L3488>`
  establishes integer-valued targets and chooses `IntegerBoundRounding::All`.
- Bound notifications on a basic variable must scan its own row in reverse;
  nonbasic notifications inspect the occurrence index. This notification
  schedule is a Primo adaptation of explained refinement. Reverse scans must not
  depend on a registered atom for the row's basic variable.
- Keep forward and reverse size/fanout limits. This milestone does not promise
  inference completeness across basis changes or compute numeric closure.
- Before comparing an inferred endpoint with an atom, use delta floor for an
  upper endpoint and delta ceiling for a lower endpoint only under `All`
  integer rounding, via the shared
  {boundrepairsource}`endpoint comparison <src/smt/solver_engines.cpp#L7091>`.
  Keep premises unchanged: integrality is a background fact.
- Pending consequences own reasons; no borrowed term/row/atom reference may
  cross a mutation of its owner. No ordinary bound introduces a tableau row.
- Later, evaluate moving the single preprocessing worklist after existing
  value/equality simplification, before finite-domain processing and final
  arithmetic normalization. Revert the stage if qualification rejects it.
- Finally, reuse upper/lower calculations within a single scan only. No
  persistent cache, backtracking invalidation scheme or numeric-no-op skip.
- Add mechanism counters and focused assertions; compare exact options and
  identities on SLURM. Stop performance work on a correctness failure.

## Verification and delivery

First qualify notification/rounding repairs, then independently evaluate
preprocessing placement and scan-local reuse. Each accepted stage needs source,
tests, synchronized implementation documentation and a verified commit.
All solver batches/build qualification run in SLURM through the preparation
helper. Required gates include check, ASan, lint, static review, four-logic
reference/model tests and seed mutations. Final evaluation uses the frozen
697-case selection, four profiles, three rotated pairs, 120 seconds and the
existing memory limits, with separate complete Dolmen SAT coverage.
Defaults, private branch budgets and UF fallback policy remain unchanged.

For example, `s = 2u + v`, `s <= 5` and `v >= 0` imply `u <= 5/2`,
hence `u <= 2` for integer `u`. With bidirectional row propagation enabled,
asserting the basic bound last must also trigger its reverse scan, subject to
the configured limits. The
{boundrepairsource}`focused regression <tests/unit/qf_lra_solver_tests.cpp#L3537>`
checks assertion ordering and both integer bound directions.


(scan-local-reuse-draft)=
## Scan-local reuse

The private
{rowcachetrialsource}`RowBoundCache <src/smt/solver_engines.cpp#L7082>`
owns a local comparison count, a computed/not-computed flag, an optional rational-plus-
infinitesimal endpoint, an optional integer-rounded endpoint and an optional
reason vector. A two-element `RowBoundCaches` array belongs to one row or child
scan, indexed by bound direction. A computed missing endpoint is cached too;
it must not be confused with an unattempted calculation.

Both arithmetic execution paths read the tableau and only enqueue pending
literals during a scan. They neither assert those literals synchronously nor
pivot or register atoms before returning. This invariant makes the local
cache valid without generation numbers. No cache survives a notification,
backtrack, feasibility repair, private completion or registry growth.

The
{rowcachetrialsource}`numeric and explanation helpers <src/smt/solver_engines.cpp#L7013>`
no longer allocate reason vectors during numeric calculation. After an endpoint first
entails an atom, separate explanation helpers collect the same premise literals
in the original order from the unchanged tableau. Each queued consequence gets
an owning copy; the cache's reason vector is retained for other candidate atoms.
A missing premise during explanation collection diagnoses an invariant failure.

For a row of width `r` with `a` candidate atoms, forward endpoint arithmetic
costs `O(r+a)` per scan instead of `O(r*a)`. Reverse arithmetic remains quadratic
in row width when all children are inspected, plus candidate comparisons;
this is not the optimized exclusion-sum worklist used by preprocessing. Lazy
explanations add one linear witness-collection pass per entailed direction.
Each pending consequence still copies its explaining vector in linear time
in the number of premises; total witness-copying cost can remain `O(r*a)`.
These complexity bounds count arithmetic operations, not arbitrary-precision
rational bit costs. This is an engineering optimization of existing CAV2006-style interval
refinement, not a new paper algorithm or stronger closure.

Work counters are published once per row/child group to avoid a statistics
hash lookup per candidate. They distinguish attempted endpoint calculations (including missing
or capped directions), candidate-polarity comparisons and constructed explanation
vectors. The
{rowcachetrialsource}`regression with many thresholds <tests/unit/qf_lra_solver_tests.cpp#L3586>`
for one row tests reuse, including an
actual UF interaction. Existing conflict, sign, scope and allocation-growth
tests and fresh four-logic gates remain required before acceptance.

## Final comparison protocol

The {boundrepairpairsource}`configuration helper <scripts/qualification/bound_repairs/configure.py>`
creates three private
configurations from the frozen 697-input selection and its four existing
profiles. Each profile has an adjacent baseline/candidate pair. The timing
{boundrepairpairsource}`runner's optional paired_groups <scripts/qualification/lia_hybrid/run.py#L40>` rotates the four pairs by instance index plus
repetition and reverses their internal order on alternating sums. Each native
measurement starts a fresh solver process; the per-allocation provenance records
the actual order, executable hashes, arguments, input identity and host. The
runner verifies executable and input hashes; Git labels are supplied metadata
and require the separate build/preflight and source-equivalence evidence.
Configurations without `paired_groups` retain the runner's earlier rotation.

The {boundrepairpairsource}`submission controller <scripts/qualification/bound_propagation/submit.py>`
accepts `--job-time` for the SLURM allocation duration.
The final comparison requests 20 minutes for eight timing measurements per
allocation and 45 minutes for their separate model replays. This does not change
the native timing deadline of 120 seconds or the 8 GiB address-space limit. Three timing
arrays run sequentially, with verified whole-array dependencies; model arrays
are submitted with `--after` naming the last timing-array ID, so they start only
after all timing arrays finish. Replay allows 180 seconds for the native solver
and 60 seconds for Dolmen. Every successful native SAT timing row
requires its own exact-option Dolmen replay. The [completed comparison](../evidence/bound-repairs.md) retains full timing,
model and follow-up receipts; the selected pilot remains a separate diagnostic.


## Redundancy audit (2026-11-15)

This follow-up first measures repeated work without changing propagation order,
bound insertion, reasons or search decisions. The baseline is `abe24b92`, whose
native source is the qualified `07ec5ce5` implementation above.

Dutertre and de Moura, CAV 2006 §§4.3–4.5, distinguish a non-strengthening
assertion from a changed endpoint and warn that row refinement may derive weaker
bounds. Schulte and Stuckey, *Efficient Constraint Propagation Engines*,
[author preprint, §§4–6](https://arxiv.org/pdf/cs/0611009), study fixpoint tracking,
relevant events and scheduling. Those finite-domain algorithms are motivation
for the audit, not implemented simplex scheduling. In particular, their domain
fixpoint conditions cannot simply be assumed across a Primo pivot or backtrack.

The audit checklist is:

- In both `OnlineLraPropagator` and `ComponentLraExecution`, count arithmetic
  notifications as conflicting, tightening at least one endpoint, or numerical
  no-ops. Snapshot `LraTableau::push()` before insertion: it is a read-only trail
  offset, and every successful endpoint change appends a bound trail entry.
  A nonconflicting unchanged offset therefore identifies a no-op without
  copying arbitrary-precision endpoints. No checkpoint is installed or popped.
- Track pending multiplicity separately for each signed SAT literal. An owning
  vector of two counters per encountered SAT-variable index belongs to the
  execution object. Queue insertion/pop/backtrack removal update it; it never
  stores terms, tableau references or reasons. Opposite polarities are distinct.
  Counts survive neither removal nor object destruction. Growth acquires the
  counter only after resize. Insertion is constant-time once the index is
  covered; total vector-growth cost and retained space are linear in the
  largest encountered SAT-variable index, including gaps.
- Audit the SAT-literal `pending_` queue only. Interface-pair publications in
  `pending_interface_equalities_` are excluded, including backtrack discards.
  Existing-equality candidates are included where `OnlineLraPropagator` queues
  them as SAT literals. Count candidates, duplicate insertions, first currently pending insertions,
  assigned-at-pop discards and backtrack discards. Keep duplicates and their
  original reason/order in this measurement stage. A first pending insertion
  is not necessarily a newly learned fact or an eventually delivered literal.
- Count row-scan notification events, including capped/empty attempts, and how
  many enqueue at least one literal not already pending. Count events caused by
  numerical no-ops separately. This measures useful queue additions, not SAT
  search benefit, arithmetic closure or CPU share.
- Exercise QF_LRA, QF_UFLRA, QF_LIA and QF_UFLIA for the counter partitions
  and unchanged-bound events;
  qualify in SLURM, compare instrumented/control prefixes and complete selected
  cases, and validate every successful SAT timing row independently. Report
  instrumentation overhead and incomplete resource outcomes separately.
- Only after these measurements consider queue deduplication and scan filtering.
  Such a change must retain signed conflicts/reasons and account for separate
  basis, registry and backtrack events. No blanket early return from assignment
  notification is authorized by the numerical no-op counter alone.

The {redundancyauditsource}`audit implementation <src/smt/solver_engines.cpp>`
contains `PropagationQueueAudit` and both execution adapters. The
{redundancyauditsource}`fixed counter keys <src/util/statistics.hpp>` and
{redundancyauditsource}`printed names <src/util/statistics.cpp>` define the
[statistics reference](../reference/statistics.md#arithmetic-redundancy-audit).
This qualification snapshot precedes the final reviewed documentation.

The owning tableau's
{redundancyauditsource}`trail checkpoint and bound operations <src/theory/arithmetic/lra_tableau.hpp>`
and {redundancyauditsource}`bound insertion <src/theory/arithmetic/lra_tableau.cpp>`
provide the no-op classification contract. This audit implements no new
inference or fixpoint proof obligation: all state must mirror queue operations
without influencing them. Tests and the paired control check this preservation.

### Candidate experiment: pending-literal deduplication

The first diagnostic prefixes at 1,000 theory conflicts show approximately 89%
duplicate candidate insertions on the two Dartagnan LIA inputs identified in the
{download}`pinned observations <../../benchmarks/propagation-redundancy-20261115/prefix-observations.json>`.
These compare instrument-only `f8845def` with source-equivalent baseline
`07ec5ce5` in row and combined profiles; all eight retained outcomes are
resource-limited `unknown`, not completed solving results. This motivates an
experiment; it is not yet an accepted optimization or a CPU attribution. Keep
the instrument-only binary as a separate control.

The proposed subset of event scheduling is simple signed-literal queue
coalescing, motivated by Schulte–Stuckey §5 rather than their finite-domain
fixpoint algorithm. Before implementation, the obligations are:

- Enable coalescing only in the existing opt-in row mode; otherwise retain
  duplicate entries and their reason order. Retain the first pending occurrence
  of each signed SAT literal when coalescing. Suppress later
  occurrences of that same sign; never coalesce opposite polarities. Every
  retained candidate owns its original reasons. The previous queue would pop
  that first occurrence before all its duplicates, so retain its reason choice
  and the order of first occurrences.
- Preserve the existing candidate/unique/duplicate keys as actual insertion
  counts. Add explicit attempt and suppressed-duplicate keys for the experiment.
  In the audit control every attempt is inserted; in this experiment only
  first-pending attempts are inserted. Removal/backtrack counters still count
  actual queue entries; evidence must distinguish these denominators.
- Check for duplication before copying a reason vector into the queue. A row
  entailment helper may return a reference to its synchronous scan's owning
  cache; consume that borrow immediately in the enqueue helper. No reference
  may be stored in the pending queue or survive cache mutation/destruction.
  Tableaux, terms and caches cannot mutate during this enqueue operation.
- Popping removes the pending marker. Backtracking removes all remaining queue
  markers while preserving the existing handling of reasons for already
  delivered and retained literals. Repeated pushes/pops and registry growth
  must not suppress later legitimate consequences.
- The proof obligation is queue coalescing, not arithmetic redundancy: each
  suppressed occurrence has an earlier pending occurrence with a still-valid
  explanation. A backtrack clears both before another search state can use
  either. After the first delivery SAT assigns that literal before a repeated
  occurrence could be useful; subsequent bound insertions only strengthen its
  premises until backtracking clears the queue. Existing assignment notification
  and theory checking remain intact. Check literal/reason sequence preservation
  directly, rather than treating aggregate counters as its proof.
- Test both literal signs, branch conflicts, backtracking, integer rounding and
  UF interaction. Run fresh four-logic reference gates, ASan and source review.
  Compare identical search counters where both runs complete, plus paired
  native runtime, memory and reason/candidate work. Validate all successful SAT
  timing rows separately.
- Audit-vector work remains `O(N+V)` for `N` candidate/removal operations and
  largest encountered SAT index `V`; reason copying is paid only for retained
  entries. Numeric row calculations are unchanged. Suppressing a numeric no-op
  notification is a separate experiment: it needs proof that relevant bounds,
  basis, atom registry and pending state have not changed since a covering scan.

(repeated-state-row-scan-filter)=
### Candidate experiment: skip a scan with unchanged inputs

A second, deliberately conservative experiment may skip a row-scan event only
when insertion was a numeric no-op and the same arithmetic variable has already
been scanned with the identical global trail offset and cumulative pivot count
in the current backtrack generation. It is engineering memoization of the
existing row operation, motivated by relevant-event reasoning in Schulte–Stuckey
§5; it is not a domain-fixpoint algorithm or numeric closure.

Before implementation, the owning execution object must satisfy:

- Store only `(generation, trail offset, pivot count)` per encountered arithmetic
  variable, not endpoints, rows, terms or reasons. A stamp records that this
  variable's complete existing bounded scan was called at those inputs, including
  its fixed cap/empty outcomes. New objects have no matching stamps.
- Every successful bound change appends to the tableau trail. Every pivot,
  including fixed-variable elimination, advances the cumulative pivot count.
  A scan only enqueues, so its numeric inputs remain unchanged until it returns.
  Identical trail offsets between monotonic insertions imply no intervening
  bound change; matching pivots also preserve row structure and variable roles.
- Invalidate all stamps logically in constant time on every backtrack, even if
  the restored trail offset equals an older one. A generation counter supplies
  this invalidation; on unsigned wrap, clear all stamps before restarting it.
- Registry extension must not reuse stamps across a changed prepared atom set.
  Component registration destroys/recreates `ComponentLraExecution`; the legacy
  `OnlineLraPropagator` borrows a fixed prepared arithmetic-atom view whose
  external owners outlive it; that fixed LRA subset is distinct from the shared
  main registry, which can grow. A new execution begins with fresh stamps. Verify these ownership
  paths against `register_atoms`, `reset` and the legacy construction site.
- SAT assignments without arithmetic changes can remove eligible atoms, but
  cannot make an assigned atom unassigned without backtracking. Previously
  queued first occurrences are retained/delivered; backtracking clears pending
  candidates and invalidates stamps together. Preserve same-variable scans,
  interface/equality handling, deferred feasibility checks and conflicts.
- Count skipped repeated states separately; `row-scan-events` remains a request
  count including these skips. This does not authorize skipping all numeric
  no-ops: another variable's changed bound or a pivot changes the fingerprint.
- Test first visit, exact repeat, other variable, changed trail, changed pivot,
  backtrack to the same offset, fresh execution and vector growth. Integration
  must exercise a skipped weak-bound event, preserve delivered reasons/search
  counters on completed prefixes and pass fresh four-logic/model/ASan gates.
- Lookup is constant-time; total growth/space is linear in the largest encountered
  arithmetic-variable ID, including gaps. Generation invalidation is constant
  except at wrap. This cache does not track dependencies finely: an unrelated
  trailed change prevents a hit too. Cumulative counter wrap within a generation
  is outside the supported solver-resource envelope; no completeness claim is
  attached to this scheduling heuristic.

The {propfiltertrialsource}`trial memo <src/smt/arithmetic_row_scan_memo.hpp>`
implements the fingerprint and generation invalidation. Its
{propfiltertrialsource}`execution integration <src/smt/solver_engines.cpp>`
contains both adapters' `notify_assignment_lit`/`notify_backtrack_delta`, component
`register_atoms`/`reset`, and the fixed-view legacy construction sites. These
links identify the qualification trial, not an accepted performance result.

### Default-mode allocation guard

Before accepting the filters, bypass signed-ID tracking when the row option is
not enabled. The diagnostic control intentionally used `O(V)` tracking for all
queue entries; the accepted implementation must not add that allocation to the
default queue. A late arithmetic atom can have a large SAT index after many
Boolean atoms, even when few arithmetic literals are pending.

`ArithmeticPropagationQueue` must take an explicit coalescing policy. When it
is false, append the original candidate/reasons in order without indexing or
growing the marker vector; pop/clear must not visit markers either. Count these
actual insertions as `propagation-untracked-candidates`. Candidate attempts,
actual insertions and owned reason-literal counts remain available. Unique and
duplicate classification is unavailable for these entries, not measured zero;
analyses must not compute a duplicate fraction when untracked entries exist.
Thus actual insertions partition into classified unique, classified duplicate
and untracked entries. With the row option enabled, all entries are classified
and signed duplicates are suppressed before copying reasons.

Add a pure queue regression that retains duplicate order/reasons with tracking
disabled and accepts a large sparse literal ID without a dense allocation.
Keep the default-mode path proportional to queue payload, while tracking in the
opt-in row mode retains the stated `O(N+V)` bound. This is a guard on the audit's
own cost; it introduces no inference or SAT decision policy.

In the coalesced queue each signed literal has at most one pending occurrence,
so markers store Boolean presence rather than full multiplicity counters. The
unfiltered audit control keeps multiplicities; it is a separate measurement
implementation. The
{propfilterfinaltrialsource}`final trial queue <src/smt/arithmetic_propagation_queue.hpp>`
and {propfilterfinaltrialsource}`focused queue regression <tests/unit/qf_lra_solver_tests.cpp>`
show the disabled-mode allocation guard and preserved duplicate/reason order.
The {propfilterfinaltrialsource}`counter registry <src/util/statistics.cpp>`
defines the untracked category. These links identify a qualification trial;
source acceptance and broad performance qualification remain pending.
