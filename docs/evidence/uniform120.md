(uniform120-evaluation)=
# Uniform 120-second theory comparisons

**Status: preparation and fresh correctness gates. No new full-corpus timing or
model result is claimed yet.** The target is a report within 18 hours of the
9 November request. The five quantifier-free divisions contain 24,505 inputs:

| Logic | Full corpus |
| --- | ---: |
| QF_UF | 7,503 |
| QF_LRA | 1,753 |
| QF_LIA | 13,306 |
| QF_UFLRA | 1,284 |
| QF_UFLIA | 659 |

The [earlier 200-input LIA chart](arithmetic-overhead-20261109.md#current-version-cactus-plots) was a development selection, whose ranking
need not match the full division. In the earlier full 13,306-input comparison,
Primo M124 solved 9,865 and archived cvc5 solved 9,239 at 60 seconds; those are
[historical results](qf-lia-progress.md) for their stated sources and profiles. Raising the wall
limit does not itself remove easy cases or guarantee a ranking reversal.

## Frozen inputs, profiles and reused references

Only Primo is timed anew, from source `7dd8874c`, whose production source and
C++ tests match the accepted interval-cache implementation `cab1ebc4`. CITE fast
paths remain rejected. Defaults run in every division. LIA and UFLIA also run
one fixed optional integer profile: integer-bound rounding, primitive integer
rows, no integer-slack obligations, integral patch moves and uniform cubes.
Uniform cubes apply to pure QF_LIA; the switch is ignored by QF_UFLIA. Profiles
remain separate curves; no per-input portfolio or promotion of defaults occurs.

The four reference candidates are archived Z3, cvc5, Yices and OpenSMT runs from
the October 24 enforced-memory campaign. Admission and timing records are read
from that campaign. Their original 1,200-second measurements are viewed at a
120-second cutoff. No reference corpus benchmark is rerun. This reuse requires
matching input SHA256 values, unchanged parent identities, complete unique row
coverage and the original solver arguments/binary identities. The parent plan
hash is `dd01e5c49cdbb9a75b4e2c819d15cc4f15514c0f2506506d6030127946db48ca`.

The {download}`campaign owner <../../scripts/qualification/uniform120/campaign.py>`
imports read-only reference rows into derived views, retaining original status
and duration. Source CSV/provenance hashes and derived CSV hashes form a reuse
certificate. A certificate digest is bound into the new plan before timing.
Reference rows cannot enter the worker's execution task list. Historical error,
unsupported and resource-limit rows remain unsolved and are reported separately.
Different measurement dates remain a limitation even when inputs and limits match.

## Execution and correctness contract

Both archived references and new native timings use one physical CPU on the
same Xeon E5-2690 v4 generation and an enforced **8 GiB per-process address-space
limit**, with an 8 GiB SLURM request. The process limit is checked explicitly;
a memory reservation alone is insufficient. Every input receives 120 seconds.
A successful solve requires SAT/UNSAT, exit zero, no timeout and a measured
runtime within the limit. Unknown and timed-out post-answer exits are unsolved.

Builds use the preparation helper with `--build-via-slurm`; its preflight runs
immediately before each submission. Fresh generated gates check 1,000 cases
per theory/profile with direct SAT-model checks. Their Z3 calls are differential
correctness checks on generated inputs, distinct from corpus reference timings.
Source-matched real-seed mutation qualification from the accepted cache stage
is retained as prior evidence; this campaign makes no new source-behavior change.
Wrong answers, contradictory concrete answers and new native execution failures
stop timing. Explicit address-space exhaustion is a reported resource outcome.

All timing arrays must finish and pass the global audit before model replays
start. Every successful native SAT timing row then receives separate direct
Dolmen validation, with a 180-second solver replay and 60-second validator
budget. Missing, invalid or incomplete coverage blocks qualification. Model
checking time never enters runtime curves. Worker journals retain completed
rows during a task; private submission state records owned arrays for cancellation.

## Uniform presentation

The {download}`report owner <../../scripts/qualification/uniform120/report.py>`
uses fixed solver colours and ordering across all five divisions. Blue is
Primo defaults; purple is the declared optional integer profile. Every panel
has the same logarithmic time axis, 0.01–120 seconds, and a 0–100% solved axis;
legends retain absolute counts. Higher curves mean more inputs solved within
the per-input time budget, rather than cumulative elapsed time.

Each division has two views. The full view retains every input. The nontrivial
view removes only inputs every plotted profile solves in under one second.
Its cohort and denominator are explicitly reported, and its counts/PAR2 are
kept separate from full-corpus metrics. The filter is computed after measurement
and never changes the scored full corpus. Quantified UF is outside this report.

Exact options and phase budgets are in the
{download}`configuration <../../scripts/qualification/uniform120/config.json>`;
{download}`worker batch <../../scripts/qualification/uniform120/run.sbatch>` and
{download}`audit batch <../../scripts/qualification/uniform120/report.sbatch>`
run in SLURM. SSH routes and corpus roots remain private. The run-log entry
`2026-11-09-uniform120` records preparation failures, jobs, throughput estimates,
phase outcomes and acceptance status as the campaign progresses.
