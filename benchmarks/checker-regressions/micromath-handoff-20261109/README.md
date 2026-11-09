# Micromath checker feedback, 9 November 2026

This compact handoff collects four natural examples from Primo's full arithmetic CORE sweep (SLURM array `365550`). The examples cover every observed checker failure category. Each is the smallest natural case in its category by required uncompressed checker-input bytes (statement plus proof input, or statement alone for the builder). No input reduction was attempted. `failure-index.json` and `failure-index.csv` list all **109** affected cases; only the four representatives have input payloads in this archive. No solver/checker stage was rerun while preparing this handoff, and no checker implementation was inspected or modified.

| Replay name | Observed outcome in the primary run | Complete-index count |
| --- | --- | ---: |
| `wisa5` | Producer reports `Logic failure: definition index allocation` | 3 |
| `clock7` | Overall raw-proof producer/kernel pipeline reaches 120 seconds | 20 (16 LRA, 4 LIA) |
| `lia-oom` | Producer reports out of memory under 3 GiB address space | 83 |
| `challenge-p15` | Challenge builder reaches 60 seconds, before proof production | 3 |

Start with `wisa5`, the compact definition-allocation example. The `definition index allocation` diagnostic's cause is untriaged; its wording alone does not establish memory exhaustion or which resource bound fired. The following kernel `invalid solution` comes after producer failure and does not establish rejection of a complete proof. Pipeline deadlines likewise do not identify the dominant internal phase.

The source native binary is Primo `a512315901c56ba95c555935a0bd3a7f092c9c00`; Micromath is `cfd36f46e756f080295d469753c9f2989dab1fee`. Case receipts preserve the exact observed commands, input/certificate/statement hashes, primary paths and diagnostics. Primary commands containing cluster paths are provenance; the portable replay commands below use your own built-tool paths.

## Replay one case

Unpack this archive, enter its directory, and verify its contents:

```sh
sha256sum -c SHA256SUMS
bash replay.sh --list
export MICROMATH=/absolute/path/to/built/Micromath
export CADICAL=/absolute/path/to/LRAT-capable/cadical
bash replay.sh wisa5 /fresh/output-wisa5
```

Replace `wisa5` with `clock7`, `lia-oom`, or `challenge-p15`. Each output directory must be new. Use an allocated SLURM worker or another appropriately resourced machine. The scripts require Bash, GNU `timeout`, `prlimit`, and the pinned built Micromath tools; CaDiCaL is needed for the three proof-producing replays, not the builder-only replay.

Proof replays build finitary axioms and the exported-CNF challenge, then pipe `--raw` proof output directly into `check-solution`. There is no saved raw proof. Limits are 30 seconds for axioms, 60 seconds for the challenge and one overall 120-second proof/kernel deadline, with 3 GiB address-space and 256 MiB per-file bounds for each external process. Each stage is a separate budget. Both producer/kernel exits must be zero, with exact `challenge proven` output and no `Inconsistency proven!` marker. Failure or timeout returns nonzero; inspect `producer.err`, `kernel.err`, `kernel.out` and `exit-status.txt` when written. A deadline can occur before the exit-status file is written. To investigate with a different build or budget, change it explicitly and report that profile separately.

`challenge-p15` only reconstructs the challenge from the supplied statement. It needs no native certificate or CaDiCaL. Its zero exit means challenge construction succeeded; it does not prove UNSAT or invoke a proof kernel. Original/native bodies are omitted from this builder-only packet because that stage does not read them.

## Verification boundary and contents

The checked statement is `exported-cnf.smt2`. This does **not** verify Primo's original-input-to-CNF transformation. For LIA proof replays, `micromath-v2.sexp` is the compatibility copy of the native version-3 certificate: only header byte 28 changes from `3` to `2`; the body is verbatim. Original and copy hashes remain in `case.json`.

The existing reviewed packet payloads are preserved under `packets/`:

- `definition-index/`: `wisa5`, its statement/certificate, original input, logs and copy receipt.
- `proof-limits/`: seven-clock LRA pipeline timeout and the LIA native-certificate-reading OOM example. The latter has zero theory lemmas, 1,401,073 initial CNF clauses, a 100,876,052-byte certificate and a 116,007,079-byte statement.
- `challenge-deadline/`: the 38,750,749-byte `wireRouting/p15` statement and builder-only replay.

Their original READMEs and `SHA256SUMS` are retained. Top-level `SHA256SUMS` covers every other payload, including the full failure index. The compact archive does not include the remaining 105 indexed cases or any raw proofs. All 109 checker inputs would require 25,618,086,206 bytes before compression when retaining just the required statements and proof inputs.

The remaining input payloads are retained under `results/full-arithmetic-certificates-365550/<primary_case>/` in the original campaign workspace. Each index row supplies its relative `primary_case` path and statement/native hashes. Proof replays need `exported-cnf.smt2` plus `native.sexp` for LRA or `micromath-v2.sexp` for LIA; builder-only replays need only the statement. This compact archive is not the full 109-case payload export.

Native solver crashes, native export/reconstruction failures, our statement-converter failures and reference-solver timeouts are outside this checker handoff. The index is evidence from the pinned primary run, not a claim that every case still fails on a newer checker. Validation of this packaging consists of archive/payload hashes and shell syntax; no new replay success or failure is claimed.
