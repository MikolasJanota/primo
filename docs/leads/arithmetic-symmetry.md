# Symmetry in linear arithmetic

**Status: Open lead. Owner: unassigned.**

Investigate whether symmetry detection and breaking improve QF_LIA or QF_LRA
solving. Arithmetic symmetries have been found in SMT benchmarks, but their
presence alone does not establish a benefit for Primo's current
[finite-domain symmetry pass](../implementation/preprocessing.md#finite-domains-and-symmetry).

## Existing finite-domain reasoning

The {source}`integer-choice recognizer <src/smt/preprocessing.cpp#L1725-L1770>`
accepts explicit alternatives such as $x=1\lor x=2\lor x=3$. These choices
support at-most-one (AMO) clauses and, when enabled, equality-definition
clauses. The {source}`QF_LIA profile <src/smt/preprocessing_strategy.cpp#L230-L264>`
enables this clause generation with `--preprocessing-mode=strong`; its default
profile does not run that stage. The
{source}`collection pass <src/smt/preprocessing.cpp#L1775-L1838>` does not
enumerate a domain from inequalities such as $1\le x\le3$. Arithmetic
[bound handling](../implementation/arithmetic/lia/preparation.md#input-lowering-and-integer-bounds)
is a separate mechanism.

Recognizing a finite domain does not prove its values interchangeable:
$x=1\lor x=2$ together with $x>1$ distinguishes the two values. The current
{project}`symmetry-group builder <src/smt/preprocessing.cpp#L2129-L2157>`
excludes domains recognized from integer numerals; it considers explicitly
disequal declared constants. The
{project}`symmetry pass <src/smt/preprocessing.cpp#L5202-L5234>` then checks
invariance of the assertions.
The {source}`integer-domain regression <tests/unit/preprocessing_tests.cpp#L432-L466>`
checks that AMO and equality clauses can be generated without symmetry
restrictions. A future detector could use domain information to find candidate
variable permutations, but must check the entire formula.

## Literature and reading scope

| Source | Relevant evidence and limit |
| --- | --- |
| Areces, Déharbe, Fontaine and Orbe, *SyMT: finding symmetries in SMT formulas*, §§2–4 {cite:p}`areces2013symt` | Detects symbol permutations through a colored graph that preserves interpreted symbols. Table 1 reports symmetries in 1,200 of 5,938 QF_LIA inputs and 210 of 634 QF_LRA inputs, taking the union of three configurations. These are detection results, not solver speedups. |
| Dingliwal, Agarwal, Mittal and Singla, *Advances in Symmetry Breaking for SAT Modulo Theories*, method and experiments {cite:p}`dingliwal2020symmetry` | Adds Boolean symmetry-breaking predicates derived from symmetries respecting both the Boolean skeleton and the theory expressions. The reported solver gains concern QF_NIA and a hybrid of ordinary solving and symmetry breaking; they do not establish QF_LIA/LRA gains. |
| Margot, *Symmetry in Integer Linear Programming* {cite:p}`margot2010symmetry` | Related survey of symmetry in integer programming, including scheduling identical machines. Only the abstract was checked in this survey; read the relevant algorithm sections before selecting a method for Primo. |
| Bödi, Herr and Joswig, *Algorithms for Highly Symmetric Linear and Integer Programs*, §2, especially Theorem 15 {cite:p}`bodi2011symmetry` | Reduces symmetric LPs using invariant solutions obtained by averaging. The convexity argument does not apply directly to arbitrary Boolean combinations of arithmetic constraints, and averaging need not preserve integrality. |

For example, $(x=0\land y=1)\lor(x=1\land y=0)$ is invariant under exchanging
$x,y$, over integers or reals. Adding $x\le y$ preserves a representative
solution; adding $x=y$ removes every solution. Finite domains, interchangeable
variables and convexity are distinct conditions.

## Investigation and acceptance criteria

Start with static permutations of arithmetic variables that preserve the whole
formula, including sorts, coefficients, numerals and Boolean structure.
Specify the graph representation and the proof that added restrictions retain
at least one model from each symmetry class. General detection and ILP-style
symmetry handling during branching are separate proposals; neither is
implemented by the existing finite-choice pass. For incremental sessions,
specify when restrictions cease to be valid as assertions change.

Compare unchanged Primo, the existing pass, and a proposed detector on matched
QF_LIA/QF_LRA benchmark families, including formulas with explicit finite
integer choices and bounded intervals. Measure detection cost, added clauses,
total time, solved counts and regressions. More detected symmetries or fewer
search steps alone do not establish a useful change.

Before changing defaults, require fresh differential fuzzing, independent SAT
model checks, and regressions where coefficients, numerals, bounds or later
assertions destroy a candidate symmetry. Reject or narrow the proposal if
detection and restriction costs outweigh the solving benefit. This lead
does not change the current default or claim a new arithmetic optimization.
