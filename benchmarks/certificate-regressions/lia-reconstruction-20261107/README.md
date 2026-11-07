# Primo LIA certificate reconstruction limits: expanded sample

SLURM array 363005; native a512315901c56ba95c555935a0bd3a7f092c9c00.
All four original inputs freshly returned UNSAT in Yices 2.7.0. Primo's main
solver also found UNSAT and CORE completed, retaining exactly one theory
conflict each. Bounded integer-witness reconstruction then returned a resource
limit; no native certificate was written and Micromath was never invoked.
The command therefore prints unknown rather than delivering a certificate.
These are producer limitations, not checker failures or refutation failures.

Current LiaCertificateLimits defaults are 8,192 steps, 4,096 active rows, 65,536 pairs
and 1,024 digits per integer. The failed guard is not distinguished in the
current diagnostics. See case.json/export.err for actual counters; do not
infer that the 8,192 step limit was reached from the generic error message.

Each directory contains unchanged original input, fresh Yices/native logs and
case metadata. dillig/25-22.smt2 was already a known producer limitation; the
other three are newly exposed by this sample of 128 LIA inputs. There is no claim of
delta minimization or a producer fix in this packet.

On an allocated SLURM compute node, set PRIMO to the absolute qualified binary
and CASE to an absolute extracted case directory. Use a fresh output directory:

    mkdir /path/to/fresh-run
    cd /path/to/fresh-run
    timeout --kill-after=2s 30s prlimit --as=3221225472 --fsize=268435456 -- \
      "$PRIMO" --quiet --stats --export-cnf "$PWD/native.sexp" \
      --export-cnf-reduction=core "$CASE/input.smt2" > export.out 2> export.err

Expected evidence includes `:export.main-unsat 1`, `:export.reduction.completed 1`,
`:export.theory.conflicts 1` and the LIA cut reconstruction limit diagnostic.
The snippet is the measured native command with fresh output paths; it was
syntax checked, not rerun as an extra experiment during packaging.
