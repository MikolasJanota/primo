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

### Follow-up: Boolean targets and small arithmetic values

Armin Biere and Mathias Fleury,
[*Chasing Target Phases*](https://fmv.jku.at/papers/BiereFleury-POS20.pdf),
POS 2020, Sections 2, 3.1 and 4, were read for this follow-up. Their targets
are partial **Boolean assignments**, retained to guide later decision signs.
Rephasing changes preferred signs without changing CDCL's logical rules.
The required state includes saved signs and a partial target-sign array;
their target update criterion uses trail length. This supplies a precedent
for sign preferences, not a numeric slack objective. Primo already delegates
SAT search to CaDiCaL; this note adds neither the paper's target machinery nor
a rephasing/restart schedule. Its policies must not be inferred from the
paper's 2020 defaults for our current backend version.

Bohan Li and Shaowei Cai,
[*Local Search For SMT On Linear and Multi-linear Real Arithmetic*](https://repositum.tuwien.at/bitstream/20.500.12708/188824/1/Li-2023-Local%20Search%20For%20SMT%20On%20Linear%20and%20Multilinear%20Real%20Arithmetic-vor.pdf),
FMCAD 2023, Sections III–V, were also read. They choose numeric moves by
clause score, then break ties by smaller denominator and smaller absolute
value. Their state includes a complete candidate assignment, clause weights
and sampled interval moves; candidates can falsify clauses. This is local
search, not a phase rule over a maintained simplex model. It is adjacent
evidence for a small-value preference, not validation of a QF_UFLIA slack
heuristic. That architecture and its move scoring are not adopted here.

This focused reading found no directly matching published rule for Primo's
proposed low-slack phase preference. It does not establish an absence from
the wider literature. Neither source supplies a complexity or performance
guarantee for that proposed adaptation.

### Follow-up: combining phase criteria

Arijit Shaw and Kuldeep S. Meel,
[*Designing New Phase Selection Heuristics*](https://www.cs.toronto.edu/~meel/Papers/sat20-sm.pdf),
SAT 2020, Sections 4–5, were read for this follow-up. Decaying Polarity Score
(DPS) maintains one decaying signed polarity-history score per variable.
Literal State Independent Decaying Sum (LSIDS) instead maintains
activity for each literal, combining learned-clause and canceled-assignment
bumps with decay/rescaling. They found no aggregate DPS improvement; LSIDS
helped when used after chronological backtracking, while using it after both
backtracking types hurt. This is evidence for context-dependent selection,
not a tested blend with arithmetic advice. Neither score is implemented here.
An adaptation needs access to its update events and score storage; it cannot
be inferred from aggregate conflict counts alone.

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

### What a preference for smaller slack values would mean

The simplest phase-only candidate needs no numeric target and no change to
the tableau assignment. Suppose existing atoms include $t\le13$ and $t\ge16$.
When SAT selects either atom for a decision, recommending true for the former
and false for the latter favors the smaller-value side of those thresholds.
Only if SAT uses the recommendation does the chosen literal enter the trail,
through ordinary DPLL(T) decision handling. No equality $t=12$ is installed,
and the preferences need not describe a jointly feasible model. Forced
literals override preferences, and normal conflict analysis and backtracking
can explore the opposite side.

This is a proposed sign rule, not the implemented current-assignment rule of
`model_phase_literal`. A later implementation would need the prepared
predicate's semantic direction and its mapping to the SAT literal; choosing
the same SAT sign for every atom would confuse upper and lower bounds.
A scan over a finite set of prepared predicates would take bounded work
proportional to the predicates inspected, with arithmetic normalization costs
accounted for separately. It would retain SAT's variable choice and add no
atoms, assumptions or numeric retry loop. It is not implemented or qualified.

"Smaller" also differs from "smaller absolute value": preferring $t<0$ can
reduce $t$ while increasing its magnitude. A raw slack's sign and scale depend
on preparation, so the semantic expression and intended direction must be
identified before testing a policy. Existing thresholds only distinguish
regions; phase preferences cannot enumerate or select arbitrary integers
inside a region without additional branching structure.

Changing the simplex starting assignment is a separate proposal. A basic
slack is determined by its row and cannot be independently overwritten;
{settingssource}`changing nonbasic values <src/theory/arithmetic/lra_tableau.cpp#L1463-L1468>`
requires {settingssource}`updating dependent basic values <src/theory/arithmetic/lra_tableau.cpp#L97-L143>`,
and may require feasibility repair. Direct threshold preferences avoid that operation.
Neither alternative can move $t$ from 17 to 12 while the current trail forces
17. For the phase candidate, an effect requires an unforced threshold choice
before that commitment, or a later choice after normal backtracking releases
it. The papers above do not establish a QF_UFLIA speedup for either proposal.

### Adaptive overrides of the current-assignment preference

When enabled on a route producing arithmetic hints, the current policy
evaluates each supported inequality at the current tableau assignment $\beta$
whenever it refreshes its
preference. Having a value for every tableau variable supplies a sign; it
does not establish that this sign leads to an inexpensive integer/EUF branch.
Equality atoms are skipped, and forced literals are not phase choices.
There is currently no competing arithmetic score in this policy.

A proposed blend for an atom $a$ could use

$$S_k(a)=w_k b_\beta(a)+(1-w_k)h_k(a),\qquad 0\le w_k\le1,$$

where $b_\beta(a)\in\{-1,+1\}$ is the current-assignment preference and
$h_k(a)\in[-1,1]$ is a separately defined, normalized alternative score.
Here $k$ indexes a bounded policy refresh; positive scores prefer true and
negative scores prefer false, with ties falling back to $b_\beta$. A history
score or the semantic low-threshold preference above could be considered,
but these alternatives measure different things and are not interchangeable.
Raw arithmetic values or distances from thresholds are not calibrated
confidence scores and depend on expression scaling.

If both inputs are only signs and $w$ is fixed, every disagreement is won by
the same criterion: $w>1/2$ selects $\beta$, $w<1/2$ selects the alternative,
and $w=1/2$ is a tie. Adaptation therefore needs changing weights, graded
scores, or an explicit mixture that chooses which policy to consult. A
discrete gate is another candidate. Repeated visits to the same threshold
pattern could trigger a bounded alternative-policy window, but choosing the
trigger, duration and recovery rule remains design work. One conflict does
not show that the selected phase was bad; learning can represent progress.
Neither the blend nor a gate is implemented or validated.

The {settingssource}`current phase-output path <src/smt/solver_engines.cpp#L9242-L9301>`
publishes persistent signs rather than receiving the selected SAT variable
at every decision. Adaptive-state changes must trigger refreshes even if
$\beta$ has not changed, and delivery must be measured at actual decisions.
The {settingssource}`backend interface <src/sat/ipasir_up.hpp>` exposes phase
installation but no phase withdrawal, saved/target-phase query or complete
learned-clause event stream. Simply ceasing updates does not withdraw an
already installed preference. Returning control to native SAT phasing needs
an explicit release operation and backend-contract checks; publishing an
opposite sign is an override, not a release. The installed CaDiCaL header
provides `unphase`, but Primo does not yet expose it through its adapter.
All proposed variants retain existing atoms, SAT variable selection and
proof rules, and introduce no numeric assumptions or integer-value loop.

Regression coverage for the rename checks `on`, `off`, explicit and implicit
`auto`, preprocessing-dependent resolution, legacy aliases and precedence,
invalid values and mixed forms. Those checks verify policy preservation;
they do not demonstrate a new search improvement. Any later phase-policy
implementation needs the applicable correctness gate and paired full-corpus
evaluation before changing defaults.
