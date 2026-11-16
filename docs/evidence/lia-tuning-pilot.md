# Hybrid branching parameter pilot

This is a bounded development study of existing integer-branching controls,
with correctness qualification before timings. It follows the
[arithmetic combination study](arithmetic-combinations.md). It is not global
tuning, a representative ranking, a default promotion, or an implemented
profile selector. Preprocessing is off throughout. The row-on context enables
both forward and reverse row propagation; same-variable propagation remains
enabled in both contexts.

The {download}`protocol <../../benchmarks/lia-tuning-20261116/protocol.json>`
and {download}`selection <../../benchmarks/lia-tuning-20261116/selection.json>`
are frozen before new runs. Forty inputs, twenty per integer logic, form a
target-enriched diagnostic pilot. Required cases include prior hybrid gains,
the two lost LIA refutations, and UF/row interactions; remaining slots use a
deterministic family round-robin. Three known long cases with no recorded
private-node or split activity are excluded from this timing pilot only.
The tracked correctness regression corpus remains intact. Selection from
previous outcomes makes this unsuitable for claims of global optimality.

| Policy | Private factor | Private node cap |
| --- | ---: | ---: |
| PVT | private mode | 100,000 |
| H0 | 0 | 100,000 |
| H1k / H1 | 1 | 1,024 / 100,000 |
| H16k / H16 | 16 | 1,024 / 100,000 |
| H4096k / H4096 | 4,096 | 1,024 / 100,000 |

The factor-zero duplicate at the smaller cap is omitted because its private
budget is zero under either cap. The two lost LIA refutations previously used
19,806 and 70,539 private nodes. The large factor therefore brackets a regime
that a factor-one neighbourhood alone would miss. It does not reproduce
private mode exactly: hybrid uses its history-based selection and can still
hand off after the cap.

The existing budget is `min(node cap, factor × original Int term count)` per
completion attempt. Earlier patch/GCD/cube/cut stages still run with factor
zero. Eight policies × two row contexts × forty inputs × three repetitions
produce **1,920 primary native measurements**, at **30 seconds**, with an
8 GiB address-space cap. Orders rotate and reverse by case/repetition; this
is a pilot, not a statistical significance test. A later full-budget and
independent global evaluation is required before selecting defaults.

## Correctness and execution

The immutable binary is source `4f02a8b7`, SHA256
`08948789d787e2a81e76ad671738f29ac192dde34b4c9276789dff55b03a44ff`.
It was built through the preparation helper with `--build-via-slurm` and
passed 49 native groups and 357 Python tests. Production C++ is unchanged;
this study adds only local metadata/configuration/analysis tools. The
existing frozen qualification and timing/model runners execute on SLURM;
only private configuration/input metadata is staged. The preparation helper
checks this binary immediately before each new submission.

Every one of the 32 logic/context/policy tasks receives 1,000 fresh generated
cases against Z3, the exact-option tracked regression sessions, and real-seed
mutations. Original regression sessions must complete and agree before the
mutation stage starts. All native SAT gate results, including mutation
originals and reference timeouts, require direct Dolmen acceptance.
Timeouts and diagnosed resource unknowns remain separate from agreement
passes. Wrong answers, crashes, invalid models or unexplained disagreements
stop the study.

Separate resource probes use published successful multi-split cases and atom
caps 0, 1, 8 and 64. Zero means unlimited fresh generated atoms per solve,
not zero split requests. These are correctness probes, not tuning scores. They
check concrete answers when completion succeeds, exact resource diagnostics
when capped, and no acceptance of an answer followed by nonzero exit. All
successful probe SAT results are independently checked. No logical UNSAT may
be inferred from atom-budget exhaustion. Their original, uncapped input and
profile identities are retained for comparison.

Only audited gates permit primary timings. Three timing arrays run in
sequence, followed by model arrays for **every** successful SAT timing row.
Model replays add `--check-model`, use 180 seconds native/60 seconds Dolmen,
and never enter speed scores. PAR2 charges each unsolved primary row 60
seconds. Report per-logic/context solved counts, paired runtimes, gains and
losses against H1/PVT, memory, private nodes/conflicts/budget exits, split
requests/atoms/resumes and overlapping phase timers. Incomplete statistics
are not zero-filled. Ranking is descriptive for this selected pilot only.

## Status and sources

The local {liatuningtools}`configuration generator <scripts/qualification/lia_tuning/prepare.py>`
freezes recipes and selection; the {liatuningtools}`96-receipt auditor <scripts/qualification/lia_tuning/audit.py>`
checks exact options, input/configuration hashes and all-SAT coverage. The
{liatuningtools}`submission controller <scripts/qualification/lia_tuning/submit.py>`
preflights and holds arrays until their whole-array dependencies are verified.
The finite-cap configuration/audit tool is
{liatuningtools}`finite-cap and resource-replay auditor <scripts/qualification/lia_tuning/probes.py>`. All 32 native probes and
10 SAT model replays passed. The
{download}`qualification inventory <../../benchmarks/lia-tuning-20261116/qualification.json>`
accepts all 96 exact-option receipts: 32,000 generated inputs (31,810 agreement
passes, 188 timeouts and two reference timeouts), 256 tracked regression
measurements, 384 original real seeds (all agreement passes), and 11,520
mutants (11,230 agreement passes; 289 native timeouts and one diagnosed
private-node-limit unknown). All **29,767 successful native SAT gate results**
passed Dolmen. Resource-incomplete cases do not become agreement passes.
The {download}`unknown replay <../../benchmarks/lia-tuning-20261116/resource-unknowns.json>`
confirms the exact private-node diagnostic and positive budget-exit counter;
the {download}`finite-cap inventory <../../benchmarks/lia-tuning-20261116/resource-probes.json>`
contains 10 SAT, eight UNSAT and 14 diagnosed atom-limit unknowns.
Three sequential primary timing arrays are running or queued, with dependent
all-SAT model arrays queued. No primary timing result is qualified yet; defaults
remain unchanged.

The native budget and history belong to
{arithmeticcombsource}`lia_branch_and_bound.cpp <src/theory/arithmetic/lia_branch_and_bound.cpp>`;
{arithmeticcombsource}`CLI registration <src/main.cpp>` defines the controls.
The frozen {arithmeticcombsource}`qualification runner <scripts/qualification/arithmetic_combinations/qualify.py>`,
{arithmeticcombsource}`cohort router <scripts/qualification/arithmetic_combinations/run.py>`
and {arithmeticcombsource}`native timing/model runner <scripts/qualification/lia_hybrid/run.py>`
execute the private JSON metadata. The older fixed-72-receipt auditor is not
used for this pilot.

The existing policy implements the ordinary-coordinate subset of Griggio §5,
with SAT scheduling motivated by splitting on demand; see the
[implementation checklist](../leads/lia-hybrid-branching.md). Proof-derived
combination splits and a new termination claim are outside this study.
Parameter choices here are engineering experiments based on prior diagnostic
counts, not paper-prescribed optimum values. Configuration/analysis metadata
has no ownership of C++ terms, tableau rows, SAT literals or solving state;
it references content-pinned inputs and the existing driver protocol.
