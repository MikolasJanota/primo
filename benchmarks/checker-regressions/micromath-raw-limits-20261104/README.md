# Solver outputs for the Micromath author

These are the original Primo outputs for the eight checker resource limits
remaining after raw replay `347770` with Micromath `48addc4`.
Each `.primo` contains the exported CNF and its CORE-reduced theory lemmas.
The author can regenerate the challenge, CNF/LRAT and raw kernel proof from it.

Start with [3492-SEQ009_size10.primo](3492-SEQ009_size10.primo): 430,806 bytes and zero theory lemmas.
Its generated raw proof reached our 1 GiB output-file cap.

| Download | Solver outputs | Compressed bytes |
|---|---:|---:|
| [proof-file-limit-inputs.tar.gz](proof-file-limit-inputs.tar.gz) | 4 | 3,313,004 |
| [challenge-timeout-inputs.tar.gz](challenge-timeout-inputs.tar.gz) | 4 | 4,488,005 |

Each archive contains only four unmodified `.primo` files, ordered by size.
The [manifest](manifest.json) records source names, byte counts, hashes and limits.
The files already use CORE reduction and experimental object-sort erasure;
they have not been delta-minimized. Packaging was hash-checked without a new
solver or checker run.

| Input | Bytes | Retained theory lemmas | Observed limit |
|---|---:|---:|---|
| `3492-SEQ009_size10.primo` | 430,806 | 0 | proof_file_limit |
| `0009-iso_icl_nogen_sk006.primo` | 17,488,186 | 2,211 | challenge_timeout |
| `0011-iso_icl_nogen006.primo` | 17,524,390 | 2,300 | challenge_timeout |
| `2723-iso_icl_repgen_sk010.primo` | 19,258,506 | 53,979 | proof_file_limit |
| `0015-QF_UF_sokoban.3.prop1_ab_br_max.primo` | 20,362,346 | 616 | challenge_timeout |
| `0008-iso_icl_nogen_sk005.primo` | 24,496,551 | 9,838 | challenge_timeout |
| `0362-PEQ002_size7.primo` | 30,599,268 | 17,870 | proof_file_limit |
| `1800-PEQ012_size6.primo` | 56,996,020 | 18,478 | proof_file_limit |

Proof-file limits occurred while `smt/prove-lrat --raw` wrote a 1 GiB file,
before independent kernel checking. Challenge timeouts used a 15-second
stage limit. Workers had 3 GiB address space. These are observations under
our resource limits; downstream trace generation on another setup can differ.
