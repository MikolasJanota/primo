# Small Micromath resource-failure inputs

These immutable archives reproduce the historical compact-export failures.
Current Primo checking workflows use the author-recommended `--raw` mode and
newer Micromath, with a 1 GiB file cap. The archived replay scripts deliberately
retain their original mode and limits.

These packages contain unchanged failing inputs from the completed CORE sweep
(array `347445`). All 799 proof-generation errors had the same recorded
`Proof export: out of memory` diagnostic under a 3 GiB address-space cap.
The examples below were selected for small existing inputs; they are not
delta-minimized. Each has already undergone CORE theory-lemma reduction.

| Example | Certificate bytes | Archive bytes | Retained lemmas |
|---|---:|---:|---:|
| [smallest-replay](smallest-replay.tar.gz) | [260,826](smallest-replay.primo) | 732,851 | 1,641 |
| [smallest-input](smallest-input.tar.gz) | [197,048](smallest-input.primo) | 913,153 | 1,157 |
| [zero-lemmas](zero-lemmas.tar.gz) | [287,482](zero-lemmas.primo) | 1,535,492 | 0 |

Start with `smallest-replay`: it has the smallest saved input/challenge/CNF/LRAT
payload among all observed OOM cases. `smallest-input` has the smallest standalone
certificate. `zero-lemmas` has no theory lemmas; it is the smaller of the two
zero-lemma OOM replay payloads. “Smallest” refers to these byte metrics, not a
minimal cause or logical subproblem.

Each archive is standalone: `input.primo`, `challenge.umd`, `problem.cnf`,
`problem.lrat`, the original error text, sanitized provenance, `SHA256SUMS`,
a README and `replay.sh`. Only a built Micromath checkout is needed; no Primo
or CaDiCaL build and no new SAT solve is required. Source code, binaries,
credentials, hostnames and private paths are absent.

```sh
tar -xzf smallest-replay.tar.gz
cd micromath-smallest-replay
./replay.sh /absolute/path/to/Micromath
```

The historical checker pin is `ac052d97ea441ca52cfd0a6b904fad8d34098a8e`.
The script runs only the failed proof-generation stage, retaining the original
3 GiB address-space and 256 MiB per-file caps. Historical expected exit status
is 1. Newer checker versions may behave differently. Original stage timing
and exact tool/file hashes are in each archive’s metadata. Packaging was
checksum-checked without running the checker again.

`manifest.json` records package hashes and source benchmark identities.
Experimental sort erasure was enabled during export. These are exported-CNF
checker inputs; source-to-CNF conversion evidence remains future work.
