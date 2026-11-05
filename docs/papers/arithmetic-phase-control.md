# Arithmetic-guided phase control

**Reading scope: 2026-11-05.** The current investigation is restricted to
preferred truth values for existing Boolean atoms. It does not introduce
numeric trial assumptions, model-value branching, new theory atoms, or a new
restart schedule. The CLI rename changes neither this mechanism nor defaults.

## The relevant literature

### Goldwasser, Strichman and Fine (FMCAD 2008)

Dan Goldwasser, Ofer Strichman and Shai Fine,
[*A Theory-Based Decision Heuristic for DPLL(T)*](https://www.cs.purdue.edu/homes/dgoldwas/downloads/papers/GSF_fmcad_2008.pdf),
FMCAD 2008 {cite:p}`goldwasser2008theorydecision`, Sections I, II-A, III-C/III-D
and IV, is directly relevant to
phase-only control. The full author-hosted paper was read for this note.
It leaves Boolean variable selection with the SAT solver and uses arithmetic
information to select the variable's truth value.

The paper obtains an approximate recommendation by evaluating a predicate at
the assignment maintained by simplex. Because consistency checks can be
delayed, that assignment need not satisfy the current partial trail. Its
value therefore supplies advice, not a theory implication. An optional
bounded number of pivots attempts to improve the approximation; among the intermediate
assignments, their implementation keeps the one satisfying most bound
predicates. In their reported LRA evaluation, extra pivots were generally not
cost-effective and the zero-extra-pivot variant performed best overall.
Those are historical results for their implementation, not a Primo speedup
or evidence about the mixed integer/EUF `wisas` cases.

Required state is the Boolean-to-arithmetic predicate mapping, the current
simplex assignment, and, for the pivoted variant, a bounded work counter and
the preferred intermediate assignment. Predicate evaluation has a finite
cost proportional to the representation inspected, including arithmetic
operand sizes. The paper gives no performance guarantee for a new Primo
phase policy. Primo already has prepared bounds and a `DeltaRational`
assignment; `--arithmetic-model-phase=on` already implements this core
existing-assignment, zero-extra-pivot idea on routes that produce phase hints.
"Zero extra pivots" means no pivots performed for the phase recommendation;
ordinary feasibility checks can still pivot. Primo's cached preference delivery
differs from consulting the theory at the decision point, as described below
and in the [implementation documentation](../implementation/lra.md#model-phase-hints).
The pivoted variant is not implemented or selected for this investigation.

### SPASS-SATT (CADE 2019), accessible footnote 3

Martin Bromberger, Mathias Fleury, Simon Schwarz and Christoph Weidenbach,
[*SPASS-SATT: A CDCL(LA) Solver*](https://doi.org/10.1007/978-3-030-29436-6_7),
CADE 27, 2019, pp. 111–122, describes decision recommendations. The
publisher's accessible **footnote 3** states that `GetPhase(L)` recommends
true if the current theory assignment satisfies `L`, or asserting its
negation would be inconsistent; otherwise it recommends false. With the
stated unate propagation enabled, the second case has already propagated.
This note checked that footnote directly. The full chapter was not newly
retrieved: the publisher provides subscription access and the linked HAL
copy was blocked. The two-page competition description confirms the feature
but is not a substitute for the chapter's detailed algorithm.

For the present phase-only scope, the useful connection is advice from an
existing assignment. It does not require introducing a numeric assumption.
The footnote does not establish a heuristic that minimizes integer values,
prefers a bound, or escapes the particular `wisas` region.

## Mapping to Primo

| Operation or state | Current owner and status |
|---|---|
| Initial theory-atom preference | CLI `--initial-theory-phase`, compatibility alias `--theory-phase`; stored in `CheckSatOptions::theory_phase`. QF_UF and dedicated QF_UFLRA install it, with different `auto` rules; pure QF_LRA rebuild/assumptions and shared-driver QF_LIA/QF_UFLIA do not. See [route coverage](../reference/policies.md#initial-phase-hints). |
| Arithmetic phase policy | CLI `--arithmetic-model-phase=on|off|auto`; stored in the existing `std::optional<bool> CheckSatOptions::lra_model_phase`. The name changes, not the policy. |
| Current arithmetic assignment | `ComponentLraExecution` on the online integer route; the dedicated LRA path also has arithmetic phase production. The former requires online components and incremental EUF. |
| Atom evaluation and refresh | `model_phase_literal` and `refresh_model_phase_atom` evaluate a prepared bound at the current assignment, then publish its sign. No implication is learned by this operation. |
| SAT variable choice and preference storage | SAT keeps variable selection. `CadicalBackend::phase` forwards the sign to CaDiCaL's phase API. This persistent preference is not an immediate per-decision `GetPhase` callback. |

The {phaseclisource}`CLI parsing <src/main.cpp>` owns the requested policy
string until parsing finishes, validates its three values, and converts it
to the existing optional Boolean. `auto` preserves query-time policy
resolution. Canonical and legacy arithmetic forms cannot be mixed; the old
positive/negative pair retains its negative-flag precedence.
On the component path,
{settingssource}`ComponentLraExecution <src/smt/solver_engines.cpp#L8803>`
owns a live tableau copy but borrows the preparation and atom mapping.
{settingssource}`LiaComponentAdapter <src/smt/solver_engines.cpp#L9757>` owns
`PreparedLraTheory`; `LiaTheoryComponent` owns that adapter, its execution and
the theory-atom vector passed to the execution. Atom registration destroys
the execution before extending the backing state, then recreates it, so those
borrows remain valid during phase evaluation. The
{settingssource}`SAT adapter <src/sat/cadical_backend.cpp>` owns backend phase
delivery. Internal policy fields and statistics retain their old names.

Primo's current implementation matches the broad idea of evaluating an atom
at an arithmetic assignment. It does not implement the complete 2008
heuristic variants or SPASS-SATT's callback protocol. Cached preferences may
be stale before the next decision; `phase` persists until updated, and Primo
currently exposes no matching `unphase` operation. Existing evidence does not
establish staleness as the cause of the `wisas` regression.

## Termination and progress obligations

A proposed phase heuristic must satisfy these project constraints:

- Each recommendation computation terminates with bounded work. It does not
  search an unbounded integer interval for a preferred target.
- It sets only preferences on existing SAT atoms. It creates no new numeric
  assumptions, permanent restrictions, atoms or outer model-retry loops.
- SAT retains variable selection, propagation, learning and backtracking.
  A preference cannot override a forced literal or serve as a proof reason.
- Existing theory checks and their explanations remain responsible for
  correctness. A poor or stale recommendation is never promoted to an
  entailed literal.
- The implementation must preserve the backend's existing progress and
  resource-control contracts; changing a phase is not itself a proof that
  the entire solver terminates or decides every supported input.

Section II-A of the 2008 paper separately discusses finite atom sets and
progress requirements on theory deduction/lemmas. Those requirements must
not be confused with permission to turn approximate phase advice into
propagation. For Primo's bounded integer solver, a resource limit still
returns `unknown`; this investigation makes no new completeness claim.

## Next experiment and unsupported extrapolations

Keep variable selection and the asserted problem fixed. First measure whether
the existing arithmetic preferences actually change the early threshold
polarities on the {ref}`four traced wisas files <integrality-evaluation>`.
On `xs_28_48`, that investigation found that following the current model can
reinforce the difficult region;
retesting that alone is not a new heuristic.

A different rule favoring small-magnitude values or values near a bound
remains an original proposal requiring a design and evaluation. The papers
above do not establish that rule for QF_UFLIA. In particular, a phase-only
heuristic cannot force `t = 12` while the current trail fixes `t = 17`; it can
only influence subsequent choices, including choices reached after normal
backtracking. No temporary numeric-assumption experiment is part of the
current plan.

Regression coverage for the rename checks `on`, `off`, explicit and implicit
`auto`, preprocessing-dependent resolution, legacy aliases and precedence,
invalid values and mixed forms. Those checks verify policy preservation;
they do not demonstrate a new search improvement. Any later phase-policy
implementation needs the applicable correctness gate and paired full-corpus
evaluation before changing defaults.
