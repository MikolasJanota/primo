# Rejected LIA reconstruction candidates, 6 November 2026

These are reproducibility artifacts, not active solver changes. Both candidates
passed their focused certificate unit test but left the real
`QF_LIA/dillig/25-22.smt2` export inconclusive at the same reconstruction limit.
The accepted native producer remains `a5123159`.

- `opposing-bounds.patch`: apply to workflow baseline `bf7638de`. Candidate
  `b6fa115c` adds an ordered-map opposing-bound check and a regression whose
  projection pair budget is exhausted before another elimination.
- `linear-rechecks.patch`: apply after the first patch. Candidate `e90bf22e`
  retries the existing linear witness reconstruction only after new integer
  floor strengthening; its regression requires that second linear check.

The algorithmic basis, state, invariants and limitations are in the
[design note](../../leads/lia-conflict-certificates.md#opposing-bound-reconstruction-follow-up).
The [ledger](../../coverage/lia-reconstruction-followup.json) records exact
patch/source/binary hashes, jobs and outcomes. The original small target source
is also in `benchmarks/checker-regressions/arithmetic-stress-20261106/arithmetic-stress.tar.gz`,
under `lia-reconstruction-limit/input.smt2`.

To reproduce, create an isolated checkout at `bf7638de`, apply the patches in
order, and commit the experimental tree so the canonical workspace helper
can freeze its revision. Build all targets using
`scripts/slurm_prepare_benchmark_workspace.py --build-via-slurm --reuse-dependencies --extra-target all`
with the available cluster/dependency/header configuration. Run only on an
allocated compute node:

```bash
ctest --test-dir build-slurm-rel -R '^primo_lia_certificate_tests$' \
  --timeout 120 --output-on-failure
build-slurm-rel/primo --quiet --stats --export-cnf candidate.sexp \
  --export-cnf-reduction=core /path/to/lia-reconstruction-limit/input.smt2
```

Expected: the focused unit test passes; the real export prints `unknown`, reports
5,224 generated cuts and 5,186 projection pairs, and creates no certificate.
Candidate one reports 5,513 opposing lookups and zero opposing conflicts.
Candidate two also reports only one linear check on the real target; the retry
mechanism does trigger in its synthetic regression. No cap was raised.

Both implementations and their active unit-test additions were restored to the
baseline on the working branch. Their isolated builds were not promoted and
were not run through full native, ASan, static-analysis or fuzz gates. Exit zero
from the diagnostic probe means its commands completed; it is not successful
certificate export. These results do not establish a general performance
comparison or explain all possible integer reconstruction failures.
