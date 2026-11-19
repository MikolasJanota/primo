# Supported input and capabilities

Start with [building and running](guide/getting-started.md), then the
[input](guide/input.md), [model](guide/models.md), and [session](guide/sessions.md)
guides. This page describes implemented routes; historical experiment counts
do not extend their supported scope.

## Supported routes

| Logic | Solving scope | Printed models | Session engines |
| --- | --- | --- | --- |
| `QF_UF` | Equality and uninterpreted functions | Yes | Rebuild; opt-in assumptions |
| `QF_LRA` | Linear Real arithmetic | Yes | Rebuild; opt-in assumptions |
| `QF_LIA` | Linear Int arithmetic with private/SAT integer branching | Yes | Rebuild |
| `QF_UFLRA` | UF with linear Real arithmetic | Yes | Rebuild |
| `QF_UFLIA` | UF with linear Int arithmetic | Yes | Rebuild |
| `UF` with quantifiers | Enumerative instantiation; restricted SAT completion | No for quantified formulas | Rebuild |

Admission is checked by {source}`SmtSession <src/smt/smt_session.cpp#L271-L289>`;
the actual solver route is selected after preprocessing. A formula can simplify
to a Boolean or pure-theory problem despite its declared mixed logic.
The Real combination route retains a dedicated coordinator, while Int
combination and quantified UF use the general component driver. See
[architecture](implementation/architecture.md) and
[combination](implementation/combination.md). The
[DPLL(T) ownership explanation](implementation/dpllt.md#shared-assignment-ownership)
describes the shared assignment record used by these routes and the theory
state each solver must undo separately.

## Implemented mechanisms and defaults

This table distinguishes available code from default policy. The linked
implementation/reference pages own the full contracts and option dependencies;
[dated investigations](evidence/index.md) own measurements and acceptance limits.

| Area | Implemented now | Default or remaining boundary |
| --- | --- | --- |
| [Integer completion](implementation/lia.md) | Patching, GCD checks, cuts, uniform/difference cubes and ordinary hybrid splits. | QF_LIA uses **M124 + uniform cubes + hybrid branching**. QF_UFLIA retains private branching and ignores cube controls. Proof-derived combination splits remain unimplemented. |
| [Arithmetic propagation](implementation/lra.md) | Same-variable propagation, optional forward/reverse row propagation, repaired notifications, queue deduplication and scan reuse. | Row modes and unconditional preprocessing bounds remain off. Optional repairs/filters are implemented, not outstanding proposals. |
| [Bounds before search](implementation/preprocessing.md) | Read-only opportunity diagnostics and an experimental bound-strengthening pass. | Diagnostics install no constraints. The strengthening pass is opt-in; its original blocked qualification is preserved in [the record](leads/preprocessing-bound-tightening.md). |
| [EUF](implementation/euf.md) | Congruence checking/propagation, mechanical cost fixes, bounded conflict-selected clauses and optional demand equality atoms. | Conflict Ackermann generation/demand remain off; demand has measured regressions. The symmetry-composition correctness repair is implemented. |
| [Integer equality export](implementation/uflia-equality-propagation.md) | Legacy and model-unit discovery with explained, selectable delivery. | Export is off; legacy remains the producer default. Selective delivery and equation-specific integer reasoning retain [open questions](leads/arithmetic-equality-exploitation.md). |
| [Statistics](reference/statistics.md#typed-instrumentation-policy) | Typed IDs/numeric counters in recurring solver work and a checked cold string-key boundary. | A build-time diagnostic switch and full control/diagnostic separation are still [proposals](leads/statistics-instrumentation.md). |

M124 enables integer-bound rounding, primitive rows and integral patch moves,
with explicit integer-slack obligations off. See the
{qfliadefaultsource}`route policy <src/smt/lia_settings_policy.cpp>` and
[effective options](reference/policies.md). An implemented opt-in experiment is
available code, not an unimplemented feature or a promise of a speedup.

## Limits

Integer search can return `unknown` on resource exhaustion. Hybrid mode may
hand a private-budget exit to SAT instead; finite local budgets do not establish
global termination or completeness.
Quantified UF can prove contradictions but has no general SAT-completeness
claim. Its supported SAT cases and inconclusive boundary are described in
[quantifiers](implementation/quantifiers.md).

Arrays, bit-vectors, nonlinear arithmetic, quantified arithmetic, general mixed
Int/Real input, and proof production are not implemented. `QF_IDL` and `QF_RDL`
are not admitted as separate logic names. Integer `div`, `mod`, and `abs` are
accepted extensions; division and remainder require a nonzero literal divisor.
These extensions are not a claim of strict linear-logic grammar conformance.

`get-value`, `get-unsat-core`, `get-proof`, `check-sat-assuming`, and global
declarations are unsupported. The [frontend](implementation/frontend.md)
documents character extensions and other lexical limitations.

Models require enabling production before selecting the logic, then a current
SAT result. Context changes invalidate the session's current model. Internal
model checking and independent validation are separate operations.

## Qualification and maintenance

The [publication qualification](evidence/publication.md) records the checks of
the publication candidate. Earlier results, including the
[assignment migration](evidence/assignment.md), have their own evidence pages.
Measured revisions, commands, resources, failures and artifacts belong in those
records and the [run ledger](run-log.json). They do not qualify every later
checkout or option.

Use [options](reference/options.md) for defaults, and
[correctness requirements](development/correctness.md) when changing behavior.
Source revision metadata belongs to the [review ledger](review.md); it is not
repeated in each chapter's prose.
