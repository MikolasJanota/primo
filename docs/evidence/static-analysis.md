# Static-analysis triage, 27 October 2026

The [first local audit](../development/static-analysis.md) recorded 745 distinct
diagnostics against solver source `88ca313`. This review assigns a disposition
to every original row in the
{diagnosticsource}`machine-readable triage record <docs/coverage/local-static-triage-20261027.json>`. A disposition can
be a guarded case, an intentional contract, a cleanup candidate or an open
follow-up; it does not mean that every warning has been removed or that the
solver has a complete lifetime or exception-safety proof.

## Confirmed rollback allocation defect

Tracing the `ModelScope` exception warning led to
{rollbacksource}`FlatCongruenceClosure::pop <src/theory/equality/flat_congruence_closure.cpp>`.
It resized three representative-proof arrays to the saved node count in every
mode. `ensure_node_count` allocated those arrays only for the representative
`proof-forest` mode or explicit proof diagnostics. A first pop could therefore
allocate unused arrays during BFS or the separate reoriented/structural modes,
including a pop invoked by `ModelScope` destruction.

The repair applies the same mode condition during pop. On a 64-bit build the
three arrays previously added 24 bytes of unused element payload per saved
node, before allocator overhead. This is a storage calculation, not a measured
RSS or timing improvement. The
{rollbacksource}`regression <tests/unit/euf_solver_tests.cpp>` exercises nested
checkpoints and post-checkpoint nodes across all five explanation modes, with
diagnostics both on and off. Disabled representative-proof capacities must
stay zero, and equality rollback must remain correct. The new EUF test
translation unit aborts when linked to the preserved pre-fix libraries; it
passes against the repaired release libraries.

`ModelScope` still treats a violated checkpoint invariant as fatal through its
destructor and `noexcept` move assignment. The repair removes this particular
allocation path; a complete exception-guarantee review with allocation-failure
injection remains open. No solver option or search heuristic changes here.

## Dispositions of the original inventory

| Diagnostics | Count | Review result |
| --- | ---: | --- |
| Include-cleaner | 409 | 20 paired self-includes retained; 389 direct-provider suggestions remain dependency hygiene work |
| Optional access | 109 | 37 production accesses have local or caller/factory engagement invariants; 72 test accesses depend on assertions or fixture setup |
| Enum size and record padding | 82 | Layout candidates; measure containing records and allocation frequency before changing types |
| Easily swappable parameters | 40 | API readability risks; no swapped call demonstrated by these warnings |
| Test exception boundaries, enum casts, assertion side effects and multiplication | 36 | Test failure behavior or bounded/explicitly enabled setup; no production overflow or discarded release setup found |
| Identical switch branches | 5 | Inspected alternatives intentionally have the same result |
| Value/copy/move and optional wrapping | 44 | Ownership boundaries, harmless moves, copy-only receiving interfaces and verified-cleanup candidates are distinguished per row |
| Vector reserve | 6 | Potential allocation cleanup; three sites are test-only |
| String operations | 11 | Test-only construction/search cleanup |
| Production cleanup exception warnings | 2 | Broader exception contract remains a follow-up after the allocation repair |
| Dependency leak warning | 1 | CLI11 `std::function` capture trace; no runtime leak established |

For example, the LRA model mutation warning at the original line 1671 concerns
`saved = current`: `current` is already an owning immutable snapshot, so the
second copy is a cleanup candidate. Keeping the original snapshot matters
because `apply` changes the assignment vector. Likewise, five const payloads
passed through `std::move` still copy vector/model data. These merit a focused
verified cleanup rather than a blanket conversion of values to references.

The optional-access dispositions state their preconditions individually. Fixture-only
unchecked `operator*`/`operator->` accesses need explicit presence checks to
guarantee a clean failure when an assumed fixture condition breaks. A missing
value otherwise violates the optional observer precondition and can cause
undefined behavior. The [standard observer contract](https://eel.is/c++draft/optional.observe)
distinguishes that from `value()`, which throws on absence. These are test
hardening follow-ups, not production fallback policies. Enum width changes also require
care around existing bitmask/index types and public option layouts; a smaller
enum alone does not establish a smaller containing record.

## Make redundant includes fail

Before the first local audit, the default clang-tidy invocation in
[the Makefile at `88ca313`](https://github.com/primo-solver/primo/blob/88ca31342d15c1e8bdebef176cd6b2d7b15daaec/Makefile#L121-L140)
did not enable include-cleaner. Its separate include scan was advisory and
could skip a missing tool successfully. The first audit's `4dca989` tooling
already enabled the broad profile and made missing tools fatal; findings still
needed triage and an explicit acceptance gate. Tool completion was being
confused with review acceptance.

The [local workflow](../development/static-analysis.md) now keeps missing
analyzers fatal and makes both `make tidy` and `make include-cleaner` fail on an
unused include, except an implementation's own `.hpp`/`.h` header. That
exception follows source-relative quoted lookup and the configured
`-iquote`, `-I`, `-isystem` and `-idirafter` order for every compile context.
Unsupported search configurations conservatively receive no exemption. A
source-relative or earlier search-path shadow is rejected, as is an unrelated
matching basename. All 20 current unused-header reports are one-line paired
header anchors, which preserve standalone header compilation. The gate records
them explicitly. Other deliberate includes can use a reviewed LLVM-supported
`IWYU pragma: keep`; no such suppression was added by this review.

Missing direct includes remain visible as advisory findings. Of 389 rows, the
tool supplies 225 insertion proposals. Those proposals are retained in the
triage data; owning/public providers and layer boundaries still need review
before applying a cleanup batch. A synthetic real-clang probe verifies that an
unused extra header fails the gate and using its declared type passes, while
the paired header remains retained.

The [LLVM include-cleaner documentation](https://releases.llvm.org/18.1.8/tools/clang/tools/extra/docs/clang-tidy/checks/misc/include-cleaner.html)
describes the unused/missing distinction; LLVM's
[include-cleaner design](https://clangd.llvm.org/design/include-cleaner) documents
intentional include retention.

## Evidence and remaining work

The {diagnosticsource}`qualification report <docs/coverage/static-diagnostic-repair-qualification-20261027.json>`
and {diagnosticsource}`run ledger <docs/run-log.json>` record 4,200 generated
inputs/sessions, 8,086 direct Dolmen model checks, 45/45 release and 45/45
ASan/LSan native tests, and 284 Python tests on the frozen native snapshot.
The final changed tooling separately passes 26 static-workflow tests plus the
full-driver mocked replay test. The fresh static pass covers 132 translation
units and retains 745 visible diagnostics, with zero redundant-include blockers.
The source repair is qualified separately from the metadata gate. Release,
ASan, differential fuzzing, explicit online-EUF checking and a fresh whole-source
static pass are recorded in the run ledger. The initial
online-EUF job stopped before generating inputs because its reused tool prefix
lacked cvc5; retries use an existing verified cvc5 binary in fresh output
directories. That setup failure contributes no solver evidence. Model replay
also needed to cover finite QF_UF and preserve its forced strong preprocessing.
Finite QF_UFLRA uses the requested profile. The corrected driver shares
per-corpus option vectors between generation and replay; its mocked regression
compares both call sets, including a requested preprocessing override. The
accepted run's metadata agrees on those options. It records and compares the
solver's SHA-256 before and after execution, captures its version and reference
binary hashes, and rejects reuse of an existing task output directory. Intermediate replay
attempts with omitted or changed options are retained as provisional evidence.

The CLI11 warning traces a copied callback into App-owned option state.
A separate ASan/LSan CLI construction/destruction probe checks the exercised
help/version path; it is not proof that every dependency path is leak-free.
The remaining priorities are the checkpoint exception contract, direct-header
hygiene and avoidable const-payload copies. No performance-profile promotion
or full-corpus performance conclusion follows from this review.

(uf-rollback-resource-measurement)=
## UF resource measurement after the repair

CPU SLURM job `258443` compares frozen native sources `0d59704` and `ed8c227`.
Their production source differs only in the guarded proof-array resizing in
`FlatCongruenceClosure::pop`. Both use the same Release compiler, `-O3 -DNDEBUG`,
explicit C++ header search paths and CaDiCaL `3.0.0-7b99c07`; no sanitizers.
The fixed binary is the correctness-qualified candidate described above.
The {ufmemorysource}`six-input manifest <benchmarks/uf-rollback-memory-20261027.json>`
selects two previous quantified-UF failures, two successful quantified-UF cases
and two QF_UF controls. Historical selection times are not the measured baseline.

Each input has three repetitions per binary, alternating execution order within
one allocation. The controller pins itself and its children to one CPU on an
Intel Xeon E5-2690 v4. SLURM allocates two logical CPUs despite a one-CPU request;
the explicit affinity restricts execution to one. The scheduler reservation is
10 GiB; each solver has an observed hard and soft 8 GiB `RLIMIT_AS`, a 180-second
deadline and arguments `--quiet --stats`. All six array tasks completed their
six measurements, for 36 solver runs total. No run reached its deadline.

The table gives medians over three repetitions. Failure times measure time to
failure, not solve time. Peak RSS is the kernel's per-process `wait4` measurement.

| Input suffix (logic) | Result, both binaries | Time before → after (s) | Peak RSS before → after (MiB) |
| --- | --- | ---: | ---: |
| `FFT/uf.706467` (UF) | Cleanup abort | 54.09 → 54.13 | 5942.5 → 5943.4 |
| `Fundamental_Theorem_Algebra/uf.828004` (UF) | Explicit `bad_alloc` | 83.43 → 83.34 | 6345.8 → 6344.3 |
| `dl_copy_loop_check_heap_access_31_6` (UF) | UNSAT | 22.67 → 22.16 | 111.3 → 110.7 |
| `QEpres/uf.804465` (UF) | UNSAT | 22.20 → 22.00 | 133.3 → 139.2 |
| `PEQ011_size8` (QF_UF) | UNSAT | 14.44 → 14.44 | 24.7 → 24.2 |
| `iso_icl_nogen_sk007` (QF_UF) | UNSAT | 22.08 → 22.18 | 364.6 → 365.5 |

Each binary solves four of six inputs in every repetition. All 24 successful
runs agree with the declared UNSAT status and their paired binary. There are
six explicit allocation-failure runs and six cleanup-abort runs, zero timeouts
and no other execution errors. No SAT answer needs post-run model validation;
this does not independently certify the UNSAT answers. The cleanup abort has
the same signature as the [earlier allocation trace](long-limits.md), but this
measurement does not collect a fresh allocation stack trace.

All reported non-timer counters match across binaries and repetitions for each
successful input. The two quantified cases report respectively 15,641 and
19,564 instances, 4,311 and 10,976 pool terms, and 1,249 and 259 SAT conflicts.
The QF_UF controls report 27,841 and 119,338 incremental-EUF backtracks and
5,890 and 161,966 SAT conflicts. Quantified UNSAT's zero incremental-EUF
counters have a {ref}`publication limitation <quantified-uf-counter-publication>`;
they do not measure resident closure size. Failed runs print no final counters.

Neither failing case is recovered. Their sampled virtual-memory peaks are
essentially unchanged: about 7,498.5 and 8,145.2 MiB. Sampling every 50 ms gives
lower bounds on those peaks; an allocation can fail below the address-space cap
when its next buffer cannot fit. The successful cases have mixed memory changes,
and timing differences from −2.24% to +0.46% on this small selected sample do not
establish a general speedup. The removed dormant arrays are a real allocation
defect, but these measurements do not attribute the prior memory-outs to it.
[Term-lifetime and storage attribution](../leads/incremental-memory.md) remains open.

The {ufmemorysource}`machine-readable report <docs/coverage/uf-rollback-memory-20261027.json>`
contains full source and binary hashes, metric ranges, selected counters and
raw-artifact fingerprints. Local raw results are
`results/uf-rollback-memory-20261027/258443/`. Reproduce execution with the
{ufmemorysource}`CPU batch script <scripts/qualification/uf_memory/paired.sbatch>`:
set `PRIMO_WORKDIR`, `PRIMO_UF_PLAN`, `PRIMO_UF_ROOT`, `PRIMO_QF_UF_ROOT`,
`PRIMO_UF_OUT`, `PRIMO_UF_BASELINE` and `PRIMO_UF_FIXED` to the prepared workspace,
manifest, corpus roots, fresh output directory and fingerprint-matching binaries,
then submit with `sbatch`. The controller is single-threaded, as required by its
child-only `preexec_fn` resource-limit setup. Rebuild the summary with the
{ufmemorysource}`summary script <scripts/summarize_uf_memory.py>` without running
the solver:

```sh
python3 scripts/summarize_uf_memory.py \
  --plan benchmarks/uf-rollback-memory-20261027.json \
  --results results/uf-rollback-memory-20261027/258443 \
  --output docs/coverage/uf-rollback-memory-20261027.json
```


## LLVM runner output compatibility

The {staticrunnerreportersource}`report adapter <scripts/summarize_static_analysis.py>`
accepts both bare `clang-tidy` command lines and LLVM 21's
`[completed/total][seconds]` invocation prefixes. It still requires every
expected translation unit, unchanged source hashes and a successful aggregate
tool exit. Recognizing the newer format establishes execution coverage; it
does not classify diagnostic findings as benign.
