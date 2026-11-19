(research-register)=
# Open research questions

This is the current research index. Each entry names work that remains, even
when part of its baseline is already implemented. Current behavior and defaults
belong in the [capability overview](../capabilities.md) and implementation chapters;
completed, rejected and blocked investigations are indexed in
[evidence and investigation records](../evidence/index.md). Publication,
provenance and storage tasks belong in [project work](../work/merge-readiness.md).
An open lead is a question, not an implementation commitment.

## Remaining investigations

### Arithmetic

| Lead | Remaining question |
| --- | --- |
| [Arithmetic efficiency](arithmetic-efficiency-followups.md) | Which regrouping, flattening, normalization or numerical-repair change repays its analysis cost? The listed implementations and measurements define the baseline. |
| [Bounded-variable GCD reasoning](lia-extended-gcd.md) | Can bounds on non-fixed variables strengthen the existing fixed-row divisibility test? |
| [Integer patching](lia-patching.md) | Can an explicit target-window parameter and better candidate selection improve the existing repair heuristic? |
| [Integer infinitesimals](lia-infinitesimals.md) | Is a discrete-bound alternative useful on the still-unpromoted QF_UFLIA preparation path? QF_LIA primitive rows already address its default case. |
| [Arithmetic equalities](arithmetic-equality-exploitation.md) | Can selective equality delivery or equation-specific integer reasoning improve the already measured optional integration? |
| [Arithmetic symmetry](arithmetic-symmetry.md) | Which arithmetic symmetries can be recognized and broken soundly, with a measured benefit? |
| [Floating-point basis guidance](lra-floating-point.md) | Can an approximate LP solver suggest an exact starting basis cheaply enough to help? |
| [Complete equality basis](lra-equality-basis.md) | Would a complete explained arithmetic equality basis reduce shared-pair probe cost? |
| [Column coefficients](lra-column-coefficients.md) | Which representation makes column-to-row coefficient access cheaper without expensive pivot maintenance? |

Uniform cubes and ordinary hybrid splits are implemented; their original
checklists are [completed records](../evidence/index.md#integer-arithmetic).
Proof-derived combination splits, general octagon cube refinements, integer
elimination and termination-oriented bounding remain outside that subset; see
[proof-derived cuts](../papers/cuts-from-proofs.md) and the
[Bromberger reading guide](../papers/bromberger-linear-arithmetic.md).
Global parameter tuning remains open; the
[combination/default comparison](../evidence/qf-lia-defaults-20261119.md) does not
identify a universally best profile.

### Equality and theory combination

| Lead | Remaining question |
| --- | --- |
| [Theory-lemma generation and reuse](theory-lemmas.md) | What additional Ackermann coverage, scheduling or backend-retention policy is useful beyond the bounded implemented selector? |
| [Safe forgetting](theory-lemma-forgetting.md) | Under what progress and reason-lifetime conditions may theory clauses be forgotten? |
| [Care-pair generation](care-pair-generation.md) | Can fewer or cheaper equality suggestions preserve the current combination acceptance checks? |
| [Care-guided combination](care-guided-combination.md) | When may care information justify leaving shared equality questions unresolved? |
| [Real driver migration](real-combination-migration.md) | What is needed to move the dedicated QF_UFLRA coordinator onto the implemented general driver? |
| [Further model-based transitions](model-based-combination.md) | Which unimplemented transition of the broader model-based framework is worth integrating? |
| [Shared EUF structure](shared-euf-structure.md) | Can query-independent EUF preparation be shared without weakening per-query ownership? |

The conflict-selected Ackermann prototype, demand atoms and symmetry repair
already exist. Their [completed investigations](../evidence/index.md#euf-and-theory-combination)
retain qualifications and regressions; they are not outstanding implementation tasks.

### Search and quantified input

| Lead | Remaining question |
| --- | --- |
| [Search diversification](search-diversification.md) | Can a sound restart or phase policy help beyond the rejected bounded diagnostics? |
| [Configuration portfolios](portfolio.md) | Can time-slicing whole configurations recover complementary solves? No portfolio is implemented by the phase diagnostics. |
| [Instantiation efficiency](quantifier-instantiation.md) | Which ordering, caching or matching improvement helps the current enumerator? |
| [Quantifier NNF](quantifier-nnf.md) | When should optional negation-normal-form rewriting run, and what does it cost? |
| [Directly asserted quantifiers](asserted-quantifiers.md) | Can fixed assertion truth values avoid unnecessary obligation branching? |
| [Quantifier relevance](quantifier-relevance.md) | Can irrelevant obligations be skipped while preserving the final-model contract? |

### Representation, lifetime and instrumentation

| Lead | Remaining question |
| --- | --- |
| [Library API](library-api.md) | Which ownership and external-consumer contracts are needed for embedding? |
| [Incremental memory](incremental-memory.md) | What is live versus retained, and which reclamation design preserves scoped state? |
| [Term storage](term-storage.md) | Which compact representation supports present and future payloads without lifetime regressions? |
| [Rational storage](rational-storage.md) | Can numerator/pointer overlap reduce size while preserving exact arithmetic and object lifetime? |
| [Diagnostic instrumentation](statistics-instrumentation.md) | How can optional diagnostic builds be separated from always-on control state? Typed-key migration and its boundary policy already exist. |
| [Large distinct assertions](compact-distinct.md) | Can a compact representation avoid up-front quadratic disequalities? |
| [Incremental equation elimination](incremental-equations.md) | How should substitutions and restoration work across later assertions and pop? |

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


## Original numbered questions

| Original lead | Current question and completion obligation |
| --- | --- |
| 1: arithmetic pure-theory filtering | Determine which arithmetic literals can be omitted under relevance/polarity conditions. The original premise was corrected; preserve that correction and prove validity at theory/model boundaries before implementing. |
| 2 and 6: decision ordering | Investigate backend-supported decision/justification choices. LRA cannot directly replace SAT's variable-selection policy. Define the layer boundary and measure effective decisions before attributing a speedup. |
| 4: adaptive early pruning | Investigate arithmetic check frequency from observed usefulness. Existing EUF propagation backoff is not evidence that the arithmetic proposal is implemented. |
| 5: ground relevance | Extend the [relevance literature](../papers/relevance-filtering.md) beyond the separate quantified-obligation lead. Prove that omitting an assertion remains sound under backtracking and model completion. |
| 8 and H6: difference logic | Design a dedicated graph solver and admission route before claiming `QF_IDL`/`QF_RDL` support. The old negative-cycle example is a future test, not an executable capability example. Static precision bounds require the graph algorithm's assumptions; they do not remove general rational overflow checks. |
| 11: unconstrained terms | Prove occurrence, invertibility, reconstruction and incremental-restoration conditions. A syntactic one-parent shortcut is not by itself a sound elimination criterion. |
| R1: recursion depth | Audit input-sized recursion throughout parsing, rewriting, purification, model construction and tactic execution/destruction. Iterative parsing does not establish whole-solver stack safety. Reproduce remaining paths at the current revision and add bounded regressions before repair. |
| 12b/12c: integer inference | The optional [QF_LIA cube test](lia-cube-test.md) has full M124 evidence and is enabled as uniform cubes in the [QF_LIA default profile](../evidence/qf-lia-defaults-20261119.md); retain integer-preserving elimination and stronger Diophantine methods as separate leads. Keep these distinct from the implemented GCD test, patching, cuts, bounded branching and conflict generalization. |
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

## Implemented or closed investigations

The [closed dispositions](../evidence/index.md#historical-lead-dispositions)
remain available for tracing earlier decisions. Remaining numbered questions
appear above, rather than in a second backlog. Recheck the current implementation, cite primary sources and define
correctness, measurement and rejection conditions before starting a lead.
