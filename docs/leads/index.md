# Research register

An open lead is a question with an implementation or measurement obligation,
not a promised feature. Recheck a historical profile before using it to rank
work. Each investigation must name the input set, correctness conditions,
observable mechanism, and result that would reject the proposal.

The dedicated lead pages in the navigation cover library lifetime, compact
storage, statistics, theory lemmas, arithmetic, combination, and quantifiers.
The [weighted LRA certificate proposal](lra-conflict-certificates.md) specifies
the next arithmetic export boundary, its exact checking rule and remaining
implementation obligations.
The [arithmetic-symmetry lead](arithmetic-symmetry.md) connects existing finite
integer choices with broader SMT and integer-programming symmetry research.
This register also accounts for the remaining numbered entries in the
{source}`original register <docs/open-leads.md>`. Their full dated reasoning,
negative results and evidence remain preserved; current dispositions follow.

## Remaining investigations

| Original lead | Current question and completion obligation |
| --- | --- |
| 1: arithmetic pure-theory filtering | Determine which arithmetic literals can be omitted under relevance/polarity conditions. The original premise was corrected; preserve that correction and prove validity at theory/model boundaries before implementing. |
| 2 and 6: decision ordering | Investigate backend-supported decision/justification choices. LRA cannot directly replace SAT's variable-selection policy. Define the layer boundary and measure effective decisions before attributing a speedup. |
| 4: adaptive early pruning | Investigate arithmetic check frequency from observed usefulness. Existing EUF propagation backoff is not evidence that the arithmetic proposal is implemented. |
| 5: ground relevance | Extend the [relevance literature](../papers/relevance-filtering.md) beyond the separate quantified-obligation lead. Prove that omitting an assertion remains sound under backtracking and model completion. |
| 8 and H6: difference logic | Design a dedicated graph solver and admission route before claiming `QF_IDL`/`QF_RDL` support. The old negative-cycle example is a future test, not an executable capability example. Static precision bounds require the graph algorithm's assumptions; they do not remove general rational overflow checks. |
| 11: unconstrained terms | Prove occurrence, invertibility, reconstruction and incremental-restoration conditions. A syntactic one-parent shortcut is not by itself a sound elimination criterion. |
| R1: recursion depth | Audit input-sized recursion throughout parsing, rewriting, purification, model construction and tactic execution/destruction. Iterative parsing does not establish whole-solver stack safety. Reproduce remaining paths at the current revision and add bounded regressions before repair. |
| 12b/12c: integer inference | Investigate integer-preserving elimination and stronger Diophantine/cube methods. Keep these distinct from the implemented GCD test, patching, cuts, bounded branching and conflict generalization. |
| 13: mixed Int/Real | Define typing/coercions, admission, preparation, combinations and models. Mixed-tableau APIs alone do not make `QF_LIRA` a frontend-only task or establish completeness. |
| 14: integer monotone elimination | Extend the Real transformation only after an integer reconstruction proof, strict-bound treatment and incremental restrictions are specified. Measure actual Int opportunities. |
| 16: remaining ITE work | CITE simplification is implemented; further compression/reconstruction proposals need separate paper mappings and measurements. Do not relabel the whole old proposal as completed. |
| 18: alternate backtracking | Compare the proposed arithmetic policy with current bounds-only rollback and the SOI exception first. Restoring/recomputing assignments is a choice with invariant and cost obligations, not an automatic missing feature. |
| 19: integer bounding transformations | Investigate termination on partially unbounded integer problems using the Bromberger thesis conditions. Current bounded search can return `unknown`; a branch budget is not a completeness argument. |
| 21: divisibility-aware `div`/`mod` | Current preprocessing expands the supported constant-divisor operations. Stronger divisibility reasoning needs tests and relevant inputs; the old zero-occurrence sample does not establish absence from every future corpus. |
| 22: remaining traversal scratch | Atom/preparation cursors were accepted. Reprofile remaining full-manager scratch initialization; preserve fresh scoped facts and count touched versus retained objects. |
| 23–25: retained UF costs | See [compact distinct](compact-distinct.md), [incremental equation elimination](incremental-equations.md), and [immutable EUF sharing](shared-euf-structure.md). |
| H4, H11 and residual arithmetic directions | Batched assignment updates, preprocessing fill-in selection and related rational/row-layout experiments remain hypotheses. Compare with the current implementation and retain the old rejected prototypes before proposing a replacement. |

Every row retains its original numbered section in the
[section inventory](../coverage/index.md). Missing primary sources block
implementation of the corresponding paper mechanism. None of these rows
authorizes a default change without fresh correctness and performance evidence.

## Reusable tactic probes

The [conditional-tactic interface](../implementation/tactics.md#conditional-predicates-and-probes)
already accepts custom predicates and named numeric/Boolean probes. The missing
piece is a useful shared catalogue beyond `assertion_count()` and `at_most()`;
production selection currently calls richer analyses directly.

- Expose suitable existing measurements as named probes: reachable-term count,
  conjunct structure, theory features, and finite-domain properties. Distinguish
  assertion roots, conjunct occurrences, and unique terms explicitly.
- Add reusable Boolean combinations and numeric comparisons. Give composed
  probes stable identities including their parameters; canonical printing
  currently records names, not the callable implementations. The numeric-limit
  formatting repair now uses locale-independent round-trip precision and a
  defined policy for signed zero and non-finite values; see
  [strategy identity](../implementation/tactics.md). Broader probe composition
  remains open. Historical fingerprints are unchanged in stored records.

- Express appropriate profile choices through conditional strategies, preserving
  logic admission, model reconstruction, and incremental restrictions. A size
  heuristic must not replace a solver's applicability check.
- Specify evaluation cost and lifetime. Read the current goal and executing
  context, avoid formula mutation, and define cache validity across transformed
  goals and different term managers. The existing
  {source}`reachable-term counter <src/smt/formula_metrics.cpp#L11-L32>`
  initializes scratch storage for the whole manager, so repeated scans can be
  costly even for small surviving formulas.

First expose existing decisions without changing defaults. Check probe values
and selected branches after transformation and splitting, then compare with the
current selectors on the supported UF and arithmetic benchmark sets. Record
probe cost, branch counts, answers, validated models, solved counts, and PAR2;
run the preprocessing fuzz gate before accepting behavior changes. Reject a
selection policy whose analysis cost outweighs its benefit or which regresses
correctness. No performance improvement is claimed for this proposal.

## Solver selection after preprocessing

Investigate selecting a solver from the features remaining in each transformed
goal, while retaining the declared logic and model-reconstruction obligations.
A logic declaration permits a fragment; it does not imply that every feature
of that fragment occurs in the assertions. Transformations can remove features
that were present initially.

Current handling is partial. The ground UF/Real
{source}`dispatcher <src/smt/solver_strategy.cpp>` examines preprocessed goals,
with additional checks for model requirements. Declared `QF_LIA` and `QF_UFLIA`
retain their respective integer routes. The
{source}`session <src/smt/solver_session.cpp>` checks whether quantifiers occur
in the current assertions, but routes surviving quantified input before ground
preprocessing; it does not generally simplify quantified input and then
redispatch it through the ground pipeline. The
{source}`frontend <src/smt/frontend.cpp>` rejects nonlinear logic declarations
and multiplication of two nonconstant terms before preprocessing. Consequently,
assertions such as $x=2$ and $x\cdot y>5$ cannot currently reach substitution
and become the linear obligation $2y>5$.

The investigation has two distinct scopes:

- Within currently admitted inputs, recheck solver applicability after
  transformations. Include mixed goals that become pure arithmetic, EUF, or
  Boolean, and integer goals whose remaining assertions need less machinery.
  Preserve reconstruction and interpretation of declared symbols even when
  their occurrences disappear from the surviving assertions.
- Separately, investigate admitting a broader, well-typed representation long
  enough to simplify it into a supported fragment. Examples include eliminating
  a quantifier with a constant body or substituting a known constant into a
  nonlinear product. This requires a design for frontend admission and safe
  preprocessing of those constructs; changing dispatch alone is insufficient.
  It must not pass residual quantifiers or nonlinear terms to an engine that
  assumes ground linear input, or silently accept violations of a declared
  logic's input contract.

Specify an applicability check at the solver boundary, including sort,
quantifier, arithmetic, and model requirements. Unsupported residual features
must produce an explicit unsupported or inconclusive outcome. Feature summaries
must be recomputed or invalidated after transformations and goal splitting;
retained sessions must also handle later assertions and scope changes that
reintroduce features. Broadening input support requires an explicit contract
and the relevant transformation proofs, not a claim of general nonlinear or
quantified solving.

Use focused examples for disappearing and surviving features, followed by
supported UF, LRA, LIA, UFLRA, and UFLIA corpora and separately identified
broader-fragment seeds. Check answers against references and validate SAT
models against the original assertions. Record features before and after
preprocessing, selected engines, applicability failures, classification cost,
solved counts, and PAR2. Run correctness and timing campaigns in SLURM. Reject
unsound routing or lost reconstruction; retain existing defaults if the added
analysis or changed search causes a performance regression. This is an open
design and evaluation lead, with no new input support or default change claimed.

## Implemented or closed investigations

| Original entry | Disposition |
| --- | --- |
| 2b: dispatch regression | Repaired at the recorded historical revision; retain the diagnosis in the evidence archive. |
| 3: least-violated leaving rule | Implemented as an option; the original comparison did not justify promotion. See [LRA](../implementation/lra.md). |
| 3b and 7: occurrence/row layout | The current LRA chapter describes the implemented sorted rows and occurrence maintenance. Hash-set and earlier indexed-removal prototypes remain rejected history. The column-to-row direction remains open as [column-to-row coefficient access](lra-column-coefficients.md). |
| 9: Real monotone elimination | Implemented, optional. Later full-corpus measurements differ from the early small-sample rejection; see [arithmetic evidence](../evidence/arithmetic.md). |
| 10: model-unit equality producer | Implemented as an optional producer. Legacy remains default; this is not an unimplemented algorithm. |
| 12a/12f | Integer-safe preprocessing and patching have current explanations. The old claim that all integer preprocessing is disabled is superseded by the route-specific policies. |
| 12d: integer conflict generalization | Implemented and enabled by default. {ref}`Integer explanations <lia-explanations>` collect the reasons used by the search; {source}`generalized_lia_lemma <src/smt/solver_engines.cpp#L8334-L8360>` checks the result and retains the coarse blocking clause as a fallback. |
| 15: bridge atoms | Removed. Do not restore it as an available alternative or open migration task. |
| 20: model-phase evaluation | Evaluated on arithmetic divisions. Model-phase hints default on for declared QF_LRA rebuild queries with automatic preprocessing, and off on other routes. See {ref}`automatic LRA heuristics <automatic-lra-heuristics>` for the policy and its qualification. |
| Shared assignment ownership and rollback markers | Implemented/repaired; theory undo logs remain separate. See [DPLL(T)](../implementation/dpllt.md). |

## Low-level and cross-cutting leads

SAT literal exhaustion remains a low-priority hardening question: specify the
backend-compatible limit, reject before overflow, and test negation/indexing
boundaries with an artificial small capacity. No overflow was observed in the
documentation review. Keep compact literal storage while investigating; see
[SAT representation](../implementation/sat.md).

General polarity-aware encoding remains open beyond the existing root handling;
its use conditions, incremental upgrades and theory/model obligations are in
[the literature note](../papers/polarity-aware-cnf.md). The rejected root-only
prototype is not the same experiment.

Unresolved experiment identities and checking coverage belong in
[evidence work](../work/evidence.md), not in the implemented manual.
