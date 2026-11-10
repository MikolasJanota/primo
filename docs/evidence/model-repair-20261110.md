# Repairing stale finite-domain clauses during model reconstruction

The strong-preprocessing model failure found in the
[QF_UFLIA gap investigation](uflia-gaps-20261110.md) comes from delayed clauses
using an eliminated function argument. It is a model-reconstruction defect,
not a memory-limit termination. The final model guard detected the problem;
no invalid model was accepted by Dolmen.

## Cause and repair

Profile analysis generated finite-domain at-most-one (AMO) clauses before
solve-eqs and value propagation. The later emission tactic reused those clauses
even after the assertions changed. That could bring an eliminated constant
back into an auxiliary function application. The reduced model assigned it a
temporary value; reverse substitution restored its original value and changed
the application’s lookup key, invalidating the auxiliary equality.

The newly written [ten-line regression](../../benchmarks/regressions/qf_uflia_finite_domain_eliminated_argument.smt2)
uses `a = 7`, `f(a) = 0`, and `f(a) = 0 or f(a) = 1`. The original `7dd8874c`
binary exits 2 with the same model-assembly diagnostic on this input. On the
original Certora seed, instrumentation identifies the failed equality as
`$primo_no_proxy_540 = x130(x159, x285)`. Restoring `x159` from its temporary
value to `1084754512` changes the function result while leaving the proxy value
at `3547226262`. The [forensic record](../../benchmarks/model-repair-20261110/forensics.json)
preserves input, binary, trace and diagnostic identities.

`FiniteDomainPreprocessInput` now owns the ordered source assertion IDs for
both its raw AMO and optional symmetry result. The delayed tactic reuses these
results only when the executing goal has exactly those roots. Otherwise it
regenerates from the current assertions. Every nonterminal alternative checks
its own roots. The caller must use the same live term manager; ID comparison
cannot check manager identity. The [implementation description](../implementation/preprocessing.md#representation)
explains ownership, lifetime, counters and the worked example.

The model guards and reverse conversion order remain unchanged. This is an
original cache-validity repair using existing generators, not a new AMO or
symmetry algorithm. An assertion change can remove an earlier finite-domain
opportunity, so emitted-clause counts can decrease. Initial profile-analysis
counters still describe the original goal. Existing regressions retain their
SAT/UNSAT checks and now check the regenerated-clause counts.

## Qualification

The tested source snapshot is `5d7d4fe0`, with Release binary SHA256
`e754c446c81caeb6fb277f41dc843071bea719e6625571bceafb93a4618248af`.
The [qualification record](../../benchmarks/model-repair-20261110/qualification.json)
and run ledger contain exact commands, jobs, limits, versions and artifact
identities. The correctness gates and static review are complete; resource
timeouts remain explicitly classified below.

- `make check`: all 47 C++ groups and 329 Python tests pass.
- `make lint` and the documentation build/local Git-link checks pass. Static
  analysis covers all 135 C++ translation units. Its reviewed advisory output
  adds no production-code diagnostic; three new string-construction advisories
  concern the bounded model-test fixture. The
  [static review](../../benchmarks/model-repair-20261110/static-review.json)
  preserves warning counts and baseline comparisons.
- ASan: all 47 C++ groups pass, plus 150 QF_UFLIA comparisons across six
  existing profiles (25 generated cases repeated across profiles).
- Both the original Certora input and minimal regression pass separate direct
  Dolmen model validation, with `--preprocessing-mode=strong --check-model`.
- Fresh generation covers 1,000 attempts for each of five logics under both
  strong and default (`auto`) preprocessing: 10,000 primary attempts total.

| Logic | Strong matching answers | Default matching answers | Successful SAT models checked, strong / default |
| --- | ---: | ---: | ---: |
| QF_UF | 1,000 | 1,000 | 647 / 632 |
| QF_UFLIA | 1,000 | 1,000 | 515 / 516 |
| QF_UFLRA | 1,000 | 1,000 | 617 / 618 |
| QF_LRA | 1,000 | 1,000 | 1,000 / 1,000 |
| QF_LIA | 975 | 986 | 869 / 875 |

The remaining 39 primary QF_LIA attempts time out at five seconds: 25 strong,
14 default. There are no wrong answers, crashes, parser/type errors, unsupported
inputs or reference disagreements. All **7,289 successful primary SAT rows**
pass direct Dolmen checks. Separate model production, with a ten-second budget,
also solves six initially timed-out SAT cases and times out on 33 others.
These resource outcomes remain separate from passing comparisons. The
[generated-case audit](../../benchmarks/model-repair-20261110/fuzz-audit.json)
joins every selected SAT row to a direct validator pass.

Real-seed mutation uses four fast SAT seeds from TwoSquares, MathSAT/Hash and
Wisas; the original large Certora failure is checked separately. All four seed
models pass Dolmen. YinYang retains 160 operator mutants: 159 have matching
concrete answers (147 SAT, 12 UNSAT), and one Wisas mutant times out even at
60 seconds. All 147 successful SAT models pass direct Dolmen validation.
The timeout remains incomplete, rather than becoming a correctness pass.
The initial ten-second answer check exited nonzero on that timeout, so the
retained mutants were replayed in a separate job; no inputs were discarded.
Generation used the mutation environment’s Z3 5.0.0 oracle; every retained
mutant was then explicitly compared with Z3 5.1.0. The qualification record
preserves both identities and the unresolved resource row.

New unit coverage exercises unchanged cache hits, refresh after root changes,
independent alternatives, absence of eliminated constants, and symmetry.
Model tests cover both integer and real UF, forced function values zero and
one, and the optional post-AMO scan enabled and disabled.

These are correctness checks. Model-production durations are not benchmark
speedups. The old strong-preprocessing timing row remains rejected; a fresh
passing model from a repaired binary cannot validate the historical binary.
The frozen cactus comparisons and their source identities remain historical.
This repair makes no default-promotion or overall cvc5-gap claim.

## Reproduction boundaries

All solver builds and runs use SLURM, with the preparation helper and explicit
compiler-header provisioning. Reference runs here are fresh correctness
comparisons for generated and mutated inputs; archived corpus timing rows are
not rerun. Model validation is a separate direct Dolmen stage, with no Z3
fallback counted as acceptance. Raw inputs, outputs, models and logs remain
under `results/model-repair-20261110` and in the private SLURM archive.
