# Avoiding infinitesimals in integer relaxation

**Status: Open lead for the default normalization; addressed by an option.**
Under `--lia-primitive-int-rows` (off by default), `Int` slacks are integer
polynomials, so strict compound `Int` atoms get integral bounds without an
infinitesimal; see the [integrality checklist](../papers/mip-presolve-integrality.md).
The spacing rule below remains the alternative if the leading-coefficient form
is kept for `Int` atoms.

Preserve the discrete values of an integer polynomial when preparing its
scaled tableau bounds. This could eliminate the strict-bound infinitesimals
currently introduced by compound integer atoms. It is a proposed change to
bound preparation, not an implemented alternative arithmetic representation.

## Current behavior and the gap

{source}`PreparedLraTheory::append_atom
<src/smt/solver_engines.cpp#L3434-L3512>` shares normalized polynomials with
leading coefficient one. Its `is_integer` flag covers direct integer terms
and compound integer atoms whose scale is $1$ or $-1$. Otherwise,
{source}`bound_assertion <src/smt/solver_engines.cpp#L3169-L3187>` uses
infinitesimals for strict bounds. The
{ref}`integer chapter <lia-infinitesimals>` explains how these can reach
integer variables and how the existing procedures handle them.

The flag records only whether the bounded variable is known to be integer.
It does not record a finer rational spacing. In an integer model, an auxiliary
$s=P/2$ with integer-valued $P$ is restricted to half-integers.
Using this fact could both remove a delta and strengthen the real relaxation.

## Related work

Integer-bound tightening is established. The relevant primary sources are:

| Source | Relevant result and relation to Primo |
| --- | --- |
| King, thesis, §4.3, pp. 106–109 {cite:p}`king2014` | Clear rational denominators, replace strict integer inequalities by non-strict ones, divide coefficients by their GCD and round the bound. The assertion procedures also tighten negated inequalities received from SAT. This directly supports preparing exact bounds for both polarities. |
| Pugh, *The Omega Test*, expanded 1992 version, §2.1 {cite:p}`pugh1992omega` | Earlier coefficient-GCD normalization and rounding of inequality constants, preserving integer solutions while strengthening the real relaxation. |
| Griggio, thesis, §2.6.1, equations (2.11)–(2.12) {cite:p}`griggio2009` | Applies the same GCD-and-rounding rule in an SMT integer solver after equality elimination, citing Pugh. Primo need not adopt that elimination procedure to use the bound rule. |
| Achterberg–Bixby–Gu–Rothberg–Weninger, *Presolve Reductions in Mixed Integer Programming*, ZIB Report 16-44 version, §§3.5, 4.2, 7.6 {cite:p}`achterberg2020presolve` | MIP presolve counterparts: §3.5, equation (3.7), divides an all-integer row by the GCD of its coefficients and rounds the right side; §4.2 rounds fractional bounds of integer variables, which Gurobi's MIP algorithms require; §7.6 marks a continuous variable as implied integer when an equation with integer variables forces its integrality. The paper reports a small overall benefit for §3.5 and a larger one for §7.6 in Gurobi; neither measurement transfers to Primo. |
| Dutertre–de Moura, technical report, §4.1, pp. 20–21 {cite:p}`dutertre2006simplexreport` | Branch-and-bound over delta rationals, including $\lfloor n-\delta\rfloor=n-1$. This supports the existing fallback in Primo's {source}`integer-value helpers <src/theory/arithmetic/lia_integer_values.hpp>` and {source}`branching procedure <src/theory/arithmetic/lia_branch_and_bound.cpp>`. |

The proposed work adapts integer tightening to Primo's leading-coefficient-one
normalization, where the shared auxiliary can have fractional spacing. King's
primitive integer normalization cannot simply be assumed for these auxiliaries.
The sources above do not establish a performance benefit for this adaptation.

## Proposed rule and justification

The following derives the bound rule in Primo's normalized coordinates.
Let $P=\sum_i a_ix_i$ have integer coefficients and integer variables,
with its constant moved to the bound. If normalization stores $s=P/c$,
$c\ne0$, then every integer model has $s\in h\mathbb Z$, where
$h=1/|c|>0$. This is a sound containing grid; it need not be the tightest one.

After accounting for the comparison's orientation and SAT truth value,
replace its bound using:

| Relation on $s$ | Equivalent bound for integer models |
| --- | --- |
| $s<r$ | $s\le h(\lceil r/h\rceil-1)$ |
| $s\le r$ | $s\le h\lfloor r/h\rfloor$ |
| $s>r$ | $s\ge h(\lfloor r/h\rfloor+1)$ |
| $s\ge r$ | $s\ge h\lceil r/h\rceil$ |

Each rule follows by writing $s=hk$ with integer $k$. The tightened bound
has no infinitesimal. For $P=2x+3y$ and $P<6$, it gives $s=P/2\le5/2$.
The relaxation may still assign fractional values to integer variables;
these bounds do not replace integer completion.

An alternative is to tighten the unscaled integer polynomial before dividing
by $c$. Either design must cover both SAT polarities: normalizing a syntactic
`<` alone misses the strict comparison obtained from a false `<=` atom.
Never round an arbitrary auxiliary to the nearest integer or erase its
infinitesimal component without deriving an equivalent bound.

## Implementation and verification obligations

- Keep the spacing or equivalent original-scale information in the prepared
  atom. Prove it from the input polynomial, independently of the changing
  tableau basis. If using normalized coefficients to infer a grid instead,
  derive it by exact denominator clearing; do not assume integer coefficients.
- Use the same effective bound for assertions, propagation, entailment checks
  and model validation. Preserve the originating SAT literal as its reason.
  Cover atoms appended during theory combination as well as initial atoms.
- Audit every producer of assignment values before claiming that the entire
  integer relaxation stays delta-free. Rational linear updates preserve zero
  infinitesimals, but this alone does not cover all assignment-selection paths.
  Retain `DeltaRational` and the existing defensive guards for shared real
  arithmetic machinery.
- Add dedicated regressions for positive and negative scales, both polarities,
  negative and fractional thresholds, legitimate fractional auxiliaries,
  shared polynomial rows, incremental scopes and combined integer solving.
  Keep the existing delta-only branching and cut-skip regressions.
- Record delta-bearing bounds and assignments, cut eligibility and branch work
  to establish whether the change removes useful work. Qualify solver changes
  with unit/ASan checks, fresh reference fuzzing and independent model checks;
  run solver batches and baseline comparisons through SLURM in this workspace.

No implementation or performance result is claimed. Wider scalar-storage
changes belong to the separate [rational-storage lead](rational-storage.md).
