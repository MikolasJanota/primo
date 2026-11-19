# Hybrid fallback resource-accounting qualification

`qualification.json` records the measured source/native identities, exact
profiles and seeds, model counts, tool reports, receipts, job IDs and outcomes.
`configs/` preserves the four exact qualification cards used by the existing
runner. Absolute corpus/output paths describe the prepared remote workspace;
connection configuration remains outside the repository.

Build source `2d6daadc` using `scripts/slurm_prepare_benchmark_workspace.py
--build-via-slurm`. Submit `scripts/qualification/conflict_ackermann/qualify.sbatch`
for the six task slots, with `PRIMO_ACK_CAMPAIGN` naming the campaign card,
`PRIMO_ACK_PHASE` set to `seeds`, `generated` or `mutations`, and
`PRIMO_ACK_JOB_FILE` naming that batch script. Follow the documented held-job
submission/preflight procedure. Stage dependencies and environment as described
in the [execution procedure](../../skills/benchmarking/references/execution.md);
never execute these batches on the login host or workstation.
For a different workspace, update paths and retain the resulting new card hashes.

The campaign was derived from an earlier card. Only qualification fields are
used: source/native, tasks/profile flags, corpus roots, manifests, seeds and
output paths. Its inherited timing comparator, selection, repetition, timeout,
address-space and timing-note fields were not executed or relied upon here.
The actual limits are recorded separately in `qualification.json` and runner
commands. This is correctness qualification, not a timing comparison.

Audit collected metadata with:

```bash
python3 scripts/qualification/conflict_ackermann/audit.py gates \
  --configs <cards> --rows <collected>/qualification --out <audit.json>
```

The initial audit lists one unknown replay candidate. The separate replay
`434150` diagnoses the private integer branch-node limit; it is incomplete, not
a correctness pass. All other mutation incompletes are native timeouts.
Successful SAT models are checked separately with Dolmen. Focused bounded-input
replays use `scripts/qualification/euf_smtcomp/triage.sbatch`; the fixture, flags,
counters and outcomes are retained in the qualification record. Supplemental
include/help checks use `scripts/qualification/euf_smtcomp/test_hygiene.sbatch`
against the unchanged qualified libraries and preserve the application hash.
