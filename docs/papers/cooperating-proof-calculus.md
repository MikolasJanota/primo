# The Cooperating Proof Calculus: reading note

**Andrew Reynolds et al., *The Cooperating Proof Calculus: Comprehensive Proofs
for an SMT Solver* (2026)** {cite:p}`ReynoldsEtAl2026CPC`.

CPC is an externally defined calculus for the reasoning performed by cvc5,
including rewriting, clausification, SAT inferences and theory reasoning. Its
main contribution is broad proof coverage with an independent checker, together
with an account of the remaining trust and performance costs. It is a useful
reference for extending Primo's small certificate format; adopting CPC is not a
decision made by this note.

## Source, scope and places to return to

Read from the user-supplied `external/2026cav.pdf`, 26 pages, SHA-256:

```text
dedf1845f4392864c0951e4e618bd1cfb0773859683e376aebd67e0fd584e6c0
```

The PDF metadata dates the copy to 26 May 2026. The filename suggests CAV,
but this note does not infer a proceedings citation or DOI from that filename.
The reading covers §§1–6 and the references; page numbers below are the printed
page numbers, which coincide with PDF page positions. The paper lists an
[experimental artifact](https://zenodo.org/records/19867026) on p. 21. Its contents
were not downloaded or reproduced for this reading. The PDF remains an external
reading copy and is not redistributed with the documentation.

| Question when returning | Paper location |
| --- | --- |
| What is CPC, versus Eunoia and Ethos? | §§1–2, pp. 1–7 |
| How are local assumptions discharged? | §2.2, pp. 5–6 |
| How are executable side conditions specified? | §2.3, pp. 6–7 |
| How are CNF and SAT proofs represented efficiently? | §§3.2–3.3, pp. 7–9 |
| Which EUF and arithmetic rules matter to us? | §§3.4–3.5, pp. 9–10 |
| What other theories are covered? | §§3.6–3.12, pp. 11–15 |
| What has actually been verified? | §4, pp. 15–17 |
| What does the evaluation establish, and exclude? | §5 and Table 1, pp. 17–20 |

## Three separate layers

**Eunoia** is the logical framework. Its syntax extends SMT-LIB 2.7, but its
underlying language is typed higher-order logic with dependent types. A signature
declares theory symbols, their types, proof rules and programs for computations.
Built-in operations include exact arithmetic on literals, term comparisons and
operations on strings and bit-vectors. Thus, calling the syntax SMT-LIB-like
does not make Eunoia an ordinary SMT-LIB input script.

**CPC** is a particular Eunoia signature. The supplied paper reports definitions
for 177 native cvc5 function symbols and 585 proof rules in 8,025 lines of Eunoia.
Of those rules, 424 are declarative rewrite rules across eight theory groups.
The signature can evolve as solver techniques evolve.

**Ethos** checks Eunoia proofs against the supplied signature. It has no built-in
CPC rules. A proof starts with declarations and assumptions, then names each
derived fact with a fresh proof identifier. A step supplies a rule, earlier
premises and any arguments that cannot be inferred. The checker computes the
rule's conclusion and checks any explicitly supplied conclusion against it.

This separates solver implementation from proof checking. It also makes the
chosen signature part of the trust boundary: an executable rule definition is
not, by itself, a theorem that the rule is sound.

## Assumptions, computations and invariants

Local assumptions are pushed and subsequently discharged in stack order. The
`scope` rule turns a derivation of $G$ under assumption $F$ into $F\Rightarrow G$.
In particular, deriving false under the negation of a theory lemma proves that
lemma. This gives a direct representation of the conflict-by-contradiction
pattern used in CDCL(T).

Computational side conditions are pure functions defined by ordered
pattern-matching cases. Evaluation eagerly evaluates arguments and uses the
first matching case. Such programs can compute a conclusion, not merely return
a Boolean test. For example, a normalization rule can compute normal forms and
compare them. Programs may get stuck or fail to terminate; the framework does
not automatically prove totality. The paper says that a computational term
which fails to fully evaluate makes the proof fail, and that formal Eunoia
semantics remains under development (§2.3).

The proof representation therefore needs typed terms and values, an environment
of previously named facts, a local-assumption stack, a rule signature and a
computational evaluator. These are conceptual requirements extracted from §2,
not claims about the internal containers used by Ethos. The main obligations
are correct typing, valid premise references and scope, exact rule application,
and agreement between the computations and their intended semantics.

## Boolean reasoning and the checking bottleneck

For CNF conversion, CPC supplies 21 rules covering SMT-LIB Boolean connectives.
These rules treat formulas themselves as literals instead of explicitly
introducing Tseitin variable names. It also has 28 further Boolean rules, such
as implication elimination and conjunction introduction/elimination (§3.2).

The resolution interface has binary resolution, chain resolution and a chain
variant allowing duplicate removal and reordering. Pivots and polarities are
explicit proof arguments. LRAT's reverse-unit-propagation steps translate
naturally to the chain rule. The discussion of RAT additionally relies on a
translation to resolution; it should not be read as saying that every LRAT
instruction is directly a RUP step (§3.3).

The important engineering mechanism is to avoid materializing every intermediate
resolvent. The chain checker computes each premise's contribution to the final
clause, using multiset operations to remove the relevant pivots. Built-in
multiset difference and clause comparison reduce intermediate term allocation.
This matters especially for large QF_UF and bit-vector proofs. The rule and its
supporting programs occupy 66 Eunoia lines, but those lines rely on optimized
built-in operations.

The paper does not supply a general asymptotic bound for the whole checker or
a constant-memory streaming guarantee. Its concrete lesson is that a compact
proof-rule definition and efficient checking require suitable computational
primitives; line counts alone do not predict cost.

## EUF and arithmetic

EUF uses equality reflexivity, symmetry, transitivity and congruence (§3.4).
Congruence has separate variants for ordinary functions, higher-order function
equality, variadic operators and pairwise operators such as `distinct`.
Distinctness rules additionally cover concrete values and conflicts with a
sort's cardinality. This is broader than checking only ground congruence over
uninterpreted sorts.

For linear arithmetic, §3.5 gives rules for summing arithmetic relations and
multiplying them by positive or negative coefficients. Negative multiplication
reverses inequality direction while preserving equality. Together these rules
express Farkas-style refutations. Separate polynomial-normalization rules
justify equality of arithmetic terms and equivalence of relations after
appropriate scaling. The normalization machinery occupies 122 Eunoia lines;
its ordering of terms is established dynamically and deterministically as terms
are encountered. The paper describes an insertion-sort-like normalization that
often benefits from this order, not a universal linear-time bound.

Integer arithmetic adds bound rounding; trichotomy provides a split into less
than, equality or greater than. Nonlinear support covers incremental
linearization, including monomial signs, magnitude comparisons and tangent
planes. It does **not** cover cylindrical algebraic coverings. Extended
arithmetic operations are reduced to simpler primitives by additional rules.

### Connection to our weighted LRA conflict proposal

This paragraph is an inference for Primo, not a proposed format taken from CPC.
A list of inequality/multiplier pairs can be viewed as a compact description of
successive scaling, summation and normalization steps. For example:

$$
x+y\le2,\qquad x\ge2,\qquad y>0
\quad\Longrightarrow\quad
(x+y)+(-x)+(-y)<2-2+0,
$$

giving $0<0$, with all three normalized inequalities weighted by $1$.
Our checker must retain strictness, validate coefficient signs and use exact
rational arithmetic. If equalities are retained, their multipliers can have
either sign. If we export the already-lowered inequalities, no separate
equality-premise rule is needed. Disequalities require Boolean splitting or
another justified rule; they are not single convex inequalities to add.

CPC's decomposition suggests a possible future elaborator from compact
certificates to smaller proof steps. It does not establish that our proposed
syntax already translates to CPC, or that Ethos or Micromath accepts it.
Normalization and the connection to the exact exported atoms remain proof
obligations.

## Other theories and reusable mechanisms

| Area | Main mechanism in the paper |
| --- | --- |
| Bit-vectors (§3.6) | Single-step bit-blasting, computational rewrites and polynomial normalization; the bit-blasting program is 540 Eunoia lines. |
| Arrays (§3.7) | Read-over-write and extensionality, using a distinguished index witnessing array disequality. |
| Strings (§3.8) | Word-equation rules, length reasoning, regex unfolding/derivatives, extended-function reductions and computational rewrites. |
| Datatypes (§3.9) | Constructor splitting, injectivity, acyclicity and selector/tester/updater rules, with datatype introspection. |
| Sets (§3.10) | Membership reasoning, singleton injectivity, evaluation and extensionality witnesses. |
| Quantifiers (§3.11) | Instantiation, Skolemization, alpha equivalence and rewriting, with explicit capture-avoiding substitution. |
| Shared normalization (§3.12) | Associative/commutative/idempotent normalization, absorbing elements and constant evaluation. |

For quantifiers, the paper's representation uses ordinary terms for quantifier
applications and named, typed variables. Substitution must therefore explicitly
avoid capture; this obligation is not discharged merely by adopting
SMT-LIB-like syntax. Theory combination largely reduces to equality-splitting
lemmas in the described solver; the paper reports no separate combination rule
family (§3.1, footnote 13).

## Independent checking versus verified soundness

The trusted core reported in §1 consists of approximately 10,000 C++ lines for
Ethos plus the approximately 8,000-line CPC signature, rather than cvc5's
more than 360,000 lines. These are size comparisons, not a proof that either
component is correct. Section 4 describes three complementary checks:

- **Lean reconstruction:** 180 CPC rules are supported across arithmetic, UF
  and quantifiers; about 115 are translated directly to theorems, with reflection
  or specialized tactics handling others. The paper reports roughly 20 times
  the native Ethos checking cost. This is not a complete verification of CPC.
- **Isabelle:** IsaRare has verified 338 RARE rules and exposed six faulty rules
  that were amended. Separate reconstruction via Alethe validates proofs in
  supported fragments. These results do not establish all 585 rules at once.
- **Translation back to SMT:** verification conditions were generated for 571
  rules; 14 involved internal symbols whose semantics was not readily encoded.
  Of the conditions, Z3 solved 139 and cvc5 solved 238, including 99 not solved
  by Z3, within 60 seconds. The reported validated total is 238 rules. This uses
  SMT solvers and does not itself reduce the trusted core. Counterexample
  synthesis found ten faulty rules from an earlier CPC release, subsequently
  fixed. Four could also be confirmed in Isabelle; six were outside that
  translation's scope.

These groups should not be added together as disjoint coverage counts. The
practical lesson is that checking a large proof can succeed while the rule
specification still needs independent soundness scrutiny.

## Evaluation and its limits

Section 5 considers 310,830 benchmarks in 59 non-floating-point SMT-LIB logics,
using a development version of cvc5 1.3.2 in its proof-supporting **safe**
configuration. The machines had Xeon E5-2686 v4 CPUs at 2.6 GHz and 252 GB RAM.
The initial solving limit was 60 seconds. Only the 153,188 instances reported
UNSAT in that stage formed the proof-production cohort; Table 1 then uses
600-second proof-generation/checking limits.

| Category | UNSAT solved | Proofs generated | Proofs checked | Generation overhead | Generation + checking overhead |
| --- | ---: | ---: | ---: | ---: | ---: |
| Overall | 153,188 | 152,382 | 151,571 | 5.11× | 7.11× |
| QF+UF | 8,971 | 8,968 | 8,941 | 4.12× | 15.85× |
| QF+Arith | 14,515 | 14,422 | 14,246 | 2.58× | 4.04× |

These are the paper's reported ratios on commonly solved instances, not ratios
computed by dividing the cumulative-time columns. QF+UF also includes arrays
and datatypes; QF+Arith includes arithmetic logics beyond QF_LRA. Neither row
is a pure-QF_UF or pure-QF_LRA result.

The paper reports about 99.4% proof-generation coverage and 98.9% checked-proof
coverage relative to the initial UNSAT cohort. Failures were mostly timeouts;
two proofs failed checking because of SMT-LIB features incompatible with Ethos
(p. 18, footnote 18). Thus the broad claim of no proof holes concerns inference
coverage, not successful bounded checking of every SMT-LIB input.

Generated proofs contain 913.4 million steps, including assumptions, and use
427 of the 585 rules. Common rules include equality transitivity (12.8%),
congruence (12.6%) and chain multiset resolution (9.8%). The overall average
proof size reported by Table 1 is 6.1 thousand steps. Rule coverage and a count
of steps are not measures of serialized bytes or peak memory.

Floating-point proofs are excluded because the SymFPU word-blasting reduction
is not fully proof-producing. The safe configuration also disables some
otherwise-default techniques, notably QF_UF symmetry breaking, CaDiCaL for
bit-vector solving and cylindrical algebraic coverings. Default configuration
solves 156,407 UNSAT instances versus 153,188 for safe configuration; the latter
is reported 17.1% slower on commonly solved UNSAT cases, or 9.9% when excluding
quantifier-free bit-vector benchmarks. Those configuration differences must
remain separate from proof-generation and checking overhead.

## Sources and implementation status in Primo

The mapping below describes the inspected Primo revision `dfdaafbe`, not an
implementation of CPC. The paper is a reading source and future design
reference only; this task changes no solver behavior.

| Paper mechanism | Current Primo boundary and possible follow-up |
| --- | --- |
| Full proof from source through preprocessing and CNF | [The current export contract](../reference/qf-uf-conflicts.md) starts at the prepared CNF and explicitly leaves source-to-CNF certification open. CPC shows why rewriting/clausification would be separate proof obligations. |
| Local proof of a theory lemma | {micromathpipesource}`QfUfConflictCollector <src/smt/qf_uf_export.hpp>` collects conflicting facts but does not export CPC derivations. Our current workflow asks an external tool to justify them. |
| Small external proof transport | {micromathpipesource}`ConflictSpool <src/smtlib/conflict_spool.hpp>` owns section files; {micromathpipesource}`the pipe helper <scripts/check_micromath_pipe.sh>` streams raw Micromath proof bytes. Neither implements Eunoia or the CPC calculus. |
| LRA scaling/summation witnesses | {ref}`LRA conflict construction <a-bound-contradiction-and-its-cancellation-proof>` currently returns premise literals without exporting weights. Weighted LRA certificates remain a proposal. |
| Efficient chain checking | Potential external-checker design reference. No change to Primo's SAT adapter or Micromath is implied by this note. |
| Rule verification | Keep producer testing, independent certificate replay and verification of the checker's inference rules as separate claims. No CPC-to-Lean/Isabelle reconstruction was added. |

For a future LRA extension, retain our small artifact and first specify exact
coefficient, strictness, equality and normalization semantics. Export witnesses
over the same atoms as the CNF. If a later translator targets CPC, identify a
specific signature version and prove or check the expansion of each compact
record. Proving preprocessing equivalence is a separate project. Broad theory
coverage, a general Eunoia evaluator and proof-assistant integration are deferred
possibilities, not requirements for the next certificate increment.
