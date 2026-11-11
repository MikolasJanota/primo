# What the arithmetic paper explains about Certora

Bjørner and Nachmanson's *Arithmetic Solving in Z3*, CAV 2024
{cite:p}`BjornerN24`, explains a concrete Certora breakthrough in §5.4,
Example 6, and publishes feature ablations in §7/Figure 3. The headline
Certora improvement is mostly on nonlinear encodings. It is a weak basis
for predicting the same gain on our QF_UFLIA selection.

This investigation rereads the supplied PDF, audits the authors' experimental
CSVs and profiles four current Certora gaps. It adds no solver algorithm and
changes no defaults.

## The 217 versus 168 result mixes four encodings

The authors' [supplementary data](https://github.com/z3prover/doc/tree/master/arithmetic/data)
contains 308 paths: 77 verification conditions with four encodings each. The
benchmark headers explain that LIA is usually an overapproximation, NIA is
usually a precise encoding, and UF/UFDT are alternative hashing-function
encodings. The BV encodings mentioned in those headers are outside this
paper's 308-row table. There are 76 quantifier-free inputs plus one quantified
input in each encoding group.

Recomputing the authors' default and legacy CSVs at **600 wall-clock seconds**
reproduces the paper's table exactly:

| Fragment | Inputs | Z3 default | Z3 legacy | Net difference |
| --- | ---: | ---: | ---: | ---: |
| QF_UFLIA | 76 | 44 | 43 | +1 |
| QF_UFDTLIA | 76 | 56 | 56 | 0 |
| QF_UFNIA | 76 | 60 | 32 | +28 |
| QF_UFDTNIA | 76 | 55 | 35 | +20 |
| Four quantified inputs | 4 | 2 | 2 | 0 |
| Total | 308 | 217 | 168 | +49 |

Thus 48 of the net 49 extra solves are on nonlinear variants. On QF_UFLIA,
the net one is five gains and four losses, not one universally improved input.
The 600-second/2 GB experiment used Z3 4.12.5; our current 120-second/8 GiB
Primo/cvc5 comparison is a separate measurement. The author CSVs do not
contain input hashes, so shared names alone do not establish byte identity
with the 2026 corpus.

The [pinned sources](../../benchmarks/certora-paper-20261111/paper-sources.json)
identify 18 CSVs by Git blob and SHA256. The
{download}`auditor <../../scripts/qualification/certora_paper/paper.py>`
requires process return code zero, a SAT/UNSAT flag, and `T_wc <= 600`.
Using the separate `T_norm` field would give 219 default solves and would not
reproduce the published 217. The
[audit result](../../benchmarks/certora-paper-20261111/paper-ablation.json)
retains every per-logic count and the default/legacy gain and loss sets.

Three ablations make the nonlinear contribution more explicit:

| Author option disabled | All 308: solved | QF_UFLIA: solved | QF_UFNIA: solved |
| --- | ---: | ---: | ---: |
| None, default | 217 | 44 | 60 |
| `smt.arith.nl.incremental_linearization` | 183 | 44 | 40 |
| `smt.arith.nl.propagate` | 171 | 44 | 32 |
| `smt.arith.nl.grobner.propagate_linear` | 216 | 44 | 59 |

These are individual ablations, not additive contributions or proofs that an
inactive feature has no possible effect on runtime. In particular, the label
“no-propagate” in these data disables **nonlinear** propagation; it is not our
`--no-lra-propagation` control below. The concrete example establishes that
linear consequences from nonlinear reasoning can be decisive, even though
that feature's aggregate ablation loses only one solve here.

## What Example 6 actually adds

In §5.4, nonlinear completion derives a linear relation between existing
monomial variables, then gives it to simplex. Extended GCD/bound reasoning
can use that relation to refute a modular-arithmetic condition. This is
stronger than checking nonlinear facts and linear facts independently.

A simpler illustration of the same transfer is the following, for integer
variables and positive constant `N`:

$$
m=xy,\qquad u=x(y+N),\qquad u=Nq+r,\qquad
0\le m,r<N,\qquad m\ne r.
$$

Distributivity gives `u = m + N*x`, hence

$$
m-r=N(q-x).
$$

The bounds put `m-r` strictly between `-N` and `N`. Its only possible multiple
of `N` is zero, contradicting `m != r`. The nonlinear engine supplies the
identity; linear integer reasoning discharges the resulting bounded residue
problem. This is an explanatory derivation, not a new Primo implementation.

Replacing multiplication by an arbitrary uninterpreted function removes the
justification. For example, with `N=10`, `x=1`, `y=0`, choose
`f(1,0)=1`, `f(1,10)=2`, `q=0`, `r=2`. Then `m=f(x,y)` and
`u=f(x,y+N)=Nq+r` satisfy the bounds and `m != r`. Inferring
`u=m+N*x` would reject a valid UF model. Such an identity needs genuine
multiplication semantics or explicit input axioms.

This distinction matters in the selected files: `44788…_35` contains no
builtin `*`, `div` or `mod` occurrences at all. Its arithmetic and UF structure
cannot acquire multiplication axioms merely because it came from a smart
contract verification tool.

## Which paper mechanisms transfer to Primo

| Paper section | Primo status and investigation boundary |
| --- | --- |
| §3.2 cheap equality discovery | The {ufliamodelunitsource}`rational unit-row query <src/theory/arithmetic/lra_tableau.cpp#L1265-L1460>` is selectable through {ufliamodelunitsource}`LiaComponentAdapter <src/smt/solver_engines.cpp#L9797-L10150>` before integer completion in QF_UFLIA. The [qualified selection replay](uflia-smtcomp2026-20261111.md) measures its effect. The paper does not isolate a Certora-specific gain for this feature. |
| §3.3 linear bound propagation | Same-variable propagation is default; forward/reverse row propagation is optional. The [implementation](../implementation/lra.md#propagate-and-explain-consequences) tests registered atoms and does not retain every newly derived numeric bound to a fixed point. |
| §4.3 bounded-variable GCD extension | Still an [open lead](../leads/lia-extended-gcd.md). The {ufliamodelunitsource}`current GCD checker <src/theory/arithmetic/lia_gcd_consistency.cpp>` substitutes fixed terms only. Its value depends on reaching an appropriate integer-infeasible relaxation. |
| §4.4 integer branching, and §2 CDCL integration | The paper creates bound literals for SAT case splitting. Primo's [integer completion](../implementation/lia.md) uses the private {ufliamodelunitsource}`branch-and-bound search <src/theory/arithmetic/lia_branch_and_bound.cpp>`. These have different scheduling and learning opportunities. |
| §4.5 cut eligibility, selection and numeral cost | Primo has {ufliamodelunitsource}`mixed cuts <src/theory/arithmetic/lia_gomory_cuts.cpp#L169-L186>`, but {ufliamodelunitsource}`find_eligible_row <src/theory/arithmetic/lia_gomory_cuts.cpp#L93-L125>` selects the first eligible integer row and requires nonbasic variables at bounds. It does not implement all the paper's relaxed eligibility, closest-integer/occurrence selection or large-numeral temporary-scope policy. |
| §§5.4–5.5 nonlinear-to-linear propagation and incremental linearization | Outside the current QF_UFLIA fragment. These cannot be applied to uninterpreted applications without additional valid premises. |
| §6 cached sharedness | The paper caches whether an E-node is shared, invalidating on parent addition or class merge. Primo already registers shared-term lists; repeated [care-pair construction](../leads/care-pair-generation.md) is a related but different cost. A cache must identify the actual changing state rather than equating the two mechanisms. |

These are implementation boundaries, not a claim that the paper exhaustively
attributes the total Certora improvement to a single technique.

## Current profiles split the Certora problem

The [four-case manifest](../../benchmarks/certora-paper-20261111/cases.json)
selects two timeouts and two slower SAT solves from the current 300-input
selection. The same frozen `44eee3c1` binary and SHA256 as the preceding
qualification are used. All native timings, GDB samples, Callgrind prefixes
and model validation run in SLURM, with preparation-helper preflight. Timing,
profiling and model replay have global phase barriers.

The [qualification record](../../benchmarks/certora-paper-20261111/qualification.json)
records commands, jobs, binary identities and resource limits. The
{download}`SLURM worker <../../scripts/qualification/certora_paper/run.py>`
and {download}`diagnostic auditor <../../scripts/qualification/certora_paper/summarize.py>`
keep the native rows, model coverage and profiler evidence separate.

The current best profile is `--qf-uflia-tableau-equalities=all` with
`--qf-uflia-lia-equality-producer=model-unit`. Each control changes only the
listed propagation option. Every cell is one observation, in seconds, with
120 seconds and 8 GiB address space for native solving. `T` means timeout
at that wall-clock limit:

| Certora input, shortened | Current best | Forward rows | Forward + reverse rows | No bound propagation | Archived cvc5 |
| --- | ---: | ---: | ---: | ---: | ---: |
| `44788…_35` | T | T | T | T | 3.100 |
| `39657…_47` | T | T | T | 13.583 | 9.499 |
| `39657…_48` | 22.821 | 46.756 | 25.152 | 9.416 | 1.108 |
| `72771…_64` | 57.191 | T | T | T | 9.556 |

All six successful SAT timing rows pass separate direct Dolmen validation.
The controls are diagnostic: no new merge-grade fuzz gate or default promotion
is claimed for these option combinations. Turning propagation off helps two
selected traces and loses another; stronger row propagation is not a general
remedy on these cases. No reference solver corpus timing was rerun.

The current-best GDB profiles give a more useful split:

| Case | Samples | Stacks containing integer completion | Private branching | Rational check | Care pairs |
| --- | ---: | ---: | ---: | ---: | ---: |
| `44788…_35` | 223 | 0 | 0 | 83 | 1 |
| `39657…_47` | 220 | 176 | 164 | 182 | 3 |
| `39657…_48` | 147 | 1 | 0 | 72 | 8 |
| `72771…_64` | 226 | 0 | 0 | 130 | 4 |

Categories overlap: integer search itself calls the rational checker. These
are inclusive wall-clock stack observations with GDB perturbation, not CPU
percentages. The 35-second prefixes are not interchangeable with the complete
native timings. `39657…_48` exits normally during sampling; the other three
reach the intended profile deadline, which is not a solver crash.

Callgrind reinforces rational work on `_48`: its instrumented prefix assigns
7.14% of self instructions to `LraTableau::pivot`, 6.65% to rational
multiplication and 4.53% to violation-status maintenance. On `_35`, the
instrumented prefix instead strongly emphasizes allocation and preprocessing
substitution, while native samples have reached SAT/rational search. That
prefix does not establish that preprocessing dominates the full native solve.
The [diagnostic audit](../../benchmarks/certora-paper-20261111/diagnostics.json)
retains the instruction totals, top functions, category definitions' counts
and every native/model artifact identity.

## Concrete next experiments

For `_47`, first inspect integer-completion scheduling and the paper's
SAT-visible branching contract: a private search occupies 164 of 220 sampled
stacks. A bounded private-work experiment followed by a valid SAT bound split
could test whether the solver commits too much work to one component candidate.
This needs explicit reasons, backtracking and progress rules; lowering a limit
alone is not an implementation of SAT-visible integer branching.

For `_35`, `_48` and `_64`, prioritize rational/SAT work before adding another
integer cut stage. Measure violation-set membership transitions and repeated
row updates, and preserve exact pivot/candidate order in a bookkeeping
experiment. The paper's large-numeral cost caution is relevant to the observed
258–512-bit input numerals, but the coarse propagation controls do not isolate
that cost. The existing [arithmetic efficiency leads](../leads/arithmetic-efficiency-followups.md)
and [care-pair lead](../leads/care-pair-generation.md) give the required cache
invalidation boundaries. Care-pair construction is visible here, but these
profiles do not make it the dominant cost on every Certora case.

Implementing a nonlinear engine solely to reproduce the paper's headline
Certora gain would target a different input theory. The next experiment should
follow the measured bottleneck of the chosen encoding.
