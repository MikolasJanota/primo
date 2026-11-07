# Isolating QF_UFLIA equality delivery costs

**Design and implementation checklist for an experimental investigation.**
The baseline is `5cde87e2`, whose solver matches qualified `8d84fdca`.
The [larger comparison](../evidence/qf-uflia-equality-sharing.md) shows that
native sharing-off remains the best of the measured Primo profiles. This
experiment separates discovery, interface naming and proof-clause delivery.
It does not implement a new equality-discovery algorithm or promote a default.

## Basis and scope

[Bruttomesso et al., *Delayed Theory Combination vs. Nelson–Oppen*](https://es-static.fbk.eu/people/griggio/papers/lpar06_dtc.pdf),
LPAR 2006, §3 and Figure 4, treats interface equalities as SAT-visible atoms
and discusses combination with partial or absent equality deduction. This
supports keeping naming distinct from deduction. Primo's existing model-guided
search and complete fallback remain responsible for combination; this is not
an implementation of that paper's optimized NO-emulation strategy or bound.
The four existing tableau rules and their original paper mapping remain in
{ref}`the combination chapter <lra-default-equality-rules>`.
The following policy filters and their tests are an original design.

## Representation and operations

Keep one CLI switch, `--qf-uflia-tableau-equalities`, accepting an optional
explicit mode. Absence remains off; the existing bare flag remains all.
The C++ Boolean enable field remains compatible. A separate owned enum in
{ufliaablationsource}`CheckSatOptions <src/smt/check_sat.hpp>` selects one of four
enabled policies; no second CLI flag is
introduced. {ufliaablationsource}`LiaComponentAdapter <src/smt/solver_engines.cpp>` copies that
policy for a query's lifetime. The source links pin the ablation implementation at `4a0c6bee`;
qualification remains a separate stage, tracked in the {download}`run ledger <../run-log.json>`.

| Mode | Discovery | New equality names/bridges | Guarded proof clauses |
| --- | --- | --- | --- |
| off | None | Existing model search only | None from this producer |
| scan | All four existing rules | None from this producer | None |
| names | All four existing rules | Only previously unregistered pairs | None |
| fixed | All four scanned; retain only `FixedEq` output | As required | Retained proofs |
| all | Existing behavior | As required | All emitted proofs |

The existing {ufliaablationsource}`adapter scan <src/smt/solver_engines.cpp>` is the
starting point. `scan` performs its owning proof translation and validation but returns
no batch. The tableau query is const and must not change assignments, bounds,
basis, SAT atoms, assumptions or clauses. Non-time search counters should agree
with off, apart from new diagnostic counters.

The existing {ufliaablationsource}`driver registration and complete enumeration
<src/smt/component_solver.cpp>` remain the semantic boundary. `names` retains
full bridge semantics: the equality atom denotes exactly the
conjunction of its two arithmetic comparisons. The generic driver receives a
recipe delivery policy that suppresses the optional proof clause; it does not
inspect a theory name or a QF_UFLIA option. Existing SAT/theory consistency
checks and model reconciliation remain active. No unsound bridge-free shortcut
is used.

The producer filters every already-registered pair in names mode, regardless
of its current truth value. Each delivered names batch must add at least one
new registered pair; the driver rejects an existing pair in that policy.
Registration is monotone within the query, with at most n(n−1)/2 same-sort
pairs for n shared terms. This prevents empty-effect naming loops. Complete
fallback already names every pair, so this producer has nothing left to add.

`fixed` drops only optional consequences. It does not drop input constraints
or change the integer-completion pipeline. All enabled modes scan only the rationally
feasible tableau before private integer cuts and branch bounds exist.

## Invariants and proof obligations

- Added names are a conservative definitional extension: every model of the
  original formula extends to their equality/comparison truth values. Existing
  component checks enforce both polarities. No new assumptions are added.
- Every learned clause in fixed/all retains true SAT antecedents with the
  existing explanation convention. Names mode learns no such clause and must
  not count one as a theory conflict.
- Scan/fixed omission preserves the existing complete fallback and cannot
  turn a model coincidence into an entailed fact.
- Copy term IDs and model-dependent truth classifications before atom growth;
  no term-storage borrow or old SAT-model query crosses materialization.
- Counters distinguish discovered candidates, suppressed scan-only proofs,
  filtered rules, skipped registered names, actual clauses and naming-only
  registrations. A missing/partial timeout counter is not an observed zero.

The policies retain existing scan complexity. Naming costs owning storage and
bridge preparation proportional to fresh pairs and their encodings. No new
asymptotic improvement is claimed. Trajectories after the first delivery may
differ, so names vs all is a sufficiency test for naming-induced regression,
not a matched-state or instruction-for-instruction causal decomposition.

## Verification and evaluation checklist

- Preserve bare-flag parsing before a positional input, explicit all/off, and
  reject invalid modes. Refresh captured CLI help from the qualified build.
- Test scan/off search-counter parity, zero naming/clauses from scan, names
  progress with zero optional clauses, complete-arrangement names exhaustion,
  and fixed rejection of a row-only proof while retaining fixed-value proofs.
- Cover online/rebuild paths, SAT model checking and original contradictory
  EUF examples. Keep all existing four-rule regressions in all mode.
- Build in an isolated SLURM workspace; run unit/check/ASan, whole-tree static
  checks and fresh merge-grade reference fuzzing for each new policy. Validate
  generated SAT models separately and record all counts in the run ledger.
- First compare on/off/scan/names/fixed at identical limits on recorded native
  gains/losses and controls, including the four previously profiled inputs.
  Record downstream pivot/atom/decision counters. Aggregate counters do not establish identical delivered batches.
- Then evaluate the informative policies on all 659 available inputs against
  the frozen best-off baseline and unchanged all policy. Replay every
  successful SAT row with direct Dolmen outside timing. No default promotion
  follows from a selected sample or a winning solved count alone.

The CLI accepts policy values with `=MODE`; the bare flag maps to `all` and never consumes the following input filename. Qualification caught an optional-value parser consuming that positional filename; the zero-positional-value flag parser avoids this ambiguity.


## Reproducible experiment methods

The {ufliaablationmethods}`selected manifest <benchmarks/qf-uflia-equality-ablation-selected-20261107.txt>` contains 61 inputs: every native solved gain/loss from
qualified `8d84fdca`, the ten largest enabled/off runtime ratios among common
solves, two fastest common-solved controls per family, and all four previously
profiled cases. Deduplication can reduce overlap; {ufliaablationmethods}`selection metadata <benchmarks/qf-uflia-equality-ablation-selected-20261107.json>` gives the
reason and input SHA for each case. This diagnostic selection is deliberately
biased toward explaining differences, and cannot establish a corpus-wide win.

{ufliaablationmethods}`select_cases.py <scripts/qualification/uflia_equality_ablation/select_cases.py>` regenerates the
manifest from the prior full analysis and timing rows. {ufliaablationmethods}`run.py <scripts/qualification/uflia_equality_ablation/run.py>` rotates serial
arms per input, pins binary versions and SHA values, retains command lines and
stdout/stderr, and flushes each completed row. It uses the native arithmetic
settings and `--arithmetic-model-phase=off`. `old-off`/`old-all` run the frozen
qualified binary; other arms run the ablation binary. {ufliaablationanalysis}`analyze.py <scripts/qualification/uflia_equality_ablation/analyze.py>` audits
completeness, answer agreement, off/scan and old/current counter controls,
per-family results and direct Dolmen coverage. Prior Z3 answers are reused only
for SHA-identical inputs. They are reference evidence from the prior run, not
fresh reference timings for this experiment.

The SLURM source snapshot stays frozen at `4a0c6bee`. Later runner scripts and
selection metadata are staged as experiment artifacts under an isolated
`tmp/ablation-methods` directory by `slurm_stage_manifest_files.py`, without
replacing solver sources or the qualified binary. The exact runner SHA is
recorded per shard. Preflight uses a clean checkout of the binary's revision;
all solver execution and correctness gates run on compute nodes.


## Completed evaluation

The [controlled full-corpus comparison](../evidence/qf-uflia-equality-ablation.md)
completes the checklist on solver source `4a0c6bee`: all 659 inputs under each
policy, separate replay of every successful SAT row, fresh per-policy reference
gates and a 3,000-case fixed-only campaign. Measured counter controls agree;
this is not a claim of identical traces. Fixed-only is a useful but mixed
experimental alternative. Default off is retained; no proof, scan or naming
mode is promoted automatically.
