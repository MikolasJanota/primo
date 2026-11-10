# Statistics reference


Use `--stats` to print numeric counters to stderr while leaving stdout as the
SMT result stream:

```bash
./build/primo --stats input.smt2
```

For a model-producing script, stdout contains the result line followed by the
SMT-LIB model response when the input requests it:

```{literalinclude} ../examples/model.smt2
:language: lisp
```

```{literalinclude} ../examples/model.txt
:language: text
```

Statistics are intentionally stable, colon-prefixed keys so benchmark scripts can
compare runs mechanically. Hot solver paths use an internal `StatKey` enum for
registered counters; the printed names below are the compatibility boundary for
scripts and result artifacts.

For the [shared SMT driver](../implementation/combination.md#the-shared-smt-driver),
also inspect `:combination.model.snapshots`,
`:combination.model.care-pairs`, `:combination.model.care-pair-agreements`,
`:combination.model.spanning-pair-checks`, `:combination.model.disagreements`,
`:combination.model.decisions`, and `:combination.model.complete-fallbacks`.
Agreeing care pairs still incur validation work; the linear selector scans
spanning edges only if no care disagreement wins. Compare these with
the production recipe's `:time.qf-uflia.model-reconciliation-us`,
`:time.qf-uflia.component-rebuild-us` and
`:time.qf-uflia.complete-fallback-us` timers, not just total time.
The dedicated QF_UFLRA `:no.*` counters belong to a different route.

Model costs are separate counters: `:time.model.reconstruction-us`,
`:time.model.check-us`, and `:time.model.serialization-us`. Original-assertion
checking in the rebuild engine requires a requested model and either debug mode
or `--check-model`. The assumptions engine also constructs a model when
`--check-model` is set and records checking and printing together in
`:time.model.check-and-print-us`. Dolmen replay is a separate process and is
not included in these internal counters.

## Shared integer route instrumentation

The {migrationsource}`QF_LIA recipe <src/smt/solver_engines.cpp#L10043-L10077>`
retains the historical `:solver.qf-lra` route key and its prepared
tableau shape counters. `:lra.theory-checks` counts its full arithmetic checks, while
`:lra.theory-conflicts` counts refutations returned at that boundary. Online
rational work keeps its existing arithmetic counters. Internal failed integer
branches do not increment the aggregate `:theory.conflicts` budget counter.
The shared driver counts each reported conflict once, including an empty
blocking clause.

Both integer recipes publish `:lia.patch-rounds-fully-integral` from the
patcher's existing remaining-fractional count when patching is enabled. The
shared-driver migration adds `:combination.*` diagnostics and
`:time.qf-lia.*` driver timers to standalone QF_LIA; the historical
`:euf.theory-conflicts` diagnostic may now include its driver-level learned
clauses. QF_UFLIA's `:time.qf-uflia.*` timers remain attached to its recipe and
are not emitted by standalone QF_LIA. Scheduling and diagnostic search counts
need not match the old standalone loop even when results and limits agree.

## Session counters

The session interpreter reports `:session.queries` (completed queries) and
`:session.frontend-builds` (frontend generations built). These are cumulative
within the current session state, not counts for a single query.

The assumptions engine additionally reports `:session.engine-builds`,
`:session.translated-assertions`, and `:session.incremental-assumptions`.
The first two count constructed engines and assertions translated into the
persistent engine; the last is a mode indicator. The indicators
`:session.safe-rewrite-pipeline`, `:session.pipeline.lower-extended-terms`, and
`:session.pipeline.lower-real-equalities` describe the selected incremental
preprocessing route. Preprocessing counters and time are drained after each
query, so they must not be interpreted as cumulative engine work.
See {source}`session accounting <src/smt/smt_session.cpp#L160-L193>` and
[session ownership](../implementation/sessions.md).

## General timing and preprocessing

For CLI queries, {source}`session accounting <src/smt/smt_session.cpp#L333-L349>`
adds parser/frontend work accumulated since the preceding query to the engine's
total. Parsing includes stream reads and waits for the client. This is not
process wall time; startup and harness overhead remain outside it. In rebuild
mode, {source}`solve timing <src/smt/solver_session.cpp#L784-L800>` also includes
final model checking and serialization, whose own timers are nested.

General counters include:

- `:time.parse-us`: SMT-LIB parser elapsed time in microseconds;
- `:time.frontend-us`: command validation, declarations, macro expansion, and
  core term construction time in microseconds;
- `:time.preprocessing-us`: shared preprocessing and pre-solve preparation time
  in microseconds;
- `:time.solve-us`: solver-strategy execution and final model handling time
  in rebuild mode, in microseconds;
- `:time.total-us`: query-engine time plus the session's accumulated
  parsing/frontend time, in microseconds;
- `:input.assertions`;
- `:preprocessing.ite.boolean`;
- `:preprocessing.ite.auxiliaries` (both renamed from `:frontend.ite.*` when
  `ite` became a term kind and lowering moved out of the frontend into the
  `lower-extended-terms` leaf; the meaning is unchanged, so older run-log
  entries remain comparable under the new names);
- `:preprocessing.lower.div-quotients`;
- `:preprocessing.lower.abs-rewrites`;
- `:frontend.define-fun.nullary-direct-uses`: direct references, including
  references from later definitions;
- `:frontend.define-fun.nullary-eager-compiles`: declaration-time compiles;
- `:frontend.define-fun.nullary-side-assertions`: auxiliary assertions stored
  with compiled definitions;
- `:frontend.define-fun.nullary-cache-hits`,
  `:frontend.define-fun.nullary-cache-misses`, and
  `:frontend.define-fun.nullary-cache-entries`: compatibility aliases for old
  result files; hits means direct uses, while misses and entries mean eager
  compiles;
- `:preprocessing.assertions`;
- `:preprocessing.passes`;
- `:preprocessing.changed-passes`;
- `:preprocessing.adaptive-passes`;
- `:preprocessing.term-size.initial`;
- `:preprocessing.term-size.final`;
- `:preprocessing.arithmetic-canonicalization.atoms-visited`;
- `:preprocessing.arithmetic-canonicalization.atoms-rewritten`;
- `:preprocessing.arithmetic-canonicalization.comparisons`;
- `:preprocessing.arithmetic-canonicalization.equalities`;
- `:preprocessing.arithmetic-canonicalization.constant-folds`;
- `:preprocessing.arithmetic-canonicalization.orientation-reversals`;
- `:preprocessing.arithmetic-canonicalization.aliases-merged`;
- `:preprocessing.arithmetic-canonicalization.duplicate-assertions-removed`;
- `:preprocessing.arithmetic-canonicalization.terms-created`;
- `:preprocessing.arithmetic-canonicalization.catchup-rewrites`;
- `:preprocessing.arithmetic-canonicalization.post-purification-rewrites`;

These arithmetic-canonicalization counters remain zero unless
`--arithmetic-atom-canonicalization` is enabled; pure QF_UF remains zero even
with the option enabled.

- `:preprocessing.boolean-flatten.attempts`;
- `:preprocessing.boolean-flatten.accepted`;
- `:preprocessing.boolean-flatten.threshold-stops`;
- `:preprocessing.boolean-flatten.operands-copied`;
- `:preprocessing.boolean-flatten.rewrites`;
- `:preprocessing.boolean-flatten.max-arity`;
- `:preprocessing.boolean-implies.visited`;
- `:preprocessing.boolean-implies.clause-candidates`;
- `:preprocessing.boolean-implies.rewritten`;
- `:preprocessing.boolean-implies.all-mode-rewrites`;
- `:preprocessing.boolean-implies.widened-operands`;
- `:preprocessing.boolean-implies.threshold-stops`;
- `:preprocessing.boolean-implies.terms-created`;
- `:preprocessing.eq-bridge.equalities`;
- `:preprocessing.eq-bridge.skipped-or-nodes`;
- `:preprocessing.boolean-units.facts`;
- `:preprocessing.boolean-units.rewrites`;
- `:preprocessing.forced-atoms.facts`;
- `:preprocessing.forced-atoms.disequalities`;
- `:preprocessing.forced-atoms.rewrites`;
- `:preprocessing.unit-equality.consequences`;
- `:preprocessing.unit-equality.substitution-units`;
- `:preprocessing.unit-equality.substitutions`;
- `:preprocessing.unit-equality.substitution-accepted`;
- `:preprocessing.unit-equality.substitution-rejected`;
- `:preprocessing.equality-query.nodes`;
- `:preprocessing.equality-query.memo-hits`;
- `:preprocessing.equality-query.add-term-requests`;
- `:preprocessing.equality-query.add-term-seen-hits`;
- `:preprocessing.solve-eqs.candidates`;
- `:preprocessing.solve-eqs.accepted`;
- `:preprocessing.solve-eqs.rewrites`;
- `:preprocessing.solve-eqs.rejected-cycles`;
- `:preprocessing.solve-eqs.rejected-blowups`;
- `:preprocessing.real-equality-lowering.lowered`;
- `:preprocessing.real-equality-lowering.guarded-distributed`;
- `:preprocessing.finite-domain.domains`;
- `:preprocessing.finite-domain.amo-clauses`;
- `:preprocessing.finite-domain.evaluated-equalities`;
- `:preprocessing.finite-domain.evaluation-cutoffs`;
- `:preprocessing.equality-definitions`;
- `:preprocessing.equality-definitions.candidate-pairs`;
- `:preprocessing.equality-definitions.accepted-pairs`;
- `:preprocessing.equality-definitions.predicted-clauses`;
- `:preprocessing.equality-definitions.rejected-domain-size`;
- `:preprocessing.equality-definitions.rejected-clause-limit`;
- `:preprocessing.ground-congruence-clauses`;
- `:preprocessing.ground-congruence-skipped-groups`;
- `:preprocessing.uf-symmetry.accepted-sets`;
- `:preprocessing.uf-symmetry.values`;
- `:preprocessing.uf-symmetry.terms`;
- `:preprocessing.uf-symmetry.constraints`;
- `:preprocessing.uf-symmetry.value-precedence-terms`;
- `:preprocessing.uf-symmetry.value-precedence-constraints`;
- `:preprocessing.uf-symmetry.rejected-noninvariant`;
- `:preprocessing.auto.single-assertion`;
- `:preprocessing.auto.strong`;
- `:preprocessing.auto.basic-single`;
- `:preprocessing.auto.basic-multi`;
- `:preprocessing.auto.large-assertion-skip`;
- `:preprocessing.auto.selector.structural`;
- `:preprocessing.auto.selector.shadow-evaluated`;
- `:preprocessing.auto.selector.legacy-disagreement`;
- `:preprocessing.auto.structural.conjunct-occurrences`;
- `:preprocessing.auto.structural.unique-conjuncts`;
- `:preprocessing.auto.structural.clause-like-conjuncts`;
- `:preprocessing.auto.structural.implication-clause-candidates`;
- `:preprocessing.auto.analysis.finite-domain-domains`;
- `:preprocessing.auto.analysis.finite-domain-choices`;
- `:preprocessing.auto.analysis.predicted-amo-clauses`;
- `:preprocessing.auto.profile.large-skip`;
- `:preprocessing.auto.profile.finite-domain-basic`;
- `:preprocessing.auto.profile.compact-clause-basic`;
- `:preprocessing.auto.profile.strong`;
- `:preprocessing.auto.limit.*`: the six effective structural selector limits;
- `:solver.boolean`;
- `:solver.qf-uf`;
- `:sat.phase.theory-negative`;
- `:sat.phase.auto-polarity`;
- `:sat.phase.auto-basic-single-predicate-polarity`;
- `:sat.phase.auto-basic-multi-polarity`;
- `:sat.vars`;
- `:sat.clauses`;
- `:sat.clause-lits`;
- `:sat.unit-clauses`;
- `:sat.binary-clauses`;
- `:sat.conflicts`;
- `:sat.decisions`;
- `:sat.propagations`;
- `:sat.solve-calls`;
- `:euf.theory-atoms`;
- `:euf.predicate-atoms`;
- `:euf.pure-filter.enabled`;
- `:euf.pure-atoms.positive`;
- `:euf.pure-atoms.negative`;
- `:euf.pure-literals.filtered`;
- `:euf.assignments`;
- `:euf.eq-assignments`;
- `:euf.diseq-assignments`;
- `:euf.checks`;
- `:euf.theory-conflicts`;
- `:euf.propagation.rounds`;
- `:euf.propagation.candidates`;
- `:euf.propagation.delivered`, which counts accepted propagations stored for
  the external propagator callback;
- `:euf.propagation.reason-lits`;
- `:euf.propagation.budget`;
- `:euf.propagation.delivery-budget`;
- `:euf.propagation.budget-cutoffs`;
- `:euf.propagation.hit-rate-cutoffs`;
- `:euf.propagation.auto-online`;
- `:euf.propagation.auto-conflict`;
- `:euf.propagation.auto-off`;

The diagnostic-only disequality-propagation opportunity counters below are
populated when `--euf-diseq-prop-diagnostics` is enabled. They are zero in
ordinary runs:

- `:euf.diseq-prop.assigned-app-disequalities`;
- `:euf.diseq-prop.source-unary-candidates`;
- `:euf.diseq-prop.source-unary-existing-arg-atom`;
- `:euf.diseq-prop.source-unary-arg-atom-true`;
- `:euf.diseq-prop.source-unary-arg-atom-false`;
- `:euf.diseq-prop.flat-same-function-candidates`;
- `:euf.diseq-prop.flat-same-function-existing-arg-atom`;
- `:euf.diseq-prop.flat-same-function-arg-atom-true`;
- `:euf.diseq-prop.flat-same-function-arg-atom-false`;
- `:euf.diseq-prop.guarded-function-guard-candidates`;
- `:euf.diseq-prop.guarded-argument-guard-candidates`;

The diagnostic-only Ackermannization cost counters below are populated when
`--euf-ackermann-diagnostics` is enabled. They count distinct reachable
post-preprocessing UF applications and estimate the SAT-level functional
consistency constraints using the cost model attributed to Bruttomesso et al.
in the {source}`Ackermannization comparison <docs/qfuf-disequality-propagation.md#L205-L236>`.
The {source}`diagnostic producer <src/smt/solver_engines.cpp#L404-L481>` implements
these counts. They are
zero in ordinary runs:

- `:euf.ackermann.applications`;
- `:euf.ackermann.function-symbols`;
- `:euf.ackermann.eligible-symbols`;
- `:euf.ackermann.unary-symbols`;
- `:euf.ackermann.nary-symbols`;
- `:euf.ackermann.estimated-constraints`;
- `:euf.ackermann.estimated-equalities`;
- `:euf.ackermann.estimated-clause-lits`;
- `:euf.ackermann.max-symbol-applications`;
- `:euf.ackermann.max-symbol-arity`;
- `:euf.ackermann.symbols-cost-le-100`;
- `:euf.ackermann.symbols-cost-le-1000`;
- `:euf.ackermann.symbols-cost-le-10000`;

The dynamic Ackermann shadow counters below are populated when
`--euf-dynamic-ackermann-shadow` is enabled. They inspect only candidate
functional-consistency clauses whose result equality is already a SAT-visible
theory atom, and they classify a candidate as SAT-visible only when every
argument equality is also already a SAT-visible theory atom. The pass does not
add clauses or create missing atoms:

- `:euf.dynamic-ack.shadow.invocations`;
- `:euf.dynamic-ack.shadow.result-pairs`;
- `:euf.dynamic-ack.shadow.arg-atoms-missing`;
- `:euf.dynamic-ack.shadow.sat-visible-clauses`;
- `:euf.dynamic-ack.shadow.satisfied`;
- `:euf.dynamic-ack.shadow.unit`;
- `:euf.dynamic-ack.shadow.conflicting`;
- `:euf.dynamic-ack.shadow.result-diseq-assigned`;
- `:euf.dynamic-ack.shadow.all-args-eq-assigned`;
- `:euf.dynamic-ack.shadow.width-total`;
- `:euf.dynamic-ack.shadow.max-width`;
- `:euf.dynamic-ack.shadow.width-le-3`;
- `:euf.dynamic-ack.shadow.width-gt-3`;

The watched disequality-Ackermann counters below are populated by the
default-off `--euf-diseq-ackermann` online propagation experiment. They describe
candidate construction and queue-local firing for contraposed Ackermann clauses
over existing theory atoms. The option never creates missing equality atoms and
does not scan all candidates when the watched queue is empty:

- `:euf.diseq-ack.candidates`;
- `:euf.diseq-ack.queue-pushes`;
- `:euf.diseq-ack.queue-pops`;
- `:euf.diseq-ack.width-skips`;
- `:euf.diseq-ack.missing-arg-atom`;
- `:euf.diseq-ack.satisfied-skips`;
- `:euf.diseq-ack.unit`;
- `:euf.diseq-ack.conflict`;
- `:euf.diseq-ack.propagated`: pending propagations produced by the watcher;
- `:euf.diseq-ack.returned`: pending propagations actually returned from the
  CaDiCaL `cb_propagate` callback;
- `:euf.diseq-ack.pending-cleared`: pending Ackermann propagations discarded
  after assignment notification or backtracking before being returned;
- `:euf.diseq-ack.reason-lits`;
- `:euf.diseq-ack.queue-budget-cutoffs`;
- `:euf.diseq-ack.per-symbol-cutoffs`;

The incremental closure counters include:

- `:euf.incremental.terms`;
- `:euf.incremental.flat-definitions`;
- `:euf.incremental.merges`;
- `:euf.incremental.pending-pops`;
- `:euf.incremental.lookup-probes`;
- `:euf.incremental.lookup-hits`;
- `:euf.incremental.congruence-merges`;
- `:euf.incremental.disequality-checks`;
- `:euf.incremental.conflicts`;
- `:euf.incremental.explain-calls`;
- `:euf.incremental.explain-cache-hits`;
- `:euf.incremental.explain-length`;
- `:euf.incremental.explain-proof-forest.shorter-than-bfs`;
- `:euf.incremental.explain-proof-forest.equal-to-bfs`;
- `:euf.incremental.explain-proof-forest.longer-than-bfs`;
- `:euf.incremental.backtracks`;
- `:euf.incremental.trail-entries`;
- `:euf.incremental.class-member-trail-entries-saved`.

For ground preprocessing comparisons, pass `--preprocess-only --stats`:

```bash
./build/primo --quiet --preprocess-only --stats docs/examples/unknown.smt2
```

This developer diagnostic emits preprocessing counters without entering
SAT/theory search. It returns `unsat` if preprocessing proves a contradiction
and `unknown` otherwise; it does not print the transformed assertions. With
quantified `UF` input, it stops after the frontend and skips ground
preprocessing as well as instantiation. Do not use this mode for benchmark
solved counts.

`:euf.theory-conflicts` counts EUF conflict clauses learned either by the
ordinary post-model check or by the CaDiCaL external-clause callback used by
`--euf-prop=conflict|online`.

`:euf.assignments`, `:euf.eq-assignments`, and `:euf.diseq-assignments` count
theory-literal notifications received by the online/conflict EUF callback.
Predicate atoms contribute only to `:euf.assignments`.

The delayed finite-domain stage records
`:preprocessing.finite-domain.raw-cache-hits` and `raw-cache-refreshes`.
A hit requires exactly matching ordered source/executing assertion IDs; a
refresh regenerates raw constraints on the executing goal. Symmetry has the
analogous `:preprocessing.symmetry.cache-hits` and `cache-refreshes` counters.
Each counts an enabled cache decision per nonterminal goal alternative.
One tactic call over two alternatives may increment twice; these are not
counts of tactic invocations, clauses or solver decisions.

## Experimental pure-QF_LRA equality propagation

With `--lra-equality-propagation`, the per-query `OnlineLraPropagator` records:

| Counter | Meaning |
| --- | --- |
| `:lra.equality-propagation.targets` | Existing comparison atoms indexed as normalized variable differences. |
| `:lra.equality-propagation.scans` | Feasible-state model-unit queries after arithmetic changes or backtracking. |
| `:lra.equality-propagation.backtrack-repairs` | Backtrack callbacks scheduling a feasibility repair before further equality discovery. |
| `:lra.equality-propagation.equalities` | Explained edges returned by those queries, summed over scans. |
| `:lra.equality-propagation.rows-inspected` | Candidate basic rows inspected by the producer. |
| `:lra.equality-propagation.nonzeros-inspected` | Row entries inspected by the producer. |
| `:lra.equality-propagation.propagations` | Target literals actually delivered to SAT, excluding queued candidates skipped because already assigned. |
| `:lra.equality-propagation.conflicts` | Indexed target literals found assigned opposite to their explained consequence. |
| `:time.lra-equality-propagation-us` | Query, component-explanation and target-scan time in microseconds. |

The counters count repeated work; discovered edges need not be distinct across
scans, and discoveries need not lead to propagation. Fixed-variable matches can
produce edges without inspecting any basic row. No target index or scan is
built in the default configuration. See
[scope and explanation rules](#experimental-pure-arithmetic-equality-propagation).

## EUF callbacks and resource limits

The callback keeps assignment and pending-propagation membership in fixed
arrays indexed by SAT variable. If these counters change without a matching
change in `:euf.theory-atoms`, inspect the observed-variable map and
`notify_backtrack` rollback path before treating the run as a solver-search
effect.

The propagation counters are used by both `--euf-prop=model` and
`--euf-prop=online`; `--euf-prop=conflict` leaves them at zero. In online mode,
one round corresponds to a callback pass that inspects currently unassigned
observed theory atoms. When `--euf-prop=auto` selects online callbacks, repeated
zero-delivery scans increase the discovery interval, so the candidate counter
can be much smaller than `rounds * theory-atoms`.

`--max-theory-conflicts N` limits the per-query aggregate `:theory.conflicts`
on supported ground, quantified and retained-session routes. It counts each
conflict reported to SMT/SAT once, including integer-infeasibility conflicts.
Ordinary propagations, quantifier instances and internal branch-and-bound
failures are excluded. Existing EUF/LRA diagnostic counters remain separate;
their sum is not the budget counter. See the
[implementation contract](#uniform-conflict-budget-implementation-contract)
for ownership, retry sharing and safe-boundary semantics. This does not bound
preprocessing, elapsed time or work between conflicts.
For mixed QF_UFLRA instances that finish SAT search but spend time in final
equality sharing or arrangement/model reconstruction,
`--nelson-oppen-arrangement-probe-limit N` instead caps temporary LRA
consistency probes and returns `unknown` at the cap. Use it when
`:no.interface.lra-extra-atom-probes` grows but the route's theory-conflict count does not.

`SIGTERM` and `SIGINT` are answered on two paths. The handler performs only
async-signal-safe work: it writes a `sig_atomic_t` cancellation flag and arms
an alarm. Tactic boundaries and every owned SAT backend observe that flag
through the solve context and CaDiCaL terminator, so a SAT search unwinds
cooperatively and `unknown` plus the `--stats` block are printed by the normal
CLI path. The terminator connection is scoped to the backend owner and is
disconnected before either object is destroyed, so no process-global backend
pointer or statistics sink is retained.

Cooperative cancellation alone cannot bound the response, because a signal
arriving inside a long parser, frontend, preprocessing leaf, or model-building
phase is not observed until that phase ends. The armed alarm is the second
path: after `--cancel-grace-seconds` (default 1) its handler writes `unknown`
and exits. That handler is restricted to async-signal-safe calls, so a run
that reaches it reports no statistics -- the grace period exists to give the
cooperative path, which does report them, the first chance to answer. Raise it
when a harness allows a longer wait and partial statistics matter more than a
prompt exit.

The benchmark harnesses send `SIGTERM` first and only use `SIGKILL` after a
short grace period of their own, which must exceed `--cancel-grace-seconds` for
the answer to be seen at all. A row answered by the watchdog carries `unknown`
with no statistics; a row answered cooperatively carries both. Inspect
`timed_out_with_stats`, `exit_code`, and `stderr_head` to tell them apart.
Retain an external time limit for frontend work and search without counted conflicts.

The auto counters record which concrete path `--euf-prop=auto` selected for the
current abstraction.

Nelson-Oppen interface diagnostics are emitted in stats output when
`--nelson-oppen-diagnostics` is enabled. They are read-only counters from the
post-preprocessing assertions, not evidence that combination is enabled:

- `:no.interface.terms`
- `:no.interface.sorts`
- `:no.interface.candidate-pairs`
- `:no.interface.registry-entries`
- `:no.interface.visible-equalities`
- `:no.interface.missing-visible-equalities`
- `:no.purification.sites`
- `:no.purification.euf-real-terms`
- `:no.purification.arithmetic-uf-apps`

The diagnostic-only purification-site counters above identify mixed-atom
blockers: Real-sorted subterms inside EUF atoms and uninterpreted applications
inside arithmetic atoms.

The default QF_UFLRA path has its own runnable partial-combination counters:

- `:no.purification.definitions`
- `:no.purification.arithmetic-uf-definitions`
- `:no.purification.euf-real-term-definitions`
- `:no.purification.reused-applications`
- `:no.purification.reused-real-terms`
- `:no.purification.memoized-formula-rewrites`
- `:no.purification.memoized-arithmetic-rewrites`
- `:no.purification.memoized-euf-rewrites`
- `:no.purification.skipped-no-sites`
- `:no.interface.shared-real-terms`
- `:no.interface.sat-equality-atoms`
- `:no.interface.sat-equalities-true`
- `:no.interface.sat-equalities-false`
- `:no.interface.sat-equalities-forced`
- `:no.interface.equalities-propagated`
- `:no.interface.euf-to-lra-equalities`
- `:no.interface.lra-to-euf-equalities`
- `:no.interface.equality-candidates`
- `:no.interface.equality-existing-atoms`
- `:no.interface.equality-missing-atoms`
- `:no.interface.equality-created-atoms`
- `:no.interface.equality-already-true`
- `:no.interface.equality-assigned-false`
- `:no.interface.equality-clauses`
- `:no.interface.equality-reason-lits`
- `:no.interface.equality-local-reasons`
- `:no.interface.equality-local-reason-lits`
- `:no.interface.equality-fallback-reasons`
- `:no.interface.euf-explanation-missing`
- `:no.interface.convex-rounds`
- `:no.interface.exact-only`
- `:no.interface.euf-spanning-equalities`
- `:no.interface.lra-opportunistic-enabled`
- `:no.interface.lra-opportunistic-skipped-rounds`
- `:no.interface.lra-opportunistic-candidates`
- `:no.interface.lra-opportunistic-fixed-eq`
- `:no.interface.lra-opportunistic-eq-row`
- `:no.interface.lra-opportunistic-eq-offset-rows`
- `:no.interface.lra-opportunistic-eq-rows`
- `:no.interface.lra-opportunistic-reason-lits`
- `:no.interface.lra-opportunistic-unmapped-terms`
- `:no.interface.lra-opportunistic-unmapped-equalities`
- `:no.interface.lra-opportunistic-existing-atoms`
- `:no.interface.lra-opportunistic-missing-atoms`
- `:no.interface.lra-opportunistic-created-atoms`
- `:no.interface.lra-opportunistic-clauses`
- `:no.interface.lra-opportunistic-rechecks`
- `:no.interface.lra-legacy-producer-selected`
- `:no.interface.lra-legacy-scan-skipped`
- `:no.interface.lra-model-unit-selected`
- `:no.interface.lra-model-unit-classes`
- `:no.interface.lra-model-unit-class-terms`
- `:no.interface.lra-model-unit-candidate-pairs`
- `:no.interface.lra-model-unit-rows-inspected`
- `:no.interface.lra-model-unit-nonzeros-inspected`
- `:no.interface.lra-model-unit-fixed-matches`
- `:no.interface.lra-model-unit-one-row-matches`
- `:no.interface.lra-model-unit-two-row-matches`
- `:no.interface.lra-model-unit-nonunit-rejections`
- `:no.interface.lra-model-unit-nonfixed-row-rejections`
- `:no.interface.lra-model-unit-reason-lits`
- `:no.interface.lra-adaptive-selected`
- `:no.interface.lra-adaptive-bootstrap-legacy-rounds`
- `:no.interface.lra-adaptive-bootstrap-completions`
- `:no.interface.lra-adaptive-model-unit-rounds`
- `:no.interface.lra-adaptive-legacy-rounds`
- `:no.interface.lra-adaptive-fallback-activations`
- `:time.no-lra-equality-producer-us`
- `:no.interface.lra-exact-fallback-pairs`
- `:no.interface.lra-exact-fallback-skipped-pairs`
- `:no.interface.lra-exact-fallback-equalities`
- `:no.interface.lra-model-filter-builds`
- `:no.interface.lra-model-filter-full-pairs`
- `:no.interface.lra-model-filter-candidate-pairs`
- `:no.interface.lra-model-filter-pruned-pairs`
- `:no.interface.lra-model-filter-unmapped-terms`
- `:no.interface.lra-model-filter-conservative-pairs`
- `:no.interface.materialized-pairs`
- `:no.interface.materialized-missing-pairs`
- `:no.interface.equality-rechecks`
- `:no.interface.euf-to-lra-candidates`
- `:no.interface.euf-to-lra-existing-atoms`
- `:no.interface.euf-to-lra-missing-atoms`
- `:no.interface.euf-to-lra-created-atoms`
- `:no.interface.euf-to-lra-clauses`
- `:no.interface.euf-to-lra-rechecks`
- `:no.interface.lra-to-euf-candidates`
- `:no.interface.lra-to-euf-existing-atoms`
- `:no.interface.lra-to-euf-missing-atoms`
- `:no.interface.lra-to-euf-created-atoms`
- `:no.interface.lra-to-euf-clauses`
- `:no.interface.lra-to-euf-rechecks`
- `:no.interface.lra-to-euf-pair-dedup-lookups`
- `:no.interface.lra-to-euf-pair-dedup-duplicates`
- `:no.interface.conflicts`
- `:no.interface.euf-conflicts`
- `:no.interface.lra-conflicts`
- `:no.interface.model-equality-candidates`
- `:no.interface.model-equality-created`
- `:no.interface.model-equality-pending`
- `:no.interface.model-equality-activated`
- `:no.interface.model-equality-duplicate-pending`
- `:no.interface.model-equality-pending-batches`
- `:no.interface.model-equality-pending-batch-lits`
- `:no.interface.model-equality-max-pending-batch`
- `:no.interface.model-equality-assumptions`
- `:no.interface.model-equality-assumption-solve-calls`
- `:no.interface.model-equality-failed-assumptions`
- `:no.interface.model-equality-blocking-clauses`
- `:no.interface.model-equality-dropped-assumptions`
- `:no.interface.model-equality-retained-assumptions`
- `:no.interface.model-equality-conflicts`
- `:no.interface.model-equality-full-pairs`
- `:no.interface.model-equality-filtered-pairs`
- `:no.interface.model-equality-care-pairs`
- `:no.interface.model-equality-care-pruned-pairs`
- `:no.interface.model-equality-scanned`
- `:no.interface.model-equality-lra-equal`
- `:no.interface.lra-model-mutation-enabled`
- `:no.interface.lra-model-mutation-calls`
- `:no.interface.lra-model-mutation-updates`
- `:no.interface.lra-model-mutation-value-classes-before`
- `:no.interface.lra-model-mutation-value-classes-after`
- `:no.interface.lra-model-mutation-candidate-pairs-before`
- `:no.interface.lra-model-mutation-candidate-pairs-after`
- `:no.interface.lra-model-mutation-pruned-pairs`
- `:no.interface.model-equality-mutation-checks`
- `:no.interface.model-equality-mutated`
- `:no.interface.model-equality-selected-care`
- `:no.interface.model-equality-selected-full`
- `:no.interface.model-equality-skipped-assigned`
- `:no.interface.model-equality-skipped-tried`
- `:no.interface.model-equality-queue-builds`
- `:no.interface.model-equality-queued`
- `:no.interface.model-equality-queue-pops`
- `:no.interface.model-equality-queue-skipped-tried`
- `:no.interface.model-equality-queue-skipped-assigned`
- `:no.interface.model-equality-queue-empty`
- `:no.interface.model-equality-adaptive-stops`
- `:no.interface.model-equality-active-assumptions`
- `:no.interface.model-equality-max-active-assumptions`
- `:no.interface.arrangement-checks`
- `:no.interface.arrangement-failures`
- `:no.interface.arrangement-separations`
- `:no.interface.arrangement-lra-extra-atom-probes`
- `:no.interface.arrangement-lra-extra-atoms-probed`
- `:no.interface.lra-extra-atom-probes`
- `:no.interface.lra-extra-atoms-probed`
- `:no.interface.lra-extra-atom-probe-limit-hit`
- `:no.interface.euf-care-checks`
- `:no.interface.euf-care-full-pairs`
- `:no.interface.euf-care-pairs`
- `:no.interface.euf-care-pruned-pairs`
- `:no.interface.euf-care-argument-definitions`
- `:no.interface.euf-care-function-classes`
- `:no.interface.euf-care-definition-pairs-full`
- `:no.interface.euf-care-definition-pairs-within-classes`
- `:no.interface.euf-care-argument-pairs-checked`
- `:no.interface.euf-care-definition-pairs-pruned`
- `:no.interface.euf-care-duplicate-argument-pairs-pruned`
- `:no.interface.euf-care-cache-hits`
- `:no.interface.euf-care-cache-misses`
- `:no.interface.euf-implied-class-projections`
- `:no.interface.euf-implied-class-projection-terms`
- `:no.interface.lra-prepared-reused`
- `:no.interface.lra-prepared-augmented`
- `:no.interface.lra-online-enabled`
- `:no.interface.lra-online-final-checks`
- `:no.online.enabled`
- `:no.online.shared-real-pairs`
- `:no.online.precreated-equality-atoms`
- `:no.online.paired-bound-precreate-candidates`
- `:no.online.assignments`
- `:no.online.backtracks`
- `:no.online.lra-bound-equality-candidates`
- `:no.online.lra-bound-equality-missing-atoms`
- `:no.online.lra-bound-equality-materialized`
- `:no.online.lazy-equality-atoms-created`
- `:no.online.lazy-equality-vars-observed`
- `:no.online.propagations`
- `:no.online.reason-lits`
- `:no.online.skipped-assigned`
- `:no.online.final-lra-equality-scan-skipped`
- `:no.online.final-lra-equality-scan-capped`
- `:no.online.final-lra-equality-scan-fallbacks`
- `:no.interface.lra-conflict-lits`
- `:no.interface.model-separations`
- `:no.interface.model-real-terms-before-interface-filter`
- `:no.interface.model-real-terms-filtered`
- `:no.interface.model-euf-real-classes`
- `:no.interface.model-euf-real-anchored-classes`
- `:no.interface.model-euf-real-fresh-classes`
- `:no.interface.model-euf-real-unmapped-terms`
- `:no.interface.model-euf-real-value-conflicts`
- `:no.interface.model-separation-candidates`
- `:no.interface.model-separation-euf-mergeable-pairs`
- `:no.interface.model-separation-euf-forbidden-pairs`
- `:no.interface.model-separation-search-capped`
- `:no.interface.model-separation-search-branches`
- `:no.interface.model-separation-search-backtracks`
- `:no.interface.model-separation-model-oriented`

These count internal proxy definitions in total, the split between Real-valued
UF applications in arithmetic atoms and non-constant Real terms in EUF atoms or
EUF-side application definitions, repeated uses of already-created application
or Real-term proxies, same-pass rewrite-cache hits over hash-consed formula,
arithmetic, and EUF terms, Real proxy terms visible to both EUF and LRA, and
equality checks in each interface direction. The older
`:no.interface.equalities-propagated`, `:no.interface.euf-to-lra-equalities`,
and `:no.interface.lra-to-euf-equalities` counters count implied equalities
reported by the producing theory, even when the equality is already true or
cannot yet be delivered as a useful SAT clause. The newer
`:no.interface.equality-*` counters split that total into SAT-visible existing
atoms, missing equality atoms, lazily created atoms, already-true equalities,
equalities assigned false in the current SAT assignment, guarded clauses added,
reason literals in those clauses, and solver-loop rechecks caused by those
clauses. The directional `:no.interface.euf-to-lra-*` and
`:no.interface.lra-to-euf-*` counters provide the same clause-delivery split per
producer/consumer direction. `:no.interface.sat-equality-atoms` counts
SAT-visible shared-Real equality atoms registered so far, while
`:no.interface.sat-equalities-true` and `:no.interface.sat-equalities-false`
describe the last checked SAT assignment. Strict shared-Real separations are
counted while validating a final `sat` arrangement or reconstructing mixed
models. A false generated interface equality is asserted to EUF as a
disequality; it is not asserted to LRA as a native false-`Eq` atom. Ordinary
SMT-LIB Real disequality such as `(not (= x y))` is supported by Real equality
lowering and Boolean-level case splitting before prepared LRA atoms are asserted.
The `euf-care-argument-definitions`, `euf-care-function-classes`, and
`euf-care-definition-pairs-*` counters are cumulative across care checks.
`definition-pairs-full` is the work of the former all-definition-pairs scan;
`definition-pairs-within-classes` is the cross-class-pruned intermediate count,
and `argument-pairs-checked` is the work retained after equal arguments within
each class are deduplicated. The two `*-pruned` counters report those reductions.
The work counters advance only on `euf-care-cache-misses`. A cache hit reuses
the exact care diagnostics only when the complete ordered EUF assignment,
including SAT-visible interface equalities, is unchanged.
`:no.interface.convex-rounds` counts SAT-consistent mixed rounds that reach
bidirectional equality sharing. EUF reports one spanning set per shared
equality class, counted by `:no.interface.euf-spanning-equalities`; it does not
materialize every pair in a class. The corresponding
`euf-implied-class-projection-*` counters report the expected-linear closure
projection used to find those classes. `:no.interface.exact-only` is one for
`--nelson-oppen-prop=exact` and its legacy `off` alias. In that mode mandatory
EUF spanning and exact LRA fallback sharing remain active, while
`:no.interface.lra-opportunistic-enabled` is zero and
`:no.interface.lra-opportunistic-skipped-rounds` counts rounds that omit the
optional opportunistic poll. LRA otherwise first polls the checked tableau's
`FixedEq`, `EqRow`, `EqOffsetRows`, and `EqRows` rules, explained under
{ref}`Default LRA rules <lra-default-equality-rules>`. The
`:no.interface.lra-opportunistic-*` counters
split rule hits, reason literals, unmapped shared terms, lazy SAT atom delivery,
clauses, and rechecks. After these cheap producers are quiescent, the feasible
tableau's `model_equal_classes()` partition removes cross-value pairs, which
cannot be LRA-entailed. The `:no.interface.lra-model-filter-*` counters report
partition builds, the unfiltered cross product, retained and pruned pairs, and
conservative fallbacks for unmapped terms. Equal-value pairs are only candidates
and still require exact proof. `:no.interface.lra-exact-fallback-pairs` counts
unresolved retained pairs sent to strict-separation probes,
`:no.interface.lra-exact-fallback-skipped-pairs` counts retained pairs already
resolved by SAT/EUF, and
`:no.interface.lra-exact-fallback-equalities` counts exact entailments found.
These counters are cumulative across fixpoint rounds. A FixedEq/EqRow test with
zero fallback pairs is the direct evidence that the cheap producer removed
probe work; a zero fallback count on arbitrary inputs is not expected.
For LRA-to-EUF equality sharing, the default `final` path classifies each
LRA-implied shared Real equality through the current EUF closure before creating
or forcing an equality atom. The filter records
`:no.interface.lra-to-euf-equalities-euf-already-true` when EUF already has the
pair equal, `:no.interface.lra-to-euf-equalities-euf-mergeable-skipped` when EUF
can absorb the LRA equality without conflict and the equality is skipped,
`:no.interface.lra-to-euf-equalities-euf-mergeable-forced` when such a
mergeable equality is conservatively sent through the guarded clause path, and
`:no.interface.lra-to-euf-equalities-euf-forbidden-forced` when EUF rejects the
equality immediately. The 2026-08-05 full QF_UFLRA run rejected the
mergeable-skip optimization after wrong answers; the conservative path should
therefore keep `mergeable-skipped` and
`:no.interface.lra-to-euf-internal-model-equalities` at zero for LRA-to-EUF
sharing. Broad fallback after an internal skip is counted by
`:no.interface.lra-to-euf-skip-model-reconstruction-fallbacks` and should also
remain zero on the conservative path.

With the default QF_UFLRA model reconciliation, the broad exact scan is no
longer the normal final step. The shared-model counters diagnose the
replacement (`--no-qf-uflra-model-reconciliation` selects the old fallback):

- `:no.shared-model.forbidden-equalities`, `forbidden-before`, and
  `forbidden-after` report SAT-false pairs supplied to scratch LRA mutation;
- `:no.shared-model.unresolved-forbidden` reports violations remaining in the
  final candidate partition;
- `:no.shared-model.targeted-separation-*` reports strict-orientation searches
  restricted to unresolved mandatory disequalities;
- `:no.shared-model.compatibility-edges`, `compatibility-merges`, and
  `compatibility-projections` report EUF absorption of LRA class spanning
  edges;
- `:no.shared-model.compatibility-splits` counts direct or congruence-induced
  rejection of an optional merge, while
  `compatibility-consequence-disagreements` isolates the latter;
- `:no.shared-model.split-frontier-batches`, `split-frontier-pairs`,
  `split-frontier-duplicates`, and `split-frontier-max` report how many rejected
  model edges are canonicalized and sent through one SAT re-entry rather than
  stopping at the first edge;
- `:no.shared-model.split-materializations` counts lazy positive-first equality
  assumptions; failed assumptions continue to use the
  `:no.interface.model-equality-*` blocking-clause counters;
- `:no.shared-model.compatibility-successes` and
  `:no.online.final-lra-equality-scan-skipped` count accepted candidate rounds.

The `:no.online.*` spelling of the final-scan counter is historical. It also
counts reconciliation rounds when `--nelson-oppen-prop` is `final`. Nonzero
`:no.shared-model.exact-fallbacks` means an unexpected mapping failure, failed
mandatory separation, or another candidate invariant forced the old complete
path. The shared interface is the intersection of EUF Real constants with
variables that survive LRA polynomial normalization; syntactically present but
algebraically cancelled constants are EUF-only model terms. Consequently
`lra-model-filter-unmapped-terms` should remain zero. A model-equality
coincidence alone is never an entailed propagation.

Augmented LRA component-state reuse is reported by:

- `:no.interface.lra-model-epoch-prepared-hits` and `prepared-misses`;
- `:no.interface.lra-model-epoch-check-hits` and `check-misses`;
- `:no.interface.lra-model-epoch-tableau-reuses` for same-interface checks that
  pop prior bounds and reuse the feasible basis;
- `:no.interface.lra-model-epoch-invalidations` when the exact true-interface
  equality key changes.

This epoch exists only in mixed QF_UFLRA when true generated interface
equalities augment LRA. A prepared hit with a check miss is expected: it means
the same rows are being checked under a different current SAT assignment.

When default-off `--model-equality-branching` is enabled, the EUF care counters
compare the full shared-Real cross product with pairs that occur as arguments
under flat EUF function prefixes equal in the current EUF closure. This optional
branch selector runs only when model reconciliation is disabled. Default
reconciliation can establish compatible component models without a broad exact
pair scan; the exact fallback retains complete coverage when needed. The default
`--model-equality-branch-policy=first` path also uses the full pair order and
reports the full pair count through `:no.interface.model-equality-care-pairs`
for continuity. The `ranked` and `adaptive` policies use care-pair diagnostics
only as a ranking signal; they fall back to the full pair set instead of pruning
the search. The opt-in `care-queued` policy rebuilds a bounded queue from the
current EUF care pairs only, so a shared Real pair that is merely present in the
full cross product is not materialized as a model-equality branch candidate.
The opt-in `--qf-uflra-candidate-final-scan` path also consumes the EUF care
diagnostic, but for entailed LRA-to-EUF equality propagation rather than model
guesses. It probes only a candidate set made from current EUF care pairs, shared
Real constants co-occurring inside one EUF atom, and shared Real pairs whose
two non-strict LRA bounds are both true in the current SAT assignment. The
counters `:no.interface.candidate-final-scan-enabled`,
`:no.interface.candidate-final-scan-pairs`,
`:no.interface.candidate-final-scan-skipped-pairs`, and
`:no.interface.candidate-final-scan-equalities` report that first pass. If
final arrangement validation or model reconstruction cannot be completed using
those equalities, `:no.interface.candidate-final-scan-fallbacks` increments and
Primo runs the broad all-pairs LRA equality scan once. Equalities found by that
fallback are forced through SAT before the solver accepts the final
arrangement; `:no.interface.candidate-final-scan-fallback-equalities` records
how many the fallback found.
`:no.interface.conflicts` counts ordinary EUF/LRA conflict clauses whose reason
mentions a generated interface equality literal, split by `:no.interface.euf-conflicts` and
`:no.interface.lra-conflicts`; direct single-theory conflicts that do not use
the generated interface atom are not included there. The model-separation search
counters record the number of strict-order candidate pairs, whether the bounded
backtracking cap was exceeded, and the LRA-consistency branches/backtracks tried
inside final arrangement validation or model reconstruction.
`:no.interface.model-separation-model-oriented` counts strict shared-Real
separations oriented directly from one concrete LRA model in the greedy capped
path, before falling back to per-candidate LRA consistency checks for pairs tied
in that model.
`:no.interface.lra-prepared-reused` counts mixed QF_UFLRA LRA checks that reuse
the base prepared LRA theory because no SAT-visible interface equality is true
in the current assignment. `:no.interface.lra-prepared-augmented` counts actual
constructions of an augmented prepared LRA theory on an equality-key cache miss.
It does not count every round that uses true generated interface equalities.
`:no.interface.lra-fixed-elimination-enabled` is one when fixed-variable
elimination is active in the mixed tableau. It is on by default in QF_UFLRA,
can be disabled with `--no-qf-uflra-fixed-elimination`, and does not describe
the separate pure-QF_LRA default. `:no.interface.lra-online-enabled` records whether the
default mixed coordinator attached the pure-QF_LRA trail-synced online
propagator for original arithmetic atoms, and
`:no.interface.lra-online-final-checks` counts final SAT assignments whose base
LRA consistency result came from that callback-owned state. This online path is
disabled for the experimental `--model-equality-branching` mode; when generated
interface equalities are true, the coordinator uses the augmented LRA epoch.
The same equality key reuses preparation; the same assignment can also reuse
the completed check. A changed assignment can reuse the tableau and feasible
basis. The epoch counters above distinguish these cases.

EUF preparation follows the same reuse boundary. `:no.interface.euf-prepared-reused`
counts rounds whose existing `PreparedFlatTheory` was sufficient, while
`:no.interface.euf-prepared-augmented` counts rounds that appended newly
materialized interface equality atoms or their flat definitions. The
`:no.interface.euf-prepared-atoms` and `:no.interface.euf-prepared-definitions`
counters report the cumulative prepared sizes. This is structural reuse only:
each SAT round still copies the definition-only congruence closure and asserts
its own assignments, so explanations and backtracking remain round-local.
The preparation is append-only during one mixed solve because lazy interface
equality atoms and Boolean EUF proxies can appear after the initial
preparation. Each cached atom is keyed by its signed SAT literal and `TermId`.
Before a closure check, current assignments are realigned through that key;
their input order is not stable because generated equalities are gathered as
true atoms followed by false atoms in each SAT round. A large
`:sat.solve-calls` count on a small model-equality example can indicate a broken
alignment: a conflict clause may then describe a different cached equality and
already be satisfied by the assignment that produced it.

With `--qf-uflra-online-euf`, final reconciliation first attempts to borrow the
trail-synchronized closure instead of copying `PreparedFlatTheory::base_closure`.
`:no.interface.euf-live-model-attempts` splits into
`:no.interface.euf-live-model-reuses` and
`:no.interface.euf-live-model-fallbacks`; the latter is further diagnosed by
`:no.interface.euf-live-model-incomplete` and
`:no.interface.euf-live-model-registration-invalid`.
`:no.interface.euf-final-closure-rebuilds` is the direct count of broad final
closure copies. Retained assignments must stay recorded across repeated solves
and backtracks; they need not be reported as new assignments again.
The old `:no.interface.euf-live-model-callback-assignments` recovery counter
was removed with shared assignment ownership. An online model that lacks an
observed assignment now raises an internal error instead of repairing state.
The [assignment and rollback contract](../implementation/dpllt.md) explains
why that recovery once concealed lost assignment markers.
`:no.interface.euf-live-deferred-callbacks` now counts notifications queued by
the shared coordinator during a protected model scope, across all consumers.
The driver publishes it after replay. `:no.interface.euf-live-registry-syncs`
counts pre-solve generated-equality registry synchronization passes. A
protected-checkpoint exception indicates that coordinator or callback code
tried to roll back SAT-owned EUF state through a live model scope.

`--nelson-oppen-prop=online` adds a mixed-theory dispatcher around the LRA
callback. `:no.online.enabled` records that the dispatcher, not the direct LRA
callback, was connected. `:no.online.precreated-equality-atoms` counts the
shared Real equality literals created before SAT search; in the lazy online path
it should remain zero. `:no.online.paired-bound-precreate-candidates` is kept as
a compatibility diagnostic and should also remain zero. The full shared Real
cross product is reported by `:no.online.shared-real-pairs` for diagnostics
only, not as an observed SAT variable count. `:no.online.assignments` and
`:no.online.backtracks` count the dispatcher trail traffic.
`:no.online.lra-published-equalities` counts equality candidates produced by
`OnlineLraPropagator`; today those are exactly the implemented paired-bound
rule where both `(<= a b)` and `(<= b a)` are true for a shared Real pair.
Broader LRA-derived equalities are intentionally not part of this **online
callback** counter. `FixedEq` and `EqRow` are polled after the final feasible
LRA check and reported through `:no.interface.lra-opportunistic-*`; they do not
run on each callback bound update.
`:no.online.lra-bound-equality-candidates` counts published candidates whose
pair is known to the mixed-theory interface registry.
`:no.online.lra-bound-equality-missing-atoms` counts published candidates outside
that registry; this can still be nonzero because the pure LRA producer may
publish equalities for pairs that are not mixed-theory shared Real interface
pairs. `:no.online.lra-bound-equality-materialized` counts candidates that
obtained a deliverable equality literal, either by reusing an existing atom or
by lazy creation. `:no.online.lazy-equality-atoms-created` counts equality atoms
created by the dispatcher from `cb_propagate()`, and
`:no.online.lazy-equality-vars-observed` counts the corresponding fresh observed
SAT variables.
`:no.online.final-lra-equality-scan-skipped` increments when model reconciliation
establishes compatibility and avoids the broad final scan.
`:no.online.final-lra-equality-scan-fallbacks` and
`:no.online.final-lra-equality-scan-capped` remain zero-valued compatibility
counters. The complete exact fallback remains available when compatibility
has not been established; an unchecked omission of implied equalities could
otherwise miss an EUF conflict.
`:no.online.propagations` counts returned equality literals, and
`:no.online.reason-lits` counts the two bound literals used as the SAT reason.
`:no.online.skipped-assigned` counts candidates already assigned by SAT before
delivery.

`--qf-uflra-online-euf` additionally attaches the existing incremental EUF
propagator as a child of the mixed dispatcher. It works with either final or
online Nelson-Oppen propagation and is default-off. Use these
counters to distinguish useful mixed EUF work from callback overhead:

- `:no.interface.euf-online-enabled` is one only when the mixed EUF child is
  attached;
- `:no.online.euf-propagations` counts literals returned by that child;
- `:no.online.euf-external-clauses` counts EUF external-clause callbacks
  selected by the dispatcher;
- `:no.online.euf-generated-equalities-registered` counts lazily created
  interface equality atoms appended to the EUF child;
- `:no.online.euf-generated-equality-registration-failures` counts atoms whose
  endpoints were not already representable in its flat term state;
- `:no.online.euf-generated-equality-observation-failures` counts dispatcher
  attempts that could not be handed to the EUF child.

A high `:euf.propagation.rounds` value with zero
`:no.online.euf-propagations` and zero
`:no.online.euf-external-clauses` means the option is adding callback work
without helping the mixed search. Consult the live-model and final-rebuild
counters above to determine whether final validation reused callback state.
Pure QF_UF and pure QF_LRA runs should leave
`:no.online.enabled` at zero; that is the guard against single-theory overhead.
`:no.interface.arrangement-checks` counts non-model `sat` results where the
coordinator validated that the EUF arrangement over shared Real interface
arguments can be separated consistently by LRA, with failures and accepted
strict separations split into the corresponding arrangement counters.
`:no.interface.model-real-terms-before-interface-filter` reports how many Real
constants and Real-valued applications were present in purified EUF atoms
before the final arrangement/model path was restricted to the Nelson-Oppen
interface, and
`:no.interface.model-real-terms-filtered` reports how many of those EUF-side
Real terms were excluded because they are not common EUF/LRA interface
constants. Exclusion applies to arrangement search and LRA probes, not to
printed model completion.

## Model construction and combination

Model construction now uses the shared typed adapter described in
[](../implementation/models.md). The historical `qf_uflia_int_values` candidate
search, cycle skipping, arena-order completion and printer-side row priorities
have been removed. Live component values determine one immutable interpretation;
eliminated definitions are replayed against its fixed evaluated-tuple tables.
The printer only serializes it. Conflicting authoritative values are errors,
not a reason to replace a row or skip a required definition.

For QF_UFLRA `get-model`, the shared class keeps its concrete LRA value and
every unanchored EUF-only Real equivalence class receives a distinct fresh
rational. Component preparation writes that class value for the live Real
constants and applications in the class before typed reconstruction.
`:no.interface.model-euf-real-classes`,
`:no.interface.model-euf-real-anchored-classes`, and
`:no.interface.model-euf-real-fresh-classes` report that reconstruction;
`model-euf-real-unmapped-terms` or `model-euf-real-value-conflicts` makes model
construction fail with an internal error rather than print an invalid model.
Resource limits during separation search can still return `unknown`. When the
exact arrangement fallback is required, or reconciliation is disabled, that
path probes the current EUF closure with a
temporary equality for each remaining shared-Real pair. A pair counted by
`:no.interface.model-separation-euf-mergeable-pairs` is in different EUF
classes but can still be merged without an EUF conflict, so it does not require
a strict LRA order. A pair counted by
`:no.interface.model-separation-euf-forbidden-pairs` would make the EUF closure
inconsistent if merged; only those pairs become
`:no.interface.model-separation-candidates` for strict LRA separation.
`:no.interface.lra-extra-atom-probes` counts LRA consistency checks with one or
more temporary strict separation/equality atoms.
`:no.interface.lra-extra-atoms-probed` sums the number of temporary atoms across
those checks. Exact single-pair equality scans normally use a check-local copy of
the already feasible tableau and probe both strict orientations with
`push()`/`pop()`. The corresponding work is split into
`:no.interface.lra-probe-reuse-pairs`,
`:no.interface.lra-probe-reuse-orientations`, and
`:no.interface.lra-probe-reuse-assignment-bounds`.
`:no.interface.lra-probe-reuse-checked-tableau-copies` counts contexts that
avoided assignment replay. The checked basis is rejected when its endpoint
difference expands above eight terms or its prior pivot-term work exceeds eight
operations per tableau variable. The total and the two reasons are reported by
`:no.interface.lra-probe-reuse-checked-tableau-fallbacks`,
`:no.interface.lra-probe-reuse-checked-tableau-difference-fallbacks`, and
`:no.interface.lra-probe-reuse-checked-tableau-basis-cost-fallbacks`; the
observed maxima are published by the matching `max-difference-terms` and
`max-pivot-term-ops` counters. A nonzero
`:no.interface.lra-probe-reuse-mapping-fallbacks` means a shared endpoint was
absent from the prepared term map and the old augmented-theory reconstruction
oracle was used. A nonzero
`:no.interface.lra-probe-reuse-context-conflicts` means replaying an assignment
already accepted by the main LRA check conflicted in the local context. This is
an internal-consistency violation: the solver records it and fails loudly rather
than treating a base conflict as evidence for the candidate equality. These live
counters expose arrangement exploration cost even if the final search does not
finish before a profiling timeout.
`:no.interface.arrangement-lra-extra-atom-probes` and
`:no.interface.arrangement-lra-extra-atoms-probed` are the subset spent while
choosing strict separations for final mixed QF_UFLRA arrangement validation or
model reconstruction. `--nelson-oppen-arrangement-probe-limit N` caps the broader
temporary-probe counter, including conservative LRA-to-EUF equality scans that
feed the final mixed check, and returns `unknown` when the next temporary LRA
consistency check would exceed the budget.
`:no.interface.lra-extra-atom-probe-limit-hit` marks that soft stop. The cap is
opt-in and does not limit SAT solving or ordinary LRA theory conflicts.
When `--model-equality-branching` is enabled, the model-equality counters record
full shared Real pairs, how many remain after the one scratch model-mutation
sweep, how many are scanned by the branch selector, optional SAT equality atoms
created for those pairs, guesses queued for the next SAT call, guesses activated onto the IPASIR
assumption trail, duplicate queued guesses that were already active, active
assumption attempts over those atoms, SAT-level failed-assumption blocking
clauses, dropped failed assumptions, the active assumptions retained after the
last SAT rejection, assigned or already-tried equality literals skipped, whether
the selected guess came from the care-ranked prefix or the full-pair fallback,
care-queued queue builds, queued candidates, queue pops, queued candidates
skipped because their existing equality atom was already tried or assigned,
adaptive stops, the current and maximum active model-equality assumption-trail
size, and ordinary theory conflicts reached while at least one guessed equality
was active. A surviving model-equality guess is re-applied as an IPASIR
assumption on later SAT calls while the coordinator tries additional model-equal
pairs; it is still not a permanent unit clause.
`:no.interface.lra-model-mutation-*` reports the scratch diversification sweep
from de Moura and Bjørner's *Model-based Theory Combination*, §5.2
{cite:p}`MouraB08`, as implemented by
{source}`diversify_shared_model <src/theory/arithmetic/lra_tableau.cpp#L1554-L1635>`.
It runs once per feasible mixed round when enabled. The counters cover
whether the path is enabled, calls, accepted
updates, distinct value classes before/after, candidate pairs before/after, and
pairs pruned. `--model-equality-mutation-scope=all|care` selects all shared Real
variables (the default) or only current EUF care-pair endpoints. The scope
counters report the selected mode, full and scoped variable visits, empty care
scopes, attempted versus accepted updates, and care-scoped results rejected
because they would increase the complete shared candidate-pair count. Even in
care mode the final partition and printed model use all shared terms. The
pre-mutation pair count is computed from class sizes; Primo does not materialize
that pair set. `:no.interface.model-equality-filtered-pairs`
is the post-mutation candidate count presented to the optional branch selector.
The legacy `model-equality-mutation-checks` and `model-equality-mutated` counters
remain aggregate branch diagnostics for pairs considered and removed by the
sweep. A pair that remains equal after mutation is only a branch candidate, not
an entailed equality. Guarded equality clauses and
`:no.interface.sat-equalities-forced` continue to require an independent LRA or
EUF explanation.
Compare it with `:no.interface.equality-candidates`: a large gap means the
producer is rediscovering equalities that are already true, missing from the
current interface registry, or not useful enough to force a SAT recheck.

Add a counter when implementing a nontrivial preprocessing rule, generated
lemma, SAT interaction, or theory-solver action. Keep names stable once they are
documented; add a new key instead of changing the meaning of an old one.


## Collection and interpretation

{source}`Statistics <src/util/statistics.hpp>` owns registered counters and
additional string-keyed entries; {source}`the registry <src/util/statistics.cpp>`
maps registered names to `StatKey`. Dynamic tactic/session keys are also
part of the output. A missing key is not necessarily a measured zero.

Read counters within their owning route. Per-query deltas, retained-state
sizes, cumulative session counts, and nested timers are different quantities.
Do not sum nested phase timers or infer memory allocation from active clause
counts. Native-stream parsing time includes waits for the client. Process
wall time additionally includes startup and harness overhead. Independent
Dolmen time belongs to a separate validation record.

The [policy reference](policies.md) explains requested versus effective
settings; [experiments](../development/experiments.md) explains paired
comparisons. New mechanisms must document their counters and actual scope;
`--stats` controls printing, not compilation of instrumentation.

## Uniform conflict budget implementation contract

The publication repair uses an SMT-owned aggregate `:theory.conflicts`, separate
from the existing EUF and LRA diagnostic counters. The original runtime design
counts one conflict reported to SMT/SAT, including integer infeasibility, but
excludes ordinary propagations, quantifier instances and internal arithmetic
branch failures. {repairsource}`record_theory_conflict <src/smt/solver_runtime.cpp>` owns aggregate
increments; {repairsource}`SolveTerminator <src/smt/solver_runtime.hpp>` owns
exhaustion checks. `StatKey::TheoryConflictCount` is the typed aggregate key. No theory kernel owns query budgets.

The query's `Statistics` object carries consumption through solver retries,
lemma-growth rounds and combination fallback. A new query owns fresh statistics.
Callbacks finish their clause/reason protocol before the solver returns
`Unknown` with `ResourceLimit` and `theory conflict limit reached`. Safe-boundary
checks before and after SAT calls and after final theory checks must override
terminal SAT/UNSAT answers when the budget is exhausted. Unlimited queries keep
the same solving behavior. The limit does not bound preprocessing, elapsed time,
or work between conflicts.

This is an original resource-accounting contract, not a new paper-backed theory
algorithm. Its proof obligations are single counting at the SMT reporting
boundary, no double counting through component adapters, consumption shared
within one query, and reset between queries. Regressions must cover online and
lazy routes, integer conflicts, callback completion, fallback and retained
sessions. The [publication follow-up gate](../evidence/publication-followups.md)
records the regressions and 800 strict bounded-budget replay samples.

The {repairsource}`component dispatcher <src/smt/component_dispatcher.cpp#L114-L149>`
latches exhaustion when it receives the conflict that
consumes the remaining budget. It finishes streaming that clause to SAT, then
suppresses further component checks and propagations until the solve returns.
The runtime supplies the final `ResourceLimit` outcome. This prevents extra
conflict reports while the backend finishes its callback protocol.

Direct {repairsource}`OnlineEufPropagator <src/smt/solver_engines.cpp#L1848-L1867>`
and {repairsource}`OnlineLraPropagator <src/smt/solver_engines.cpp#L7250-L7283>`
callbacks use the same
{repairsource}`runtime exhaustion predicate <src/smt/solver_runtime.cpp#L13-L17>`. They stop new propagation and conflict delivery
once the budget is consumed, while preserving any pending clause or reason
stream. The LRA aggregate increments at external-clause handoff, not when a
bound check first discovers an internal conflict. Repeated availability checks
for one pending clause do not increment it again. At exhaustion a complete SAT
candidate may unwind the backend; the runtime must replace that candidate with
`unknown` before any model or definitive solver answer is accepted.
