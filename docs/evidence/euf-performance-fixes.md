# EUF mechanical fixes and demand-atom evaluation

Status: implementation and qualification in progress. Native source `abb4d19c`
passes 49 native test groups, 382 Python tests, and 49 ASan/leak test groups.
The mutation gate found three model replay errors in demand mode;
qualification is blocked and the paired timing campaign has not been submitted.
No new speed conclusion or default promotion is recorded yet.

The [implementation checklist](../leads/euf-performance-fixes.md) and
[EUF representation](../implementation/euf.md#conflict-selected-ackermann-prototype)
map the changes to their owners and limitations. Dutertre and de Moura's
conflict-selected congruence clause {cite:p}`dutertre2006yices` is the algorithmic
basis; the caps, queue lifetimes, negative cache and safe-boundary scheduling
are Primo engineering choices. The [earlier pilot](conflict-ackermann.md)
measured the older existing-atom policy, not the revised implementation.

## Frozen comparison

| Label | Native source | Configuration and purpose |
| --- | --- | --- |
| BASE | `13230b6d` | Frozen pre-change production binary; source behavior agrees with pre-change HEAD `690ddc7f`. Default options, with statistics. |
| MECH | `66ebec78` | First mechanical/typed-statistics milestone; default options. |
| OFF | `abb4d19c` | Integrated implementation; conflict Ackermann remains disabled. |
| OLDON | `13230b6d` | Frozen original conflict-Ackermann emission policy. |
| ON | `abb4d19c` | Revised selection, using existing equality atoms. |
| DEMAND | `abb4d19c` | Revised emission plus demand atoms, default fresh-equality limit 4096. |

All timing commands use `--quiet --stats`. ON and OLDON add
`--euf-conflict-ackermann`; DEMAND also adds
`--euf-conflict-ackermann-atoms=demand`. Complete native hashes, ordered arguments
and input hashes belong to the run cards and immutable receipts. The
{download}`card generator <../../scripts/qualification/euf_performance/configure.py>`
and {download}`auditor <../../scripts/qualification/euf_performance/audit.py>`
are reproducibility tools, not solver implementations.

The frozen 80-case pilot has 40 QF_UF and 40 QF_UFLIA cases, enriched for the
previous exploration bottleneck. It runs all six profiles, in three rotated and
reversed repetitions: 1440 native rows. The broader control set contains the
existing 697 arithmetic inputs plus the 40 UF pilot inputs: 200 QF_LIA,
300 QF_UFLIA, 100 QF_LRA, 97 QF_UFLRA and 40 QF_UF. BASE/OFF are paired three
times on these 737 cases: 4422 rows. The three previously quarantined arithmetic
inputs remain excluded, as recorded in the frozen parent selection. UF controls
are the enriched pilot subset; this is not a uniform full-corpus ranking.

Each measurement has a 30-second wall limit, an 8 GiB native address-space cap,
and a separate one-CPU SLURM allocation. Paired profiles run sequentially within
that allocation. Timeouts, resource returns and unsupported results count as
unsolved in PAR2, even if an answer was printed during termination. RSS is the
fresh native child's high-water mark. Solved counts, paired runtimes, PAR2,
RSS and mechanism counters are reported by logic, with stable gains/losses and
repeated regressions separated from short-run noise.

Every successful native SAT timing row must pass a separately scheduled replay
with original assertions, model production, `--check-model` and direct Dolmen
validation. Replay time does not enter the speed rows. Repetitions and option
profiles remain distinct validation targets. A timing shard without complete
model coverage remains provisional.

## Correctness and mechanism stages

The first milestone's {download}`audited gate <../../benchmarks/euf-performance-20261118/mechanical-qualification.json>`
records 9000 generated and 2855 mutated inputs, with 20 timeouts and 12 incomplete
mutations, respectively; 8617 SAT models passed separate Dolmen checks. Those
counts qualify `66ebec78` only.

The integrated gate uses 21 exact logic/option profiles. It includes all five
ground-logic defaults; existing-atom OFF/diagnostic/emitting variants; demand,
diagnostic-only demand and a one-atom limit in UF/UFLIA; both supported alternate
explanation searches; and mixed demand with the model-unit equality producer or
immediate hybrid integer handoff. Each generated profile gets 1000 fresh cases against Z3: FuzzSMT for UF/LIA,
`qf_uflia_fuzz.py` for UFLIA, `qf_lra_boolean_arithmetic_fuzz.py` for LRA, and
`qf_uflra_smoke_fuzz.py` for UFLRA. Real-seed mutations, tracked regressions, scoped
sessions and all successful SAT models are checked separately. The native and
ASan gate is {download}`a pinned SLURM driver <../../scripts/qualification/euf_performance/verify.sbatch>`;
its ASan revision is explicitly set because the remote source archive has no
Git checkout.

A focused nested mixed probe on preliminary source `d1f71fb6` selected two
congruence instances, created one equality atom used by both instances, and resumed SAT
once. A direct variant already had the argument atom and used existing-atom
clauses. These are mechanism diagnostics, not timing evidence or qualification
of later source. The matching native regression requires demand growth for a
non-interface equality, while leaving the arithmetic owner with its one input
atom and no arrangement pairs.

The local GCC ABI size diagnostic found `Statistics` objects of 2424 bytes
before the migration, 7176 bytes at the mechanical milestone, and 7848 bytes
with the integrated registry. This was a compile-to-assembly size check, not a
runtime measurement. Fixed-array growth and removed string/map work must be
assessed together with measured RSS and runtimes. Closure checkpoints use their
separate compact statistics structure.

The preliminary full-tree {download}`static inventory <../../benchmarks/euf-performance-20261118/static-preliminary.json>`
completed both scans over 141 C++ translation units, with no missing/changed
inputs or tool errors and no blocking unused includes. It records advisory
findings and their inspected scope; it is not a claim that the whole tree is
warning-free. The final integrated scan also completed 141/141 units with zero tool errors;
its advisory disposition and the evaluation remain pending.

## Integrated correctness blocker

Source `abb4d19c` passed 21 regression and 21 generated-input tasks, but the
real-seed mutation array passed only 20 of 21 tasks. The model-unit mixed profile
had 241 SAT replays: 238 passed Dolmen and three exited with
`model: inconsistent final component equalities`. The original SAT answers
agreed with Z3. Isolation job 414923 reproduces all three errors with demand
and the default or legacy equality producer too. One case validates with both
the frozen baseline and current existing-atom mode; the other two time out
without demand at 30 seconds. The root cause is unresolved; this is an integration blocker, not accepted
performance evidence. Raw task-19 artifacts and the failing inputs are retained;
timing cards are prepared but unsubmitted while diagnosis continues.
