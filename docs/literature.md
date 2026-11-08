# Literature and implementation mapping

The [bibliography](references.md) identifies publications; this page explains
where their algorithmic content belongs. A citation or reading note does not
establish that an entire algorithm is implemented. The owning implementation
chapter states the actual subset, deviations, invariants and tests.

## SAT, EUF, and Boolean structure

| Source or topic | Current role and retained reading note |
| --- | --- |
| IPASIR-UP | [SAT integration](implementation/sat.md) owns callbacks, reasons and the adapter boundary. The {source}`interface reading note <docs/papers/ipasir-up.md>` retains the paper checklist; Primo delegates propositional search. |
| Nieuwenhuis–Oliveras, fast congruence closure | [EUF](implementation/euf.md) maps flattening, congruence, explanation and rollback to Primo. The {source}`algorithm note <docs/papers/fast-congruence-closure.md>` and {source}`implementation comparison <docs/papers/fast-congruence-closure-implementation.md>` preserve optimizations not adopted wholesale. |
| CVC4 architecture | [Combination](implementation/combination.md) explains care pairs and the general component interface. The {source}`reading note <docs/papers/cvc4-architecture.md>` is architectural context, not a copied implementation. |
| Yices | [Survey](papers/yices.md): dynamic Ackermannization and theory-lemma reuse are distinct from Primo's existing bounded propagators. |
| Clause forgetting | [Survey](papers/clause-forgetting.md): logical redundancy, reason lifetime and termination are separate obligations. |
| Learned-clause minimization and all-UIP shrinking | [Survey and probe design](papers/conflict-explanation-minimization.md): Sörensson–Biere 2009, Feng–Bacchus 2020 and Fleury–Biere 2021 distinguish literal deletion from replacement by shared antecedents. Backend configuration and benefit remain to be measured. |
| Plaisted–Greenbaum and SMT encodings | [Survey](papers/polarity-aware-cnf.md): direction-sensitive definitions remain an open extension; the rejected root-only prototype is retained history. |
| Relevance and don't-cares | [Survey](papers/relevance-filtering.md) and [quantifier relevance](leads/quantifier-relevance.md): deciding which obligation matters differs from filtering instances of an active obligation. |

## Arithmetic papers and theses

[Arithmetic-guided phase control](papers/arithmetic-phase-control.md) reviews
polarity-only recommendations, their mapping to Primo, and the termination
obligations for the current investigation. Its follow-up distinguishes Boolean
target phases and small-value local search from an unimplemented preference
for the lower-valued side of existing slack thresholds, and reviews polarity
scores as context for adaptive overrides of arithmetic advice. The
{ref}`narrow-region candidate <narrow-region-phase-candidate>`
preserves β generally and proposes measuring repeated entry into small integer
ranges before selecting local overrides. It is not implemented or evaluated.
Numeric trial assumptions and model-value branching are outside its scope.
Its [CSP follow-up](papers/arithmetic-phase-control.md#csp-connections-and-the-finite-domain-limitation)
distinguishes least-constraining values, Refalo's impact-based search and
counting-based solution density, including the finite-domain limits on transfer.
The supplied [Baptista thesis overview](papers/csp-restarts-phase-control.md)
adds finite-domain context, negative split-point results and randomized branch
orientation as related work, with no policy adoption.

| Source | Current role and retained reading note |
| --- | --- |
| Arithmetic symmetry | [Open lead and reading scope](leads/arithmetic-symmetry.md): SyMT detection results, CVC4-SymBreak, and related LP/ILP work. These motivate investigation beyond the current finite-choice pass; they do not establish a QF_LIA/QF_LRA speedup for Primo. |
| Dutertre–de Moura, integrating simplex with DPLL(T) | [LRA](implementation/lra.md) covers preparation, bound repair, strict arithmetic, conflicts and rollback. The {source}`base-paper checklist <docs/papers/fast-linear-arithmetic-dpllt.md>` records departures and later optional methods. |
| Tim King, *Effective Algorithms for the Satisfiability of Quantifier-Free Formulas Over Linear Real and Integer Arithmetic*, PhD thesis, 2014 | LRA sparse operations, finite fallback, SOI and conflict construction. [The chapter's source table](implementation/lra.md#sources-and-implementation-status) distinguishes Primo's representation from the complete thesis designs. |
| Alberto Griggio, *An Effective SMT Engine for Formal Verification*, PhD thesis, 2009 | Sparse engineering, violation selection and early-pruning context. The {source}`thesis note <docs/papers/griggio-mathsat-thesis.md>` retains exact sections and the corrected least-violated attribution. |
| Martin Bromberger, *Decision Procedures for Linear Arithmetic*, PhD thesis, 2019 | [Reading guide](papers/bromberger-linear-arithmetic.md), [LIA](implementation/lia.md) and [preprocessing](implementation/preprocessing.md). Optional pure QF_LIA uniform/difference cubes are implemented from the CAV 2024 description; see {ref}`lia-cubes`. Complete equality bases, mixed cubes and bounding transformations remain comparisons/leads. The {source}`original thesis checklist <docs/papers/bromberger-linear-arithmetic-thesis.md>` retains the full intake. |
| Monniaux, CAV 2009 | LRA normalization, direct bounds and sparse-pivot motivation. [Floating-point assistance](leads/lra-floating-point.md) is unimplemented; leading-coefficient-one normalization differs from the paper's integer normalization. |
| King–Barrett–Dutertre, SOI | LRA's optional SOI section states the implemented objective, breakpoint method, conflict explanation and finite fallback; it does not describe the default sparse repair loop. |
| King, ordinary-conflict strengthening and provenance | [Missing-section intake](papers/conflict-explanation-minimization.md#king-ordinary-simplex-conflict-strengthening-and-provenance): thesis §§2.2.4 and 2.2.9 describe regression to assertions and greedy bound weakening. These are not implemented as general LRA explanation transformations; SOI row reduction is separate. |
| Bjørner–Nachmanson, *Arithmetic Solving in Z3* | LRA's optional model-unit producer, LIA's selected patching/GCD/cut mechanisms, and the optional pure QF_LIA uniform/difference cube test (§4.2); see {ref}`lia-cubes`. The {source}`paper map <docs/papers/arithmetic-solving-in-z3.md>` preserves broader unimplemented integer/nonlinear procedures. |
| Dillig–Dillig–Aiken, *Cuts from proofs* (2011 journal version) | [Reading note](papers/cuts-from-proofs.md): HNF certificates, general integer splits, coefficient bounds and the bounded-region termination argument. Deferred; current Gomory cuts and coordinate DFS do not implement this procedure. |
| Word GCD and integer divisibility | [LIA](implementation/lia.md) states row integrality preconditions; {source}`word-GCD notes <docs/papers/word-gcd-notes.md>` are an engineering comparison, not the origin of the mathematical divisibility test. |
| Integer division and modulo | [LIA](implementation/lia.md) and [preprocessing](implementation/preprocessing.md) describe the constant-divisor subset; the {source}`reading note <docs/papers/integer-division-and-modulo.md>` retains excluded variants. |
| LP/MIP presolve and primal heuristics | [Repair survey](papers/lia-primal-heuristics.md), [patching lead](leads/lia-patching.md), and {source}`monotone-elimination note <docs/papers/monotone-variable-elimination.md>` keep rounding/repair and elimination obligations distinct. |
| Achterberg et al., *Presolve Reductions in Mixed Integer Programming* {cite:p}`achterberg2020presolve` | Taxonomy of MIP presolve reductions. GCD row reduction (§3.5), integer-bound rounding (§4.2) and implied-integer detection (§7.6) are mapped in the [integrality checklist](papers/mip-presolve-integrality.md) and the [infinitesimal-avoidance lead](leads/lia-infinitesimals.md). Primo implements fragments: `--lia-primitive-int-rows` stores `Int` rows with coprime integer coefficients, and `--lia-round-integer-bounds` rounds non-strict fractional thresholds on integer-valued variables; both are on by default for QF_LIA and off for QF_UFLIA. The separate early atom canonicalizer is off by default and does not round the right side. |
| Atom canonicalization | Current normalization is explained in preprocessing and LRA; {source}`the comparison note <docs/papers/lra-atom-canonicalization.md>` retains rejected/default-off policy evidence. |
| Difference constraints | {source}`Reading note <docs/papers/difference-constraint-propagation.md>` motivates a future dedicated route. It is not implemented difference-logic admission. |

The {source}`arithmetic related-work index <docs/papers/lra-related-work.md>`
retains additional comparisons. [Arithmetic evidence](evidence/arithmetic.md)
separates implementation choices from the measurements that accepted or rejected
them. [Primo's published results](papers/primo-qflra-results.md) have their own
configuration and unresolved artifact mapping.

## Preprocessing, strategies, and combination

| Source/topic | Current role and retained note |
| --- | --- |
| de Moura–Passmore, *The Strategy Challenge in SMT Solving* | [Tactics](implementation/tactics.md) maps §3 to Primo's formula-valued composition and model conversion, with explicit limits. |
| FastSMT and tactic configuration | The {source}`FastSMT note <docs/papers/fastsmt-strategy-synthesis.md>` and {source}`configuration evidence <docs/papers/tactic-configuration-evidence.md>` motivate future strategy selection. Primo has no implemented learned strategy system or parallel tactic scheduler. |
| Symmetry breaking and value precedence | [Preprocessing](implementation/preprocessing.md) states the finite-domain/permutation conditions. The {source}`symmetry note <docs/papers/smt-symmetry-breaking.md>` and {source}`finite-model note <docs/papers/finite-model-finding-sorts.md>` retain broader comparisons; Primo is not a general finite-model finder. |
| Nelson–Oppen closure methods | [Combination](implementation/combination.md) explains shared variables, equality exchange and completeness assumptions; the {source}`closure note <docs/papers/nelson-oppen-closure-methods.md>` is the retained algorithm intake. |
| de Moura–Bjørner, model-based combination | The {source}`reading note <docs/papers/model-based-theory-combination.md>` and current combination chapter distinguish model mutation, guessed equalities, explanations, and exact fallback. This is separate from Ge–de Moura MBQI. |
| MathSAT5; Bjørner–Fazekas incremental preprocessing | [Sessions](implementation/sessions.md) maps labels and safe preprocessing restrictions. Restoration/replay algorithms remain [open work](leads/incremental-equations.md). |

## Quantifiers, syntax, and testing

Reynolds, Barbosa and Fontaine's enumerative-instantiation results and Janota,
Barbosa, Fontaine and Reynolds's tuple-order work are mapped in
[quantifiers](implementation/quantifiers.md). The
{source}`enumeration note <docs/papers/enumerative-instantiation.md>` and
{source}`tuple-order note <docs/papers/fair-adventurous-enumeration.md>` preserve
their reading checklists and historical evaluations. Entailment filtering,
fail masks, equality representatives, model-refining filters and Ge–de Moura
MBQI are distinct proposals in [quantifier leads](leads/quantifier-instantiation.md).
The old statement that an `unknown` from one order is logically “weaker” is
superseded: every `unknown` is inconclusive; fairness concerns the unbounded
enumeration procedure, not a hierarchy of bounded `unknown` answers.

The chapter also maps Andrew Reynolds's 2013 thesis, *Finite Model Finding in
Satisfiability Modulo Theories*, §§3.1.1–3.1.2, and Haniel Barbosa's 2017 thesis,
*New Techniques for Instantiation and Proof Production in SMT Solving*, §5.1
and §§7.1.1–7.1.2. These supply the finite-model/instantiation distinction and
formula-processing comparisons; Primo does not implement their complete
finite-model-finding or proof-production frameworks.

SMT-LIB 2.7 supplies the syntax/binding/session specification used by
[frontend](implementation/frontend.md) and [sessions](implementation/sessions.md).
Robert Daniel Brummayer's 2009 PhD thesis, Chapter 4, grounds
[fuzzing and reduction](development/correctness.md#fuzzing-and-reduction-sources).
The {source}`general reading summaries <docs/papers/summaries.md>` and
{source}`original literature register <docs/literature.md>` retain initial
questions and comparisons. Their old milestone language does not override
the current capability or lead registers.

## Maintaining the reading record

Publication identity belongs in the bibliography, not an ignored PDF filename.
For new work, read the relevant primary section and record operations, data,
invariants, cost assumptions, proof obligations, tests, and implemented subset.
If the required source is unavailable, record the access blocker rather than
substituting another solver's implementation or a secondary summary.
