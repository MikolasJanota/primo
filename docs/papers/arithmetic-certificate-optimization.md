# Arithmetic certificate coefficients and interpolation

**Status: focused primary-source survey, 11 November 2026; native solver unchanged.**
Earlier work read selected sections of the SMTInterpol SPIN 2012 description,
*Proof Tree Preserving Interpolation* (2013/2017), and the SMT 2022 proof-format
paper. That was a proof-contract reading, not a broad survey of coefficient
optimization. This note expands it from the
[SMTInterpol publication list](https://ultimate.informatik.uni-freiburg.de/smtinterpol/publications.html)
to interpolation quality, integer cuts and certificate checking.

The central result is that the literature already formulates Farkas-based
interpolation as constraint solving and explicitly proposes simplicity and
sparsity restrictions. Our bounded min-max/L1 adapter is one particular
objective, not the origin of this approach. Small interpolant coefficients are
useful objectives to investigate, but small proof multipliers alone do not
ensure them. No universal relation to verification convergence follows.

## Reading scope

The {download}`reading register <../coverage/arithmetic-certificate-literature-20261111.json>`
records the primary URLs, PDF hashes, page counts, read sections and exclusions.
Only papers and format documentation were consulted; no other solver's
implementation was read or copied. PDFs are external reading copies and are
not redistributed here. The survey is focused, not an exhaustive bibliography.

| Primary source | Sections read for this question | What it establishes, and what it does not |
| --- | --- | --- |
| Christ–Hoenicke–Nutz, *SMTInterpol: An Interpolating SMT Solver*, SPIN 2012 {cite:p}`christ2012smtinterpol` | §§2–4 | Proof/interpolation architecture; integer interpolation uses an extended vocabulary. Not a coefficient-minimization algorithm. |
| Christ–Hoenicke–Nutz, *Proof Tree Preserving Interpolation*, TACAS 2013, improved author version 2017 {cite:p}`christ2017proof` | §5 | Weighted arithmetic leaves and mixed-literal handling; rational infeasibility and purely integer infeasibility require different reasoning. |
| Christ–Hoenicke, *Proof Tree Preserving Tree Interpolation*, JAR 2016 {cite:p}`christ2016tree` | §7, especially §§7.3–7.4 | Leaf weights affect partial interpolants. Integer mixed-pivot combination uses finite case distinctions; its cost depends on coefficients and range parameters. The paper explicitly chooses the orientation with fewer disjuncts. |
| Christ–Hoenicke, *Cutting the Mix*, CAV 2015 {cite:p}`christ2015mix` | §1, §4 discussion of constraint ordering, §5 construction overview | Cut generation and basis/constraint choices matter before export. Its mixed-arithmetic/Hermite construction is not our postprocessor or Primo's bounded projection reconstructor. |
| Hoenicke–Schindler, *A Simple Proof Format for SMT*, SMT 2022 {cite:p}`hoenicke2022simple` | §4.1/Fig. 3, §§5–6 | Exact Farkas rules, integer-specific axioms, proof streaming and term sharing. It does not specify our min-max/L1 objective. |
| Hoenicke–Schindler, *SMTInterpol with resolution proofs*, SMT-COMP 2022 {cite:p}`hoenicke2022competition` | Description; Proof Format; Proof Checker | A small proof kernel still relies on parsing, typing and term representation. This is an implementation description, not evidence that coefficient magnitude alone determines checking cost. |
| Albarghouthi–McMillan, *Beautiful Interpolants*, CAV 2013, extended author copy {cite:p}`albarghouthi2013beautiful` | §4; Appendices B.2–B.3 | Linear constraints synthesize separating inequalities and Farkas witnesses together. B.3 explicitly tries unit separator coefficients and few variables, then relaxes restrictions; it also suggests maximizing zero Farkas multipliers. This is directly relevant prior art. |
| Scholl–Pigorsch–Disch–Althaus, *Simple Interpolants for Linear Arithmetic*, DATE 2014 {cite:p}`scholl2014simple` | §§II–III | LP/SMT problems seek inequalities shared by multiple theory conflicts, reducing distinct inequalities in the final interpolant. This is not minimization of multiplier bit height. |
| Alt–Hyvärinen–Sharygina, *LRA Interpolants from No Man's Land*, HVC 2017 {cite:p}`alt2017land` | §§1–3.1 | A strength parameter varies interpolants between primal and dual choices. Logical strength is a separate objective from numeral size and syntax size. |
| Blicha–Hyvärinen–Kofroň–Sharygina, *Decomposing Farkas Interpolants*, TACAS 2019 {cite:p}`blicha2019decomposing` | §§3–5.2, Algorithm 1 | Decompose the A-side witness into nonnegative components that each eliminate A-local variables. Their conjunction can strengthen the ordinary interpolant. This may increase the number of inequalities. |
| Cheung–Gleixner–Steffy, *Verifying Integer Programming Results*, IPCO 2017, revised report {cite:p}`cheung2017verifying` | §§2–5 | Exact linear combination, rounding and branch assumptions form verifiable certificates. §5 removes unused derivations, orders dependencies and records last use to release storage. These are separate from shrinking numerical weights. |
| Schurr–Fleury–Desharnais, *Reliable Reconstruction of Fine-Grained Proofs in a Proof Assistant*, CADE 2021 {cite:p}`schurr2021reconstruction` | §§4.3, 4.5 | Explicit arithmetic coefficients and integer strengthening reduce reconstruction search. Eliminating redundant reconstruction steps is another distinct optimization. |
| Barbosa, *Challenges in SMT Proof Production and Checking for Arithmetic Reasoning*, SC-Square 2023 {cite:p}`barbosa2023arithmetic` | §§2.1–2.1.1 | Separates Farkas witnesses, integer bound tightening and coarse-step elaboration. Supplying a witness saves search; this does not establish that minimizing its coefficients improves a given checker. |

The current [RESOLUTE arithmetic documentation](https://ultimate.informatik.uni-freiburg.de/smtinterpol/proof-format.html#arithmetic)
was also inspected. Its positive-integer `farkas` multipliers and `total-int`
axiom describe checking rules, not a coefficient-optimization procedure.
The [existing adapter note](../leads/farkas-coefficient-minimization.md#reading-scope-and-attribution)
explains our adaptation to rational weights and retained zero positions.

## Which coefficients are small?

Consider a conjunction of non-strict rational inequalities, partitioned into
$A: C_Ax\le r_A$ and $B: C_Bx\le r_B$. A Farkas witness consists of
$\lambda_A,\lambda_B\ge0$ with

$$
\lambda_A^T C_A+\lambda_B^T C_B=0,\qquad
\lambda_A^T r_A+\lambda_B^T r_B<0.
$$

For this pure LRA conjunction setting, the ordinary A-side interpolant is

$$
I:\quad \underbrace{\lambda_A^T C_A}_{\alpha^T}x
       \le\underbrace{\lambda_A^T r_A}_{\beta}.
$$

Nonnegative combination proves $A\Rightarrow I$. Combining $I$ with the
B-side sum yields a contradiction. An A-local variable has no B-side
coefficient, so its coefficient in $I$ must vanish by full cancellation;
B-local variables do not occur in $A$. Thus $I$ uses only shared variables.
This recalls the leaf construction, not a complete interpolation procedure for
arbitrary Boolean combinations, mixed literals, or integer cuts.

There are three different numerical objects: the input entries of $C_A,C_B$,
the witness multipliers $\lambda$, and the resulting interpolant coefficients
$\alpha,\beta$. Our current adapter optimizes the second object. Interpolation
quality depends directly on the third, its variable support, its Boolean
structure, and its use in a verification procedure.

Two newly authored examples show why these objectives differ. Let integer
$M\ge2$ and let $x,y$ range over the reals; all variables are shared.

1. With $A: Mx+y\le0$ and $B: -Mx-y\le-1$, unit multipliers give a
   contradiction, but $I: Mx+y\le0$ has an arbitrarily large primitive
   coefficient. Small multipliers do not guarantee small interpolant numerals.
2. With $A: x\le0\ \land\ x+My\le0$ and
   $B: -x-y\le-1$, multipliers $(M-1,1;M)$ cancel both variables and give
   $0\le-M$. The A-side sum is $Mx+My\le0$, which normalizes to $x+y\le0$.
   Here large primitive multipliers produce a unit-coefficient interpolant.

Multiplying every multiplier by the same positive rational scales $I$ by that
factor and leaves its meaning unchanged. Clearing denominators and removing
common integer content restores the same primitive inequality. Common-scale
reduction can save representation cost, but does not select a semantically
new pure-LRA leaf interpolant. Relative-weight or support changes can do so.
This observation is our direct algebraic inference, not a new literature claim.

## The useful interpolation techniques

*Beautiful Interpolants* is the closest match to the request for a separate
optimization problem. Its §4 introduces unknown separator coefficients and
one witness per sample, with linear constraints connecting them. Appendix B.3
suggests a staged search: restricted simple separators first, then less
restrictive ones, and sparse witnesses. The unit coefficients there refer to
the **separator**, not a requirement that every Farkas multiplier equal one.
A target for future work is therefore a small coefficient alphabet and few
variables in $I$, with an explicit sparse-witness objective as a separate option.
We have not implemented that synthesis algorithm.

*Simple Interpolants* adds an important qualification to aggressive shrinking.
Its §III-C discusses how minimized theory conflicts can restrict local
interpolant directions. It enlarges the available choices using controlled
proof transformations and implied literals, then seeks shared interpolants
across conflicts. Those transformations require their proof/partition
conditions: adding arbitrary premises to our exported lemmas is not a
weights-only edit. Also, shrinking a whole set of theory lemmas with CORE/MUS,
shrinking the premise support of one lemma, and shrinking the final interpolant
are three separate tasks.

*Decomposing Farkas Interpolants* instead keeps the total A-side witness and
seeks $\lambda_A=\sum_j u_j$, where $u_j\ge0$ and each $u_j$ eliminates
A-local variables. Its §5.1 separates rows that already have no local variables;
§5.2 uses the null space of the local-variable matrix. Algorithm 1 checks
nonnegativity of the resulting components and falls back to the original
witness when the chosen basis is unsuitable. A null-space basis alone is not
a nonnegative decomposition. This is an interpolation strategy, not merely
numerical GCD reduction or our existing candidate heuristic.

*No Man's Land* and the SMTInterpol journal extension reinforce the distinction
between syntactic simplicity and the rest of the interpolation algorithm:
strength selection and mixed-integer pivot elimination have their own costs.
We should measure normalized numeral size, variable/atom counts, reconstruction
cost and verification convergence separately. None is measured by the current
coefficient-adapter sweep.

## LIA: preserving a cut versus choosing an interpolant

The [current integer rule](../reference/qf-lia-certificates.md#integer-rule-and-supported-fragment)
combines previously available integer rows $a_i x\le b_i$ with weights
$w_i\in\mathbb Z_{\ge0}$, then divides by $d\in\mathbb Z_{\ge1}$ and floors
the bound. The divisor must divide every resulting variable coefficient.
Plain Farkas infeasibility of the original real relaxation is insufficient for
all integer contradictions; the SMTInterpol papers and the VIPR rounding rule
make the missing integer reasoning explicit.

For a **fixed** target cut $a x\le b$, preserving its exact conclusion is
an integer linear feasibility problem:

$$
\sum_i w_i a_i=d a,\qquad
 d b\le\sum_i w_i b_i\le d(b+1)-1,\qquad
 w_i\ge0,\quad\sum_i w_i\ge1,\quad d\ge1.
$$

The first equation ensures divisibility and the correct left side. The bound
interval is exactly $\lfloor\sum_i w_i b_i/d\rfloor=b$, including negative
bounds. Products with $d$ remain linear because $a,b$ are fixed constants.
Dividing the weights **and divisor** by a common positive integer factor
preserves the quotient and floor. An unreferenced terminal contradiction can
use the looser requirement of zero left side and any negative bound.

This model is our derivation from Primo's cut contract; the survey did not
identify it as a named SMTInterpol coefficient-minimization algorithm. It is
**implemented experimentally** in {coefficientextensionsource}`scripts/lia_coefficients.py <scripts/lia_coefficients.py>` and the
streaming CLI; see the [adapter contract](../leads/farkas-coefficient-minimization.md#representation-and-exact-integer-encoding). Allowing both $d$ and the target
coefficients $a,b$ to vary would introduce products of unknowns; the linearity
claim does not extend to that different problem. Integer rounding and mixed
literals also mean the pure-LRA scaling argument above is not a complete LIA
interpolation correctness argument.

## Checking cost, sharing and lifetime

The proof-format and reconstruction papers justify supplying explicit witnesses
and retaining integer-strengthening steps, so a checker need not rediscover
arithmetic proofs. SMT 2022 §6 separately describes streaming proofs and naming
shared subterms. VIPR §5 prunes unused constraints, topologically reorders them
and records their last references so storage can be released. These mechanisms
address proof volume and live state as well as numeral representation.

Primo's LIA reconstructor already [keeps only ancestors of the final
contradiction](../reference/qf-lia-certificates.md#reconstruction-ownership-and-limits).
The optional coefficient adapter now releases arithmetic rows after their last
use while replaying LIA cuts. It does not add last-use annotations to the output,
change checker storage or implement RESOLUTE-style term sharing.
The [109-case replay](../evidence/checker-replay-20261110.md) is evidence about
bounded outcomes; allocation diagnostics do not establish their internal cause.
This literature survey neither inspects the checker implementation nor claims
that a particular mechanism fixes those diagnostics.

## Implementation map and next checks

| Mechanism | Primo status and owner |
| --- | --- |
| Exact witness validation and simple candidate passes | Implemented in {coefficientextensionsource}`valid and cheap <scripts/farkas_coefficients.py>`. No A/B partition is consumed. |
| Bounded integer min-max then L1 objective | Implemented in {coefficientextensionsource}`optimization_problem <scripts/farkas_coefficients.py>`. It does not optimize the derived interpolant or synthesize shared separators. The optional `support` objective now prepends the positive-weight count. |
| Streamed weight replacement and atomic publication | {coefficientextensionsource}`transform <scripts/minimize_lra_coefficients.py>` streams version-2 QF_LRA; the CLI {coefficientextensionsource}`main <scripts/minimize_lra_coefficients.py>` owns temporary output, flush/fsync and atomic replacement. |
| Fixed-conclusion LIA cut optimization | Implemented experimentally by `cut_optimization_problem`, `cheap_cut` and `replay_cut` in {coefficientextensionsource}`scripts/lia_coefficients.py <scripts/lia_coefficients.py>`; CLI {coefficientextensionsource}`Optimizer.improve_cut <scripts/minimize_lra_coefficients.py>` checks conclusion preservation. Native export and checking rules are unchanged. |
| Interpolation-directed synthesis, decomposition and strength control | Literature-backed future work; no claim that the certificate postprocessor implements these algorithms. |

Before implementing an interpolation-directed option:

1. Supply the A/B partition and auxiliary-symbol provenance, externally or in
   an extension. The current certificate does not retain those maps or an
   interpolation annotation for the propositional proof. Prepared-CNF
   interpolation does not by itself prove interpolation of the original input.
2. Separate objectives for multipliers, normalized interpolant coefficients,
   variable support, number of inequalities, and checking cost. Choose explicit
   normalization before optimizing homogeneous rational expressions.
3. Keep every proposed arithmetic witness exact-checked. Regenerate any
   weight-dependent interpolation annotations and check implication, shared
   vocabulary and, where applicable, sequence/tree inductivity.
4. Test the two examples above, common-scale invariance, alternative supports,
   local-variable cancellation and failed nonnegative decompositions. For LIA,
   include negative floor, joint weight/divisor reduction and preservation of
   all downstream cut conclusions.
5. Compare witness generation and checker cost separately from interpolation
   quality and application convergence. The existing real-case sweep found
   no extra verified case from bounded coefficient optimization; it did not
   evaluate interpolation at all.
