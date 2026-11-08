# Cuts from proofs: integer-infeasible intersections

Dillig, Dillig and Aiken's *Cuts from proofs: a complete and practical
technique for solving linear inequalities over integers*
{cite:p}`dillig2011cuts` explains how to branch on an integer linear
expression that excludes an entire intersection with no integer points.
This is a potential extension to Primo's coordinate-wise branching, not an
algorithm currently implemented in Primo.

## Source and reading scope

The supplied PDF is the **2011 journal article**, *Formal Methods in System
Design* 39, pp. 246–260, DOI
[10.1007/s10703-011-0127-z](https://doi.org/10.1007/s10703-011-0127-z).
An [author-hosted journal manuscript](https://www.cs.utexas.edu/~isil/fmsd.pdf)
is available online. The historical local reading copy was:

```text
external/papers/Cuts_from_proofs:_a_complete_and_practical_technique_for_solving_linear_inequalities_over_integers.pdf
```

This note uses §§1–4, particularly Lemma 4, the algorithm in §3.1, and the
termination argument in §3.3; §§5–7 provide experimental scope and context.
Bjørner–Nachmanson §4.5 {cite:p}`BjornerN24` instead cites the 2009 CAV
publication with this title and *Cutting the Mix* as sources for additional
cut machinery. This note does not assume those versions or algorithms are
identical.

The main qualification for our completeness discussion is explicit in
§3.3: the proof assumes a bounded polyhedron, using satisfiability-preserving
variable bounds if necessary. It also limits the coefficients of generated
split directions. Adding proof-derived splits without those obligations
would not transfer the paper's termination guarantee to Primo.

## From a fractional assignment to an integer split

The input is a conjunction $Ax\le b$, with $A\in\mathbb Z^{m\times n}$,
$b\in\mathbb Z^m$, and integer unknowns $x\in\mathbb Z^n$. Its rational
relaxation permits rational values for $x$. The output is an integer model
or a proof that none exists. Arbitrary Boolean structure is outside this
conjunction algorithm; an SMT integration must supply it.

At a rational feasible assignment $v$, an inequality $a_i x\le b_i$ is
**defining**, or tight, when $a_i v=b_i$. Collect these equalities as
$Dx=d$. Their intersection contains the current assignment. If that
intersection has no integer point, excluding the whole intersection can be
more useful than excluding $v$ by splitting one coordinate.

The paper asks its LP routine for an extreme-point solution. This is a
precondition to check during integration: a feasible assignment from Primo's
bound-repair tableau is not automatically such a point. Polyhedra with
unbounded lines may have no extreme points at all. The paper's §1 example
also uses a point on an unbounded intersection; therefore we must state the
representation and any bounding reduction explicitly rather than assume
that every rational model meets the LP interface described in §3.

### Extracting the equation

For a full-row-rank integer matrix $D$ with $r$ rows, column Hermite normal
form gives a unimodular integer matrix $U$ such that

$$
DU=[H\mid0].
$$

Here $H$ is a nonsingular $r\times r$ triangular integer matrix;
**unimodular** means $\det U=\pm1$, so the coordinate transformation
preserves the integer lattice in both directions. Dependent rows must be
removed consistently; the current rational model guarantees consistency
of the tight equalities. Lemmas 3–4 establish that $H^{-1}D$ has integer
entries and that $Dx=d$ is integer-feasible exactly when
$H^{-1}d\in\mathbb Z^r$.

Suppose a row $q$ of $H^{-1}D$ corresponds to a nonintegral reduced fraction
$p/s$ in $H^{-1}d$, with $s>0$. Then

$$
q x=p/s,\qquad (s q)x=p
$$

is implied by the tight equalities and impossible for integer $x$.
More generally, write the resulting integer equation as $a x=c$, and let
$g=\gcd(|a_1|,\ldots,|a_n|)>0$. Integer infeasibility means $g\nmid c$.
Every integer assignment satisfies the disjunction

$$
(a/g)x\le\lfloor c/g\rfloor
\quad\lor\quad
(a/g)x\ge\lceil c/g\rceil.
$$

Both alternatives exclude the impossible equality. **Neither alternative
alone is necessarily implied by the original inequalities.** They are two
search branches, unless one side is separately refuted. A proof that the
tight equalities have no integer model is not a proof that the entire
inequality system has no integer model.

### A worked split

Consider the integer problem from our
{ref}`LIA limitation <lia-limits>`:

$$
2\le4x+4y\le3.
$$

A rational solution on the lower boundary satisfies $4x+4y=2$. This
equality itself is a certificate: its coefficient GCD is $4$, which does
not divide $2$. The split becomes

$$
x+y\le0\quad\lor\quad x+y\ge1.
$$

The left branch contradicts $4x+4y\ge2$; the right contradicts
$4x+4y\le3$. Both rational relaxations are infeasible, so the integer
problem is infeasible. This illustrates the split rule even though the
original unbounded strip has no extreme point. It does not establish a
complete LP-selection strategy for arbitrary unbounded inputs.

For an example requiring combined equalities, the paper's Example 2 uses
$z=0$ and $-3x+3y+z=-1$. Its HNF calculation derives
$-3x+3y+3z=-1$, giving the split
$-x+y+z\le-1$ or $-x+y+z\ge0$. The useful equation can therefore be a
combination of defining constraints, not merely an individually
inconsistent input row.

## Search, progress and termination

The algorithm in §3.1 fixes a threshold
$\alpha\ge n\max_{i,j}|A_{ij}|$ from the initial system. At each node it:

1. Solves the rational relaxation, closing an infeasible node or returning
   an integral model immediately.
2. Extracts the defining equalities and checks their integer feasibility.
3. Branches around an infeasible equation only if every normalized
   coefficient satisfies $|a_i|/g\le\alpha$.
4. Otherwise branches on a fractional coordinate using its floor and ceiling.

The coefficient restriction is mathematical, not a timeout or a maximum
number of cuts. Removing it is not the same as giving this algorithm
unlimited resources. Lemmas 5–7 explain the demand-driven exclusion of
integer-infeasible faces. Here a **face** is the part of the polyhedron
where selected valid inequalities hold as equalities. Appending defining
rows preserves the relevant
HNF proof for an earlier row prefix, and the coefficient bound admits
proofs from subsets of the original constraints. Row order and dependent
row handling therefore belong in an implementation's proof obligations.

The termination argument in §3.3 combines three facts:

- The search region is bounded. For unbounded inputs, the paper invokes
  computable variable bounds that preserve satisfiability, citing integer
  programming textbooks. Such bounds can remove models outside the box;
  they must preserve the existence of a model.
- Primitive integer split normals have bounded coefficients, so there are
  finitely many possible directions. Coordinate splits add only finitely
  many additional directions.
- For a fixed normal $q$, distinct parallel planes $qx=k$ with integer
  offsets have distance at least $1/\|q\|_2$. Only finitely many such planes
  intersect a bounded region. A split excludes the current rational model,
  so it makes progress along the branch.

Together these give finite search. They do not imply that arbitrary
proof-derived splitting terminates on an unbounded region. The article
refers out for constructing the variable bounds and the detailed HNF
algorithm; this reading note is not a complete implementation specification
for either dependency.

HNF and integer-equation feasibility admit polynomial-time algorithms
(§2.3); integer-inequality feasibility remains NP-complete. Polynomial
certificate extraction does not make the complete search polynomial.
Section 4 delays a proof split until the same proof occurs again, and uses
$\alpha=10n\max|A_{ij}|$ in the implementation. These are engineering
choices. Its observations about HNF cost and branch frequencies are
historical measurements, not predictions for Primo.

## Representation and integration checklist for Primo

The following is a proposed decomposition, not implemented code. Source
claims were checked at revision `934b6ce`; the invocation map below also links
the later shared integer-completion owner.

| Operation or state | Existing owner and missing work |
| --- | --- |
| Rational feasibility and active bounds | {source}`LraTableau <src/theory/arithmetic/lra_tableau.hpp>` owns rows, assignments and trailed bounds. An extractor would need exact tight integer constraints and their reasons, with explicit treatment of rationally scaled slacks and infinitesimals. |
| Integer matrix and proof equation | A new owner under `theory/arithmetic/` would hold exact integer matrices, rank information, stable variable IDs and certificate provenance. No HNF certificate extractor is present in the current integer completion path. |
| Search and split lifetime | {source}`branch_and_bound <src/theory/arithmetic/lia_branch_and_bound.cpp>` currently stores one variable and a floor value per DFS frame. General expression splits need a representation, bounds and rollback for any added tableau rows. |
| Explanations | {source}`LiaExplanation <src/theory/arithmetic/lia_explanation.hpp>` accumulates antecedents of existing cuts and closed branches. A new proof split must preserve the distinction between a globally valid integer disjunction and either conditional branch. |
| Invocation and ownership | {integersource}`complete_integer_assignment <src/theory/arithmetic/lia_completion.cpp#L13-L35>` runs patching, a GCD check, Gomory rounds and then DFS for the standalone and combined LIA component routes. Integer completion uses a private tableau; current cuts precede DFS, rather than running at each branch node. |
| SMT terms and Boolean search | Any CDCL-visible split must pass through SMT abstraction and terms created by the term manager. The arithmetic layer must not construct SMT-LIB AST nodes or encode terms directly in SAT literals. |

The current {source}`Gomory producer
<src/theory/arithmetic/lia_gomory_cuts.cpp>` requires eligible fractional
basic integer variables with nonbasic row terms at bounds. It adds valid
inequalities derived from those bounds. This is different from finding an
integer-infeasible intersection and exploring a disjunction around its
certificate. Existing GCD reasoning is also not a general HNF test of a
system of defining equalities.

Before implementing a complete route, the design needs the following
checklist resolved:

- Select and read the primary algorithms for exact HNF and the
  satisfiability-preserving bounding construction. Record their complexity
  in matrix dimensions and coefficient bit lengths, not just arithmetic
  operation counts.
- Specify the LP point and tight-row contract, including rank-deficient
  systems, lineality, equality constraints and scaled auxiliary variables.
- Retain enough provenance to verify the derived equation, integer
  coefficients, GCD, coefficient bound and both split alternatives.
- Own proof matrices and split rows for explicit lifetimes; reacquire
  tableau views after mutation and restore all branch-local state on pop.
- Prove the finite-search argument for the actual scheduling and rollback
  strategy. Resource exhaustion remains `unknown`, never a refutation.

Candidate regressions should cover the strip above; the paper's combined
certificate; redundant tight rows; negative floor/ceiling values; a
certificate exceeding $\alpha$ and falling back to coordinate branching;
a tight intersection with no integer point inside an otherwise satisfiable
polyhedron; and rollback across sibling branches and repeated queries.
Counters should distinguish HNF calls, certificates, oversized rejections,
proof splits, coordinate fallbacks and immediately refuted split sides.
These tests and counters are proposed, not newly implemented or measured.

## Sources and implementation status

**Deferred research direction.** Primo has the rational tableau and
coordinate branching needed for comparison, but does not implement this
paper's proof extraction, general expression branching, or complete bounded
search construction. A first study could measure certificate usefulness on
stalled integer searches; it must not be presented as a complete solver
until the missing obligations are discharged.

Section 5 evaluates conjunctions of integer inequalities against historical
solver versions, deliberately excluding arbitrary Boolean SMT-LIB formulas
from that comparison. Its performance claims do not establish an advantage
on Primo's six divisions or retained sessions. Any implementation would
require fresh SLURM correctness and performance qualification.

For a different route to bounded integer search, see
{ref}`Bromberger's transformation-based approach <bromberger-bounded-search>`.

The current implementation priority is the smaller [QF_LIA cube test](../leads/lia-cube-test.md). Proof-derived splitting remains deferred; the plan does not relabel a cube feasibility test as Cuts-from-Proofs.
