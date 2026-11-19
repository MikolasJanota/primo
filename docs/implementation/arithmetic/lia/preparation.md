# Integer input and integrality

This chapter explains the integer preparation contract: supported translations, exact bound normalization, integer obligations and the remaining infinitesimal cases.

Start with the [LIA overview](../../lia.md) for integration with SAT and default settings.

(lia-input)=
## Input lowering and integer bounds

[Preprocessing profiles](../../preprocessing.md#profiles-and-repetition) explains the
automatic and explicit integer pass schedules. The transformations below run
even when the optional repeated round is disabled.

Here, **lowering** means translating an input construct into simpler terms
and constraints supported by the solving stages. Each translation below
specifies the replacement and any fresh variables it introduces.

The {source}`frontend <src/smt/frontend.cpp>` admits linear `Int` expressions
and comparisons under `QF_LIA`; uninterpreted functions require `QF_UFLIA`.
The integer logics reject `Real` declarations. Products must have a constant
coefficient. Primo also accepts constant-divisor `div`/`mod` and integer
`abs` as extensions to its QF_LIA input; that is not a claim of strict QF_LIA
grammar conformance.

{source}`Extended-term lowering <src/smt/preprocessing.cpp#L4468-L4616>`
runs even when optional preprocessing is off. For positive integer $k$, a
quotient $q$ for `div(t,k)` is constrained by

$$0\le t-kq\le k-1.$$

`mod(t,k)` becomes the residual $t-kq$. Equal numerator/divisor pairs share
one quotient. The inequalities characterize a unique integer quotient and
a remainder in $[0,k-1]$: two distinct quotients would make the remainders
differ by at least $k$, which that interval cannot contain. For instance,
$-7=3(-3)+2$, so the quotient is $-3$ and the remainder is $2$.
{source}`Term construction <src/core/term_manager.cpp#L364-L393>` normalizes
a negative divisor by negating the positive-divisor quotient and preserving
the remainder. Zero and nonconstant divisors are rejected.

`abs(t)` is lowered through `ite(0 <= t, t, -t)`. Non-Boolean `ite` introduces
a fresh constant with guarded equalities to its two possible values.
Equality lowering, despite its `lower_real_equalities` name, also handles
`Int`: $a=b$ becomes $(a\le b)\land(b\le a)$. Negating the equality therefore
lets SAT choose a false non-strict comparison. Integer strictness must account
for that polarity, not just syntactic `<` atoms.

{settingssource}`bound_assertion <src/smt/solver_engines.cpp#L3193-L3220>` computes
the bound direction and strictness from the normalized relation, orientation
and truth value. When the **bounded variable** is known to be integer, it uses

$$x<r\iff x\le\lceil r\rceil-1,
\qquad x>r\iff x\ge\lfloor r\rfloor+1.$$

Thus `3*x < 5` yields $x\le1$, while a false `3*x <= 5` yields $x\ge2$.
Subtracting or adding one to the rational threshold $5/3$ would be wrong.

With `--no-lia-round-integer-bounds`, the QF_UFLIA default, non-strict
bounds keep their rational threshold: a true `3*x <= 5` asserts
$x\le5/3$. With `--lia-round-integer-bounds`, the QF_LIA default, the same
integer-valued variables also get rounded non-strict bounds,

$$x\le r\iff x\le\lfloor r\rfloor,\qquad x\ge r\iff x\ge\lceil r\rceil,$$

so every bound on such a variable is integral (King §4.3; Achterberg et al.
§4.2 {cite:p}`achterberg2020presolve`). The bound stores the **negation of the asserting SAT literal** as its reason,
including when the assignment makes the original atom false. If `p` names
the atom, a true assignment stores `¬p`, while a false assignment stores `p`. Each prepared atom stores an `IntegerBoundRounding`
mode (`None`, `StrictOnly` or `All`), and every assertion, entailment check,
propagation and model-phase hint reads it through `bound_assertion`, so all
uses agree. In `3*x <= 5`, `3*y <= 5`, `x + y >= 3` the rounded bounds
$x,y\le1$ refute the candidate in the rational check, without integer
completion. The option is on by default for QF_LIA since `5dec036` and off
for QF_UFLIA; see the
[integrality checklist](../../../papers/mip-presolve-integrality.md).

{settingssource}`PreparedLraTheory::append_atom
<src/smt/solver_engines.cpp#L3529-L3656>` marks direct bounds on `Int` terms
as integer. For compound polynomials its sufficient condition without
`--lia-primitive-int-rows` is more
conservative: the input is integer-sorted and the normalization scale is
$1$ or $-1$. Other auxiliary variables use the real strict-bound encoding,
even if a stronger analysis could prove some of them integer-valued.

With `--lia-primitive-int-rows`, `Int` atoms are stored with coprime integer
coefficients and a positive leading coefficient instead: `2*x + 3*y <= 5`
gets the row $s=2x+3y$ rather than $s=x+\tfrac32y$ (King §4.3). Every
coefficient is then an integer over `Int` variables, so the slack is integer
in every integer model and is marked integer whatever the scale (King §4.3,
footnote 4). Proportional atoms of either sign still share one row; Real
atoms keep leading-coefficient-one normalization. Together with
`--lia-round-integer-bounds` this gives the GCD rounding of Achterberg et
al. §3.5: `2*x + 4*y <= 5` becomes $x+2y\le2$. In the parity case
$2x+4y=3$, written as two inequalities, the rounded bounds $2\le s\le1$
conflict in the rational check. In a case with $13/2\le2x+3y\le7$ over
unbounded variables, with both options off (the QF_UFLIA default),
completion exhausts its branch-node limit, while the integral bound $s=7$
lets one patch move find $x=2,y=1$. Both options are on by default for
QF_LIA and off for QF_UFLIA. The SLURM evaluation at `2e120fa` found no
wrong answer. It also found that `--lia-primitive-int-rows` loses 30 QF_UFLIA
`wisas` files ({ref}`integrality evaluation <integrality-evaluation>`). The
cause, traced in four of the files, is search order: the leading-coefficient form escapes
them because its first commitment is fractional and cheap to refute.

A slack marked integer this way can also become an integer obligation of
completion. `--lia-integer-slacks` puts such slacks (Achterberg et al. §7.6
call them implied integers) at the front of `integer_vars`. Patching, the GCD
test and Gomory row selection may then use their rows, and private branch-and-bound,
which splits the first fractional variable, can split a constraint
expression: for $13/2\le2x+3y\le27/4$, the single split $s\le6\lor s\ge7$
refutes the candidate, with the two bound literals as its explanation, while
splitting only $x$ and $y$ exhausts a 50-node budget. A slack appended after
the inputs would never be split, because it is integral whenever they are.
On QF_LIA at 60 s, this option together with the two above solves 197 fewer
files than all options off, and 301 fewer than the two above alone. Adding
`--lia-patch-integral-moves` as well gives 9,798 solved files, the most of any
configuration in that historical comparison
({ref}`integrality evaluation <integrality-evaluation>`).

(lia-infinitesimals)=
### Why the relaxation can still contain infinitesimals

The current invariant is that a **completed integer assignment** has no
infinitesimal component. Intermediate relaxation assignments can have one,
including on variables corresponding to `Int` terms.

For example, let $P=2x+3y$ with integer $x,y$. Normalizing by the leading
coefficient $2$ introduces $s=x+\tfrac32y=P/2$. Consider $P<0$:

1. The current fallback represents it as $s\le-\delta$. The initial
   assignment $x=y=s=0$ violates this bound.
2. If simplex pivots on $x$, the row becomes $x=s-\tfrac32y$. Moving
   $s$ to $-\delta$ gives $x=-\delta$, $y=0$. Every real constraint holds,
   but $x$ is not integer-valued.
3. Patching can move $s$ to $-1$, giving $x=-1$, $y=0$. This satisfies the
   bound and makes both integer variables exact integers.
4. If patching does not resolve the assignment, branch-and-bound uses
   $\lfloor-\delta\rfloor=-1$ and splits on $x\le-1$ or $x\ge0$.
   The first branch admits that same integer solution.

This is a possible tableau trace, not a promised preprocessing or pivot order.

The {source}`integer-value helpers
<src/theory/arithmetic/lia_integer_values.hpp>` require both an integral
rational part and a zero infinitesimal part. In particular,
$\lfloor n-\delta\rfloor=n-1$ and
$\lceil n+\delta\rceil=n+1$ for integer $n$. Treating $n-\delta$ as having
floor $n$ would make branching repeatedly choose an already-satisfied bound.
Dutertre–de Moura's technical report §4.1, pp. 20–21, gives this delta-aware
floor for branch-and-bound {cite:p}`dutertre2006simplexreport`.
Patching targets exact integers; branch bounds have zero infinitesimal
components. The cut generator skips a basic value with a nonzero
infinitesimal, since its rational fractional-part formula does not cover
that case. The {ref}`regression map <lia-models-evidence>` links the existing
branching, patching and cut guards.

There is room to avoid introducing these infinitesimals. Since $P$ is
integer-valued, $P<0$ is equivalent to $P\le-1$, giving $s\le-1/2$ without a
delta. This must use the spacing of $P/2$: rounding $s$ as if it were an
integer would incorrectly require $s\le-1$ and exclude $x=1,y=-1$, where
$s=-1/2$. The strengthened relaxation can still have fractional assignments;
integer completion remains necessary.
King's thesis §4.3 describes the underlying integer tightening, including
strict bounds obtained from negated SAT atoms {cite:p}`king2014`.
[Preserving this spacing during bound preparation](../../../leads/lia-infinitesimals.md)
is an open lead for the leading-coefficient normalization, which QF_UFLIA
still uses. Under `--lia-primitive-int-rows`, the QF_LIA default, the slack is $P$ itself, so a strict compound
`Int` atom becomes an integral non-strict bound and no infinitesimal is
introduced. Simply discarding a nonzero delta would change a strict
inequality and is not a valid replacement.

## Sources and regression map

The overview collects [primary sources and implementation status](../../lia.md#sources-and-implementation-status) and [regression obligations](../../lia.md#models-regression-coverage-and-retained-evidence).
