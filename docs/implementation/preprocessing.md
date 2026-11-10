# Preprocessing

Preprocessing simplifies assertions and translates operators into the forms
supported by the solver engines. Some passes also add useful consequences.
A rewrite may preserve equivalence, elimination may require model reconstruction,
and fresh definitions may introduce symbols. The [tactic layer](tactics.md)
keeps the transformed assertions together with their reconstruction records.

This chapter describes the default rebuild engine's pipeline. Quantified UF
takes a [separate route](quantifiers.md); assumptions-based incremental engines
use the [restricted tactic contract](tactics.md#scope-and-incremental-use).
Clausification follows preprocessing and is described in [SAT integration](sat.md).
An **input goal** here is a collection of assertions without quantifiers.
A **profile** selects which optional passes run and how often they repeat.

(preprocessing-and-tactics)=
## How tactics execute preprocessing

Preprocessing passes are implemented as **tactics**: operations that return
transformed goals together with the information needed to reconstruct models.
Tactics can be composed in sequence or repeated; the resulting operation is
also a tactic. Primo uses this composition to build its preprocessing pipeline.
Users select profiles and pass options through command-line settings; there
is currently no user-facing tactic language.

For example, suppose a goal contains the Real assertions $x=y+2$ and $x>5$,
and the substitution pass accepts the first assertion as a definition of $x$.
Replacing $x$ by $y+2$ makes the defining equality trivially true and leaves
$y+2>5$ as the remaining obligation.
{source}`substitute_solved_constants <src/smt/preprocessing.cpp>` returns
these rewritten assertions and the substitution $x\mapsto y+2$.
The `solved-constant-substitution` tactic in
{source}`preprocessing_tactics.cpp <src/smt/preprocessing_tactics.cpp>`
keeps the rewritten assertions in the goal and, when a model is requested,
records the substitution for reconstruction. If the solver later chooses
$y=4$, reconstruction assigns $x=6$, satisfying both original assertions.

In the repeated simplification round, this substitution belongs to the
`solve-eqs` group. Value propagation runs before that group, and derivation
of unit-equality consequences follows it. Repeating the round allows facts
found by later passes to help earlier passes on the next iteration. The
selected profile and individual options control which optional passes run
and the repetition limits. The example above isolates one step; other passes
may simplify its result further.

Before choosing a profile, Primo lowers extended operators and normalizes
the assertions. After the selected passes finish, the session handles goals
already settled by preprocessing and sends remaining obligations, with their
reconstruction records, to the [solver tactics](tactics.md#from-a-session-query-to-an-engine).
The following sections give the execution order and stopping rules.
The [tactic chapter](tactics.md) explains the common goal contract and
composition operations.

(preprocessing-order)=
## Execution order

{source}`prepare_and_execute_preprocessing_strategy
<src/smt/preprocessing_strategy.cpp#L554-L641>` executes the initial sequence,
analyzes its result to select a profile, then runs the remaining passes.
The following order is encoded by
{source}`make_preprocessing_strategy <src/smt/preprocessing_strategy.cpp#L419-L552>`.
A goal that reduces to true or false can stop the enclosing sequence early.
In the table, **ITE** means an if-then-else expression, written `ite` in SMT-LIB;
**solve-eqs** is the tactic that eliminates definitions by substitution.

| Stage | Work and condition |
| --- | --- |
| 1. Simplify extended terms | If enabled, simplify comparisons involving constant if-then-else terms, then apply bounded structural `ite`/`div`/`mod`/`abs` rewrites. The constant-ITE pass also has its own switch. |
| 2. Eliminate extended operators | Always translate remaining extended terms into Boolean structure, linear terms, and fresh definitions. |
| 3. Normalize the initial goal | Optional arithmetic atom canonicalization, then optional initial bounded Boolean flattening. |
| 4. Choose a profile | Analyze the normalized goal; retain finite-domain clauses and symmetry results needed later. |
| 5. Profile-specific preparation | Integer profiles run bounded syntactic solve-eqs. The profile that skips the main loop for large inputs can run one solve-eqs pass. Integer profiles with an enabled round normalize Boolean negations before that round. |
| 6. Repeat a simplification round | Only when the selected profile enables it and the pass budget is positive; see the round below. |
| 7. Emit finite-domain constraints | Add selected symmetry restrictions, at-most-one clauses found before and after simplification, and equality definitions. |
| 8. Normalize generated arithmetic | Optionally canonicalize atoms again, then replace arithmetic equalities with pairs of inequalities. |
| 9. Eliminate one-sided Real variables | Optional monotone elimination. |
| 10. Purify mixed terms | Introduce fresh constants to name terms shared between theories; optionally canonicalize their comparisons again. |
| 11. Normalize the final Boolean structure | Optional implication rewriting and bounded associative flattening. |

`--preprocessing-mode=off` disables the repeated round. It does **not** skip
the initial sequence, arithmetic-equality elimination, purification, or final
Boolean normalization. Optional stages still use their individual switches;
required translations always run unless an earlier stage settles the goal.
Integer syntactic solve-eqs also runs independently of this mode.
`--preprocess-only` is a developer diagnostic for measuring preprocessing
without search. It returns `unknown` for surviving goals, even an empty goal,
and `unsat` if every alternative reduces to false. It does not print the
transformed assertions. Quantified `UF` input stops after the frontend,
without running these ground passes or entering the quantifier solver.

(preprocessing-profiles)=
## Profiles and repetition

The options default to `auto`, with the **legacy** automatic selector.
{source}`Profile analysis <src/smt/preprocessing_strategy.cpp#L223-L417>`
selects passes separately for the integer logics and for UF/Real inputs.

| Requested mode or logic | Simplification loop |
| --- | --- |
| UF/Real `off` | No repeated round. |
| UF/Real `basic` | Round with the selected optional transformations. |
| UF/Real `strong` | Allow at least three base passes; enable equality bridging and finite-domain strengthening. Other optional passes retain their individual switches. |
| UF/Real `auto` | Select a profile based on assertion count, formula size, and recognized finite domains, as described below. |
| `QF_LIA` with `auto` | Adapt to `off`, retaining integer syntactic solve-eqs and the stages outside the loop. |
| `QF_UFLIA` with `auto` | Use `basic` with the integer restrictions described below. |
| Either integer logic with explicit `basic`/`strong` | Enable the round with integer restrictions; strong additionally enables finite-domain strengthening. |

The legacy selector uses the number of assertion roots and reachable-term
counts. Writing a conjunction as one assertion can therefore select a different
profile from writing each conjunct as a separate assertion. It skips the main
loop for very large inputs, selects basic preprocessing for small inputs with
multiple assertions or suitable finite domains, and otherwise selects the
single-assertion or strong profile according to the number of roots.

The opt-in structural selector treats these forms alike: it recursively
collects conjuncts from assertion roots and nested `and` nodes, stopping at
other operators. An `and` under `or` or negation remains part of one conjunct.
The structural selector tries these profiles in order:

1. Skip the main loop if the conjunct count exceeds its configured limit.
2. Use basic preprocessing if the finite-domain and symmetry checks pass.
3. Use basic preprocessing for a sufficiently small conjunction with recognized
   finite-domain choices, where each conjunct is a literal, a disjunction of
   literals, or an implication with a literal antecedent and a literal or
   disjunction of literals as its consequent.
4. Otherwise, use strong preprocessing.

The thresholds are explicit options. Profile selection is a Primo heuristic
whose usefulness must be measured. The
{source}`selector design and earlier evaluation <docs/structural-auto-preprocessing.md>`
record why it remains opt-in.

Both integer profiles first run `qf-lia-solve-eqs`, with at most eight syntactic
substitution passes and no Gaussian elimination. Their optional repeated round
uses Boolean/value propagation and syntactic solved-constant substitution,
but disables Gaussian elimination, equality bridging, ground-congruence
generation, and ground-unit substitution. Before the first round, Boolean
rewriting puts formulas into **negation normal form** (NNF), with negations
applied to atoms.
The separately optional monotone pass recognizes only Real variables, so it
does not eliminate integer variables. Strong integer profiles can recognize
finite domains of distinct integer numerals. Symmetry breaking cannot permute
these numerals because their meanings are fixed.

The base repeat budget is `preprocessing_passes` (default 4). Once that budget
is reached, another round requires a strict reduction in reachable terms of
at least `preprocessing_adaptive_shrink_percent` (default 10), with at most
`preprocessing_adaptive_extra_passes` extra rounds (default 4). Strong and
UF/Real single-assertion profiles have a minimum base budget of three.
Unchanged assertion vectors stop the loop earlier. Reachable terms measure
the current formula; the term manager also retains old nodes no longer
reachable from its assertions.

(preprocessing-round)=
## One simplification round

The round in
{source}`preprocessing_tactics.cpp <src/smt/preprocessing_tactics.cpp#L643-L666>`
has this order:

1. Equality bridging, if selected.
2. Ground-congruence clauses, if selected. The reusable group also offers
   finite-domain evaluation, which the current profile builder leaves disabled.
3. Boolean unit propagation, then forced-atom propagation.
4. Solved-constant substitution, optionally with Gaussian candidates, then
   optional direct or guarded ground-unit substitution.
5. Simplify assertions using consequences of asserted equalities, and append
   changed, non-true results while retaining the original assertions.

Later phases can produce facts or definitions that earlier phases will use in
the next round. Their order also determines which facts and model-reconstruction
records each phase receives.

### Propagating facts

{source}`propagate_boolean_units <src/smt/preprocessing.cpp#L4763-L4799>`
collects Boolean units from the assertion conjunction and simplifies formulas
under those assignments. For $p\land(\neg p\lor q)$, it retains the fact
$p$ and reduces the second conjunct to $q$.

{source}`propagate_forced_atoms <src/smt/preprocessing.cpp#L4801-L4894>`
extends this to asserted predicates, equalities, and disequalities. It builds
a congruence closure from the equalities and records disequalities between
the resulting equivalence classes.
Thus $a=b$ can simplify a test of $f(a)=f(b)$, and $a=b$ together with
$f(a)\ne f(b)$ gives a contradiction. Known facts remain represented in
assertions or reconstruction data; they cannot be discarded as if their
atoms were independent propositional variables.

For example, the conjunction $a=b\land b=c\land a\ne c$ has three atoms
each used in just one polarity, yet is unsatisfiable. Applying the propositional
pure-literal rule to those theory atoms would incorrectly classify the formula
as satisfiable: their truth values are constrained by equality reasoning.

The final {source}`unit-equality phase <src/smt/preprocessing.cpp#L3612-L3667>`
builds congruence closure from asserted equalities. Equality tests established
by that closure simplify to `true` inside the assertions. The phase retains
the original assertions and appends changed, non-true simplified formulas,
avoiding duplicates. For example, from $a=b$, $b=c$, and $a\ne c\lor p$,
it can append $p$; from $a\ne c$ instead, it appends `false`.

### Eliminating definitions

{source}`substitute_solved_constants <src/smt/preprocessing.cpp#L4676-L4761>`
looks for asserted definitions $x=t$. It rejects self-reference, dependencies
on other pending definitions, and substitutions exceeding its definition-size
or estimated-growth limits. Multiple rounds can therefore eliminate a chain
of definitions that one round conservatively leaves in place.

Optional {source}`Gaussian candidate generation <src/smt/preprocessing.cpp#L734-L858>`
jointly reduces asserted Real equalities using exact-rational Gauss–Jordan
elimination. It normalizes each pivot row and removes that variable from every
other row, including rows used by earlier pivots. The reduced rows supply
substitution candidates, which then pass the same acceptance checks as other
definitions. For example, $2x+3y=5$ and $x-y=1$ yield $x=8/5$ and $y=3/5$;
the {source}`joint-system regression <tests/unit/preprocessing_tests.cpp#L949-L977>`
checks both substitutions.

The generator skips systems with more than 128 collected equalities or 256
eligible pivot variables. It is a bounded candidate-generation stage, not a
complete arithmetic solver. Division is valid over the reals; applying it as
an unconditional integer substitution would lose divisibility constraints.
Integer profiles disable it.

Ground-unit substitution instead picks representatives of equal ground terms
and rewrites their uses. The `guarded` variant accepts a rewrite only if it
sufficiently reduces the number of reachable terms, or rewrites many terms
without excessive growth in the assertion count. These are cost checks.
Both variants are off by default. Directed substitutions are recorded when
a model is requested, including the extra
ground-term restoration information needed for UF interpretations.

### Deriving equality and congruence clauses

{source}`EqualityBridge <src/smt/preprocessing.cpp#L3602-L3610>` examines
conjunctive equality information within the branches of a disjunction.
For

$$((a=b)\land(b=c))\lor((a=d)\land(d=c)),$$

both branches imply $a=c$, so the pass can add that consequence. It uses
limits on the number of branches and term pairs examined, and follows `and`/`or`
structure. It does not infer a unit from a disjunction occurring in an arbitrary
negative context. The two paths from $a$ to $c$ give this pattern its name,
an **equality diamond**. Primo implements this restricted rule; it does not
generate a complete set of transitivity constraints.

The optional {source}`CongruenceDefinitionGenerator
<src/smt/preprocessing.cpp#L5132-L5195>` adds bounded clauses expressing
congruence between applications of the same function already present in the
goal. For unary $f$, the clause is $a\ne b\lor f(a)=f(b)$. It creates no
new function applications.

(preprocessing-extended)=
## Extended terms and arithmetic normalization

### Simplify before introducing definitions

{source}`simplify_extended_terms <src/smt/preprocessing.cpp#L4425-L4466>`
performs a bounded number of bottom-up rewriting rounds. Structural ITE
identities, constant folding, and identities for `div`, `mod`, and `abs` can remove
operators before fresh variables become necessary. Equality distribution over
an ITE is {source}`enabled by default <src/smt/check_sat.hpp#L330-L348>`, with
{source}`growth restrictions <src/smt/preprocessing.cpp#L4160-L4198>`:
the ITE must not be shared by several parent edges, and neither immediate
branch may itself be an ITE. These checks avoid duplicating a shared conditional
or distributing only part of a nested chain. Use
`--no-ite-equality-distribution` to disable this rule.

A **constant if-then-else term** (CITE) has numeral branches, possibly nested
under further conditionals or scaled and offset by constants. Its value can
still depend on the conditions. For instance, `ite(p, 2, 5) < 4` simplifies
to `p`, whereas `ite(p, 2, 5) < 6` is always true.
{source}`simplify_constant_ites <src/smt/preprocessing.cpp#L4328-L4423>`
computes possible leaf values and pushes a comparison with a constant into
the conditional only where needed. Uniform outcomes collapse immediately.
Leaf sets and comparison results are cached across the pass, so shared
subterms are not repeatedly expanded for every atom.

This implements the case of Bromberger's §8.2.3 `csimp` procedure in which
one side of a comparison is a numeral and the other is a CITE
{cite:p}`bromberger2019`. The procedure's additional rule for equality between
two conditional terms is not implemented. Primo moves conditionals out of
sums and constant multiples using exact identities, without dividing
coefficients out of integer comparisons. This pass precedes extended-term
simplification because it can produce Boolean ITEs that those structural
rules simplify further. Fewer
auxiliary variables can come at the cost of more Boolean structure.

### Constant-comparison work inside CITE simplification

**Rejected experiment.** This subsection describes snapshot `98a5fd8b`, whose
endpoint/membership fast paths passed correctness checks but were removed after
a paired runtime regression. Current production retains the full leaf scan and
does not export the three comparison-work counters below. The
[experiment evidence](../evidence/arithmetic-overhead-20261109.md#cite-constant-comparisons)
records the measured savings, regression and rejection. The snapshot retains
the implementation and probes for reproduction.

In that snapshot, the pass owns `CiteLeaves`, an optional vector of exact `Rational` values per
`TermId`, and `CsimpCache`, keyed by operator, CITE ID, constant and operand
orientation. Present leaf sets are sorted and unique; negative scales are
resorted by the existing leaf builder. The complete vectors and their merging
representation remain unchanged.

{citesource}`csimp_against_constant <src/smt/preprocessing.cpp>` classifies a
nonempty set using its first and last leaf for `<` and `<=`. Each comparison
is monotone in the leaf value, in either operand orientation: matching endpoint
truth values establish a uniform result. Different endpoint truth values require
the existing mixed-branch reconstruction. Equality first handles a singleton;
for larger sets, `lower_bound` finds whether the constant occurs. Absence is
uniformly false, while membership is mixed because the leaves are unique.
For example, equality to 3 over `{2, 4, 8}` is false without descending into
branches; equality to 4 requires the original conditional reconstruction.

These are Primo engineering adaptations of Bromberger §8.2.3, p. 274
{cite:p}`bromberger2019`, not additional rules attributed to the thesis.
{citesource}`SimplifyConstantItesResult <src/smt/preprocessing.hpp>` owns the
counters published by {citesource}`make_simplify_constant_ites
<src/smt/preprocessing_tactics.cpp>` as
`:preprocessing.cite.uniformity-checks`, `.leaf-comparisons` and
`.leaf-scan-equivalent`. One uniformity check counts one classification visit;
leaf comparisons count exact endpoint predicates or binary-search comparisons,
including the final equality test. The equivalent scan counter adds the full
leaf-set size at the same visit. Mixed frames can be classified on entry and
again after their children finish, as before. Cache-key comparisons and leaf
construction, sorting and union work are excluded.

Per classification, inequalities take at most two leaf comparisons and equality
takes logarithmic comparisons in the set size. Reconstruction can still visit
many subterms; these bounds do not make the whole pass constant/logarithmic or
bound the sum of cached leaf-set sizes by the input DAG size. The existing
hoist only handles a directly exposed ITE child: a mixed comparison against
a scaled CITE hidden inside a sum can retain its original atom. This fallback
is preserved, rather than extending the reconstruction pipeline. Non-CITE absence
still rebuilds the original atom, and an empty present set retains the old
vacuous true result before endpoint access. The outer pass still leaves a
top-level set with fewer than two leaves to ordinary folding. All endpoints
and iterators are consumed before any term construction or leaf-map mutation.

{citesource}`Preprocessing regressions <tests/unit/preprocessing_tests.cpp>` compare
reconstructed terms against independent leaf decisions for both orientations,
strictness, exact rational thresholds, negative/zero scales, equality membership
and absence, singleton leaves and shared subterms. Balanced 8/64/512/4096-leaf
probes require two comparisons for uniform inequalities and a logarithmic bound
for equality absence inside the endpoint range. The candidate avoids 96.0% of
equivalent leaf comparisons on the NEC diagnostic, but is rejected after seven
completed Hongyi pairs are all slower than the cache-only control (median
1.53%). This is not a new leaf representation or CITE pipeline.

### Translate remaining operators

{source}`lower_extended_terms <src/smt/preprocessing.cpp#L4468-L4614>`
traverses shared terms once and uses the context's function for creating fresh
constants.

| Input | Replacement and side constraints |
| --- | --- |
| Boolean `ite(c, a, b)` | $(c\land a)\lor(\neg c\land b)$, with no fresh term variable. |
| Non-Boolean `ite(c, a, b)` | Fresh $v$ with $c\Rightarrow v=a$ and $\neg c\Rightarrow v=b$. |
| Integer `abs(t)` | The same conditional encoding for `ite(0 <= t, t, -t)`. |
| Integer `div(t,k)` / `mod(t,k)`, positive constant $k$ | Shared integer quotient $q$ with $0\le t-kq\le k-1$; division uses $q$ and remainder uses $t-kq$. |

Definitions are emitted before the assertion that needs them. Within the call,
equal rewritten numerator/divisor pairs share a quotient. Negative divisors
are normalized earlier; zero or nonconstant divisors are unsupported. The
[integer chapter](lia.md#input-lowering-and-integer-bounds) gives the uniqueness
argument, signed example, and subsequent bound handling.

(normalize-atoms-at-the-right-boundary)=
### Arithmetic atom normalization and equality elimination

The optional early arithmetic canonicalizer converts proportional atoms to
a common core-term form with coprime integer coefficients. It can therefore
merge their SAT abstractions. This differs from the always-used
[leading-coefficient normalization](lra.md#from-atoms-to-bounds)
inside LRA preparation, whose purpose is sharing a polynomial row. The
{source}`canonicalizer <src/smt/preprocessing.cpp#L499-L599>` is off by default;
if enabled it runs initially, again after finite-domain generation, and for
comparisons after purification.

Required {source}`arithmetic-equality elimination
<src/smt/preprocessing.cpp#L2657-L2668>` translates $a=b$ to
$(a\le b)\land(b\le a)$ for **both Real and Int**, despite the helper's
`lower_real_equalities` name. Negation remains Boolean structure for SAT.
This stage follows equality-producing passes, including finite-domain
generation, so it also translates the equalities those passes introduce.
The opt-in `guarded` variant rewrites $G\lor(a=b)$ as
$(G\lor(a\le b))\land(G\lor(b\le a))$. It requires exactly one arithmetic
equality among the disjuncts to avoid repeated distribution and exponential growth.

(preprocessing-finite)=
## Finite domains and symmetry

A recognized choice $t=c_0\lor\cdots\lor t=c_{n-1}$ permits extra Boolean
reasoning when the values are known distinct. The
{source}`finite-domain generators <src/smt/preprocessing.cpp#L4896-L5102>`
use explicit asserted disequalities; distinct integer numerals supply another
supported proof of distinctness. Arbitrary values with different `TermId`s
are not automatically distinct.

At-most-one (AMO) clauses state
$\neg(t=c_i)\lor\neg(t=c_j)$ for distinct values. Cached clauses generated before simplification are reused only if the
executing goal has the same ordered assertion roots. Otherwise that raw scan
is repeated on the current goal before emission. Emitted clauses are
deduplicated against a second scan after simplification. A choice disjunction is not always needed: if
the formula contains $t=u$ and $t=v$, and asserts $u\ne v$, the generator
can add $\neg(t=u)\lor\neg(t=v)$. For terms $s,t$ with the same recognized
domain and a known disequality $s\ne t$, it can add
$\neg(s=c_i)\lor\neg(t=c_i)$. The goal retains forced equality and
disequality facts so this check remains possible after rewriting.

If the input already mentions $s=t$ for two terms with the same recognized domain,
equality-definition clauses connect that atom to their values. For each $c_i$,
they express $(s=c_i\land t=c_i)\Rightarrow s=t$ and both directions of
value transfer under $s=t$. The pass does not create equality definitions
for every possible term pair. Enabling these clauses in `auto` mode is a
separate option, off by default, with limits on domain size, generated clauses,
and formula size.

{source}`UfSymmetryBreaker <src/smt/preprocessing.cpp#L5197-L5241>` attempts to check
whether permuting a recognized set of uninterpreted constants leaves the
assertions unchanged. It treats equality, `and`, and `or` as commutative when
comparing formulas. Checking a cycle through the constants and a swap of the
first two suffices, because these permutations generate all the others.
The pass then orders candidate terms containing none of the permuted constants
by `TermId`. These terms are **fixed** by the permutations: their syntax stays
unchanged when the constants are renamed.

With **value precedence**, a term may take value $c_i$, for $i>0$, only if
an earlier term in the sequence takes value $c_{i-1}$. Disabling value
precedence uses weaker restrictions on the values allowed at each position.
A group with no fixed candidate term is rejected. Arithmetic numerals are
never permuted.
Unlike AMO consequences, symmetry constraints need not hold in every original
model; the justification is that some equivalent renamed model satisfies them.
The {source}`symmetry design note <docs/papers/smt-symmetry-breaking.md>`
maps this subset to Déharbe et al.'s constant-permutation restriction.
Theorem 5 justifies the restriction by renaming models, and Lemma 6 justifies
checking only the two generating permutations {cite:p}`deharbe2011symmetry`.
The value-precedence rule for fixed terms is described in the design note.
General graph-automorphism detection is outside Primo's implementation.
The [arithmetic-symmetry lead](../leads/arithmetic-symmetry.md) records broader
SMT and integer-programming approaches and their evaluation requirements.

The {project}`repaired comparison <src/smt/preprocessing.cpp#L2036-L2070>`
must preserve every semantic term field: kind, sort, symbol,
Boolean value, exact rational value and children. The rational field stores
numeric literals, multiplication coefficients and integer `div`/`mod` divisors.
It is serialized using the canonical integer-or-fraction spelling of
{source}`Rational::to_string <src/util/rational.cpp#L627-L637>`, rather than a
rounded value or a hash. Only the selected constants are renamed; only the
three commutative kinds above have their children reordered.

This repairs the payload omission found in the page-by-page review. The
{ref}`repair and qualification record <symmetry-payloads>` retains the SAT
counterexample and the completed correctness checks. The proof obligation is structural:
equal keys must identify the same syntax after these allowed operations. The
balanced child encoding and full payloads distinguish other terms. Memo tables
and strings belong to one comparison; it creates no terms and changes no solver
state. Numeric serialization adds exact integer-to-text conversion; the key
stores the complete resulting spelling. Existing subtree-string copying and
child sorting remain; this is not an
implementation of the paper's linear-time DAG representation.

For a domain of size $n$, pairwise AMO generation can produce
$n(n-1)/2$ clauses for one term. Equality-definition and congruence generation
also expand the formula, so generation limits control their cost and counters
record the resulting growth. `FiniteDomainEvaluator` is a separate simplifier
using bounded enumeration; the current profile builder leaves it disabled
both inside the round and in the final finite-domain stage.

(preprocessing-final)=
## Final passes and reconstruction

### One-sided Real variables

The {ref}`automatic QF_LRA policy <automatic-lra-heuristics>` enables this pass
for rebuild queries with automatic preprocessing. Explicit preprocessing profiles
and other routes retain a disabled default; the positive and negative flags
override that choice, subject to the assumptions-mode restriction.

The optional {source}`monotone eliminator
<src/smt/preprocessing.cpp#L5250-L5509>` tracks the direction in which each
linear inequality constrains a Real constant, accounting for Boolean polarity.
If every occurrence permits moving $x$ in the same direction, a sufficiently
large or small value can satisfy those occurrences together. For
$x>y\land x>z$, removing the two constraints is satisfiability-preserving:
after choosing $y,z$, reconstruct $x=\max(y,z)+1$.

Equalities, opposing directions, uninterpreted applications involving the
variable, and unsupported arithmetic leaves prevent elimination. Negative
occurrences reverse the direction; an atom seen in both polarities cannot be
replaced with a Boolean constant by this rule.
The pass records the eliminated variables and their constraints for model
construction. When several eliminated variables share a constraint, whichever
receives a value last is chosen to satisfy it, using the other variables' known values.
Primo uses a deterministic order; it does not reject such dependencies as
cycles. The {source}`original rule and attribution note
<docs/papers/monotone-variable-elimination.md>` explain the one-sided
Fourier–Motzkin observation and why it is not an objective-driven LP
dominated-column rule.

### Mixed purification and Boolean structure

{source}`make_purify_mixed <src/smt/solver_engines.cpp#L12578-L12625>`
introduces fresh constants, called **proxies**, to name expressions shared
between the theories. It records the correspondence between function applications
and their proxies for model reconstruction. [Theory combination](combination.md)
explains the interface and its defining constraints. This step runs after
arithmetic-equality elimination and handles its own definitions.
If canonicalization is enabled, the post-purification pass is restricted to
comparisons so it preserves the intended interface equalities.

Finally, {source}`make_normalize_boolean
<src/smt/preprocessing_tactics.cpp#L485-L531>` can rewrite implications and
flatten nested same-kind `and`/`or` terms. Implication rewriting defaults to
off; its clause-only mode recognizes a bounded clause shape, while `all`
allows the broader rewrite. Initial flattening defaults to off; final
flattening defaults to an arity threshold of 2048. A second application point
is needed because intervening rewrites can introduce new nesting.
The term constructors retain sharing and local simplifications; they do not
recursively flatten every shared Boolean node. This distinction prevents
construction from repeatedly copying large operand lists.

(preprocessing-example)=
### Follow an integer goal

Take the `QF_LIA` assertions $x=y+2$ and $y\ge1$, with model production and
the default automatic mode. There are no extended terms to eliminate. The
profile selects `off` for the repeated round, but integer syntactic
solve-eqs still replaces $x$ by $y+2$ and appends a `LiaSubstitutionConversion`.
The remaining defining equality simplifies to true. Arithmetic-equality
elimination and purification leave the remaining formula unchanged; final
Boolean normalization leaves the bound on $y$ for the integer engine.

A reduced model with $y=1$ is reconstructed with $x=3$. The goal carries that
conversion through every intervening `with_assertions` update. Even with the
repeated round disabled, preprocessing has changed the formula, and the solver
needs the saved substitution to reconstruct the original model.

More generally, Boolean facts, directed substitutions, integer substitutions,
monotone eliminations, and purification proxies have distinct conversion
records. The [model chapter](models.md#following-a-model-back-through-preprocessing)
describes their reverse replay and validation against original assertions.

(preprocessing-representation)=
## Representation

| Type and code | Responsibility |
| --- | --- |
| {source}`CheckSatOptions <src/smt/check_sat.hpp>` | Requested profiles, pass switches, and size/iteration limits. |
| {source}`PreprocessingProfile <src/smt/preprocessing_strategy.hpp>` | Selected passes, repetition limits, finite-domain analysis, and generated clauses retained for later use. |
| {source}`PreparedPreprocessingStrategy <src/smt/preprocessing_strategy.hpp>` | Full strategy description, selected profile, and result returned by `prepare_and_execute_preprocessing_strategy` after executing the transformations. |
| {source}`PreprocessingRoundOptions <src/smt/preprocessing_tactics.hpp>` | Immutable settings for one simplification round. The round uses the executing tactic context. |
| {modelrepairsource}`FiniteDomainPreprocessInput <src/smt/preprocessing_tactics.hpp#L65-L75>` | Owning source assertion IDs, immutable precomputed AMO clauses and optional symmetry result. Reuse is bound to the same term manager and exact executing goal roots. |
| {source}`ModelReconstruction <src/smt/model_reconstruction.hpp>` | Each goal's model-conversion records and forced equality/disequality pairs needed by later stages. |

The term manager owns every original and generated term. Pass results refer
to these terms by ID. Rewriting caches the result for each visited term and
creates replacements through the manager, preserving shared subterms.
IDs remain valid when term storage grows; borrowed references to term nodes
may not. A pass must copy the child IDs and other data it needs before
constructing another term.

Finite-domain and symmetry analysis retain owning copies of the ordered
assertion IDs and their generated constraint results. The
{modelrepairsource}`emission operation <src/smt/preprocessing_tactics.cpp>`
compares `FiniteDomainPreprocessInput::source_assertions` with the executing
`Goal::assertions()`. An exact match permits cached results; any change forces
regeneration from the current goal. The original profile-analysis counters
still describe the initial goal; emitted-constraint counters describe actual
emission. Simplification may remove an earlier finite-domain opportunity;
reusing its old vocabulary is not a substitute for transporting its proof
through the transformation.

For example, let `a = 7`, `f(a) = 0`, and `f(a) = 0 or f(a) = 1`. A raw AMO
clause over `f(a)` may be computed before solve-eqs eliminates `a`. Emitting it
afterwards reintroduces `a` as a free argument of an auxiliary purification
application. Its temporary model value need not be 7. Restoring `a = 7` can
then invalidate the function row selected by that auxiliary. The refreshed
scan uses the surviving `f(7)` syntax instead. The model guards and reverse
conversion order remain unchanged.

The same cache boundary applies to analyzed symmetry constraints. Each
alternative goal is checked separately; refreshing one invocation owns a local
result and does not replace the immutable snapshot captured by another. All
IDs belong to the same live term manager, which must outlive the tactic.
This is an original cache-validity repair; it adds no new finite-domain or
symmetry algorithm. Regression tests exercise unchanged cache hits, changed
goals, alternative goals, AMO and symmetry, plus original-model replay on
integer and real UF inputs. Qualification evidence is recorded separately.

(preprocessing-evidence)=
## Correctness checks and evidence

The {source}`preprocessing unit tests <tests/unit/preprocessing_tests.cpp>`
exercise transformations and their preconditions: deep and shared term graphs, substitution
cycles and growth limits, equality consequences, finite-domain distinctness,
ITE arena growth, shared quotients, and monotone polarity. The
[composition checks](tactics.md#checks-and-implementation-status) test complete
supported profiles and model conversion, since individually correct passes
can still compose incorrectly. These checks include arithmetic equalities
introduced by later passes and finite-domain clauses generated before simplification.

Useful counters distinguish selection from actual work: profile and limit
counters describe policy; `:preprocessing.passes` and adaptive-pass counts
describe repetition; transformation counters describe rewrites, generated
clauses, rejected candidates, and cutoffs. Tactic invocation alone does not
show that an algorithm transformed an input. Historical performance and fuzz
records are located in the [preservation map](../coverage/tactics-preprocessing.md).
These records describe earlier runs; no solver experiments were rerun for this
documentation review.

(preprocessing-sources)=
## Sources and implementation status

- The strategy and model-conversion design follows de Moura and Passmore,
  §3 {cite:p}`deMoura2013`; [tactics](tactics.md) describes the implemented subset.
- Bromberger's §§8.2.2–8.2.4 {cite:p}`bromberger2019` provide the comparison
  for sharing, arithmetic flattening, ITE simplification, definitions, and
  CNF preparation. Primo hash-conses on construction, keeps its own polynomial
  representation, and implements the CITE subset described above. The thesis's
  broader ITE reconstruction, pseudo-Boolean rewriting, and optimized CNF
  selection are not all implemented. Its §7.2.4 arithmetic search preparation
  is a separate subject covered by [LIA](lia.md).
- Arithmetic atom normalization is related to Monniaux's §2.1
  {cite:p}`Monniaux2009`; [LRA](lra.md) distinguishes the two normal forms
  actually used by Primo. This is not an implementation of the paper's
  floating-point oracle.
- The symmetry and monotone design notes cited above give their restrictions
  and attribution. Equality bridging and the bounded finite-domain clauses
  are the elementary rules stated here, not complete implementations of
  transitivity-constraint generation or finite-model finding.

Further ITE preprocessing and strategy synthesis are recorded as proposals
in the {source}`open-leads register <docs/open-leads.md>`.
