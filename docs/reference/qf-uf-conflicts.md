# QF_UF conflict-output format, version 1

**Status: experimental single-query exporter.** Use `--export-cnf FILE` to
write this format for one QF_UF problem that Primo reports UNSAT. The input may
contain nested Boolean structure. The file records the generated CNF and theory
conflicts; producing it does not independently validate conflicts or certify
UNSAT. Primo bundles no checker; the external Micromath route below covers a
restricted subset of this format.

```bash
./build/primo --quiet --export-cnf certificate.sexp docs/examples/qf-uf-export.smt2
```

The {download}`runnable SMT-LIB example <../examples/qf-uf-export.smt2>` uses
an `or` over an `and`; each possible branch contradicts its EUF disequalities.

## Running the exporter

The option takes just the output filename. Supply one SMT-LIB input file as the
usual positional argument, or supply the script on stdin. The entire script is
buffered and validated before solving. It must contain one `(set-logic QF_UF)`,
followed by declarations, optional `define-fun` commands, and assertions, and
exactly one final `(check-sat)`. An optional `(exit)` and ignored metadata are
accepted. Multiple assertions jointly form the single problem. Push/pop,
resets, SMT-LIB options, model requests, commands after exit, and additional
queries are rejected. The CLI also rejects multiple input files and an output
file equivalent to its input.

The ordinary frontend translates terms, expands definitions, lowers Boolean
equality, and introduces definitions for compound Boolean arguments. The
existing Boolean clausifier handles nested `and`, `or`, `not`, and implication.
The exporter copies **every clause that this clausifier sends to SAT**, including
its Tseitin definitions, root constraints, and truth-constant units. Input need
not already be CNF. It does not independently convert the input to clauses or
implement a new clausification algorithm.

Each theory-atom variable is printed as its SMT-LIB atom. Remaining propositional
variables, including Tseitin auxiliaries, receive fresh declared Boolean names
`primo_cnf_0`, `primo_cnf_1`, and so on, skipping names already used by any symbol
or sort. Their defining clauses are included in the CNF. The normal frontend's
Boolean argument proxies are also declared and their defining clauses are
captured. Thus the artifact is self-contained without exposing SAT variable
numbers. Core simplifications and frontend translation can change source
spelling, order, and duplicates before capture.

The exported CNF is the starting problem for independent checking. Establishing its
relationship to the original formula is still future work; no CNF conversion
proof is emitted. Ground Boolean and object-valued `ite` use the solver's
existing extended-term lowering, including its fresh symbols and defining
assertions. Quantifiers and interpreted sorts remain outside this producer.

Export uses the normal query preparation, preprocessing profile, solver dispatch,
and Boolean clausifier. Engine, propagation, purity-filter and clausification
options retain their ordinary meanings. Incremental assumptions mode, portfolio
solving, branching tactics and preprocess-only mode are incompatible with the
single-CNF artifact. Cancellation and the theory-conflict limit remain applicable.
If preprocessing establishes UNSAT without invoking SAT, the exporter serializes
its false result through the same clausifier, producing an empty clause and no
theory lemmas. `:export.preprocessing-unsat=1` marks this case. Such a small
artifact is not evidence that the source-to-CNF conversion has been certified.

An UNSAT result writes one complete S-expression to `FILE`, replacing existing
contents. A SAT or unknown result does not write the path; a pre-existing file
therefore remains unchanged and must not be mistaken for this run's artifact.
Rejected requests also leave the path untouched. An output open/write failure
reports an error result and a nonzero exit status; a failed write may leave a
partial file. Normal solver results stay on stdout and diagnostics on stderr.
With `--stats`, `:export.cnf.clauses` counts captured input/definition clauses and
`:export.theory.conflicts` counts retained conflicts, including duplicates.
`:export.theory.conflicts-collected` and `:export.theory.conflicts-removed`
distinguish the original collection from reduction.

## Reducing theory lemmas

```bash
./build/primo --quiet --export-cnf certificate.sexp --export-cnf-reduction=core input.smt2
./build/primo --quiet --export-cnf certificate.sexp --export-cnf-reduction=mus input.smt2
```

The reduction option requires `--export-cnf`. The default `none` retains all
collected conflicts. `core` uses one failed-assumption core query in a separate
SAT instance; it returns an UNSAT subset that may contain redundant lemmas.
`mus` starts from that core and removes lemmas until every remaining one is
necessary. This is **subset minimality relative to the fixed input CNF**, not
minimum cardinality. Different subsets can be valid answers. If the input CNF
alone is propositionally UNSAT, both modes return no conflicts.

The separate oracle asserts the captured input clauses permanently and guards
each learned theory clause with a fresh activation variable. Assuming a
variable activates its clause. These variables are private to reduction and
never appear in the artifact. All queries are propositional: asking an SMT
solver would make every valid theory lemma redundant by theory reasoning.
Reduction changes only the conflict-record selection, preserving their order,
literal contents, the input CNF and signature. It does not minimize individual
conflicts or independently check their theory validity.

The reducer owns a fresh incremental-safe SAT backend for one export and reuses
its search across subset queries. It does not enable incremental SMT-LIB input.
Cancellation applies during reduction; an interrupted request returns unknown
and writes no artifact. Main-solver CaDiCaL options and theory-conflict limits
are not applied to the postprocessor. There is no separate reduction budget.
Reduction adds retained raw clause copies and another SAT instance to memory
use and can require many SAT calls.

With `--stats`, `:export.reduction.sat-calls` counts actual reduction solves and
`:export.reduction.core-removed` counts initial core removals. For `mus`,
`:export.reduction.block-queries`, `:export.reduction.binary-queries`, and
`:export.reduction.max-block-size` expose the progression search;
`:export.reduction.completed=1` marks a completed requested reduction.
The [design and literature note](../leads/theory-lemma-reduction.md) maps
CAV 2013 Algorithms 3–4 to the implementation and records selector-free
alternatives. Specialized model-guided algorithms are not implemented.

## Temporary object-sort erasure

```bash
./build/primo --quiet --export-cnf certificate.sexp --experimental-export-erase-sorts --export-cnf-reduction=mus docs/examples/qf-uf-export-multisort.smt2
```

`--experimental-export-erase-sorts` is a temporary, disabled-by-default option
for testing the external checking framework. It requires `--export-cnf` and is
intended to be removed when the framework supports the original sorts. The
signature contains one fresh `primo_uf_sort` declaration, adding a numeric suffix
to avoid all original sort/function names. Every uninterpreted object sort in
function arguments and results is printed as that common sort. `Bool` and all
function identifiers remain unchanged. Exactly one object sort is declared even
if the original problem has zero or one.

The solver and reducer retain the original many-sorted terms and SAT clauses.
Only declaration text changes; CNF and conflict literal sequences are preserved.
For the supported ground QF_UF fragment this is equisatisfiability-preserving,
by the model construction in the [design note](../leads/experimental-sort-erasure.md).
The same argument preserves the inconsistency of individual conflict records.
Quantified and interpreted-sort inputs remain unsupported. No independent
erasure-proof object is emitted.

With `--stats`, `:export.sort-erasure.enabled=1` marks the rewrite and
`:export.sort-erasure.input-sorts` counts original object sorts. The
{download}`multi-sort example <../examples/qf-uf-export-multisort.smt2>` uses a
function from one object sort to another and becomes acceptable to Micromath
with this option. It does not remove that checker's separate restrictions on
Boolean arguments, positive-arity Boolean-valued functions or Boolean conflict facts.

## Checking with Micromath

[Micromath's SMT tools](https://git.olsak.net/mirek/Micromath/src/commit/505588a6a0b184c992c3cbcd97721507365ec428/smt/README.md)
consume this container directly. The initial six-example qualification used
`df5c5d3b6dcb3310dde35a2dd134927d34973fa4`. The current pinned revision
`505588a6a0b184c992c3cbcd97721507365ec428` adds SAT-checking optimizations;
its upstream SMT tests and all nine frozen-artifact kernel replays passed in
SLURM job `344901`. This checks the prior producer's artifacts, not this pending
exporter change. On the Goel case, SAT checking used 9,899 kernel steps instead
of 34,480, and the proof shrank from 173,809 to 113,624 bytes. This is a counter
comparison on three distinct problems, not a runtime speedup claim; the
{download}`update report <../coverage/micromath-sat-update.json>` records scope
and provenance. Its
[input specification](https://git.olsak.net/mirek/Micromath/src/commit/505588a6a0b184c992c3cbcd97721507365ec428/smt/Primo.md)
supports exactly one uninterpreted object sort, functions from objects to
objects, and propositional Boolean variables in CNF. Conflicts contain object
equalities/disequalities or truth constants. Multiple object sorts,
positive-arity Boolean-valued functions, Boolean function arguments and Boolean variables in
conflicts are outside this checker's fragment. The structured example above is
within it; the extended format example below is not.

Download the external checkout into the ignored dependency directory:

```bash
git clone https://git.olsak.net/mirek/Micromath.git contrib/src/micromath
git -C contrib/src/micromath checkout 505588a6a0b184c992c3cbcd97721507365ec428
```

Build Micromath with `make` in that checkout and provide a CaDiCaL executable
with text LRAT support. Its SMT regression suite needs Python 3.10 or newer;
`MICROMATH_PYTHON` selects an interpreter for the batch workflow.
`MICROMATH_REUSE_BUILD=1` explicitly reuses already built external binaries;
the report records that choice and their hashes. On this project's cluster, building and batch checking
belong in SLURM. The optional {micromathworkflowsource}`Micromath workflow <scripts/slurm_micromath_check.sbatch>`
builds both external tools inside its allocation, runs Micromath's `make
check-smt`, and checks fresh artifacts from all three export-reduction modes, both for
the ordinary single-sort example and the erased multi-sort example.
Its environment parameters select separate external source directories, a
prepared Primo binary, and a results directory; connection details stay outside
the script. Prepare Primo with
{micromathworkflowsource}`slurm_prepare_benchmark_workspace.py <scripts/slurm_prepare_benchmark_workspace.py>`
with `--build-via-slurm` and perform its
`--check-only` preflight before submission. The workflow never copies external
implementation code into Primo.

For a single artifact, these are the proof stages. Set `certificate`,
`proof_dir`, and `cadical` to absolute paths, and run from the Micromath checkout:

```bash
mkdir -p "$proof_dir"
./build-axioms --fragment equality "$proof_dir/axioms.umd"
./smt/build-challenge "$proof_dir/axioms.umd" "$certificate" "$proof_dir/challenge.umd"
./smt/prove-cnf "$certificate" "$proof_dir/problem.cnf" "$proof_dir/problem.lrat" --cadical "$cadical"
./smt/prove-lrat "$proof_dir/challenge.umd" "$certificate" "$proof_dir/problem.lrat" "$proof_dir/solution.ump"
./check-solution "$proof_dir/challenge.umd" "$proof_dir/solution.ump"
```

`build-challenge` encodes the input CNF without assuming the supplied conflicts.
The prover derives those conflicts and combines them with the LRAT replay.
Successful independent replay prints `challenge proven`. The evaluated replay
path handles RUP hints in textual LRAT; RAT hints and binary encoding remain
outside its supported fragment. This proves the
encoded exported CNF unsatisfiable under Micromath's equality axioms; the
relationship between that CNF and the source SMT-LIB script still needs the
conversion evidence discussed above. A conflict-only check with
`./smt/check-conflicts` does not establish propositional sufficiency.

The workflow also checks three rejection cases: a consistent alleged conflict,
an insufficient lemma collection, and a corrupt proof file. Exact revisions,
commands, job status, proof hashes and scope are recorded in
the {download}`run report <../coverage/micromath-check.json>` and the run ledger. No broader-fragment or
performance claim follows from this integration test.

### Historical real-benchmark stress pilot

The original {micromathpilotsource}`pilot harness <scripts/micromath_stress.py>` and
{micromathpilotsource}`SLURM job <scripts/slurm_micromath_stress.sbatch>` select
up to twelve metadata-UNSAT SMT-LIB QF_UF inputs from six families at two size
ranks, with a 1 MiB input cap. Each is tried with `none`, `core`, and `mus`,
using experimental object-sort erasure. A Z3 `sat` answer flags a disagreement
with UNSAT metadata and blocks that case. Reference `unknown`, timeout or error
is recorded without blocking proof attempts. Export, conflict validation,
challenge construction, LRAT/proof generation and kernel replay have separate
logs and outcome states. Only successful kernel replay counts as a verified
profile; a profile-stage timeout or error overrides any partial answer.

Four workers use 3 GiB per-process memory ceilings and 256 MiB per-file limits.
Each reference call has a 20-second limit; each profile shares a 60-second
pipeline budget, including at most 30 seconds for export, plus a two-second
termination grace for a timed-out process. The {download}`pilot report
<../coverage/micromath-stress.json>` records sampling rules, revisions, job
status and results. The {download}`selected manifest <../../benchmarks/micromath-pilot-20261103.json>`
pins the twelve inputs by path, size and hash. Job `339676` completed: three
inputs reached kernel replay in all modes (nine verified profiles), six timed
out during export (eighteen profiles), two encountered Micromath's unsupported
positive-arity Boolean declarations (six profiles), and one required exporter
`ite` lowering (three profiles). All twelve reference answers were UNSAT; no
wrong answer, invalid conflict or invalid completed proof was observed.
The TypeSafe successes declare seven object sorts before erasure. All modes
retained the same lemmas on inputs whose export completed, so this sample shows
no minimization benefit. This is a bounded diagnostic sample, not full-corpus
coverage or a performance comparison.

### Selecting the next stress inputs

The current {preparedexportsource}`stress harness <scripts/micromath_stress.py>` requires a hash-pinned manifest drawn
from recorded native UNSAT results within one second. The
{download}`selected manifest <../../benchmarks/micromath-fast-unsat-20261103.json>`
contains six inputs of 94–821 KB; their historical answers, timings and input
hashes are retained. The harness no longer chooses cases
by size alone. {preparedexportsource}`select_micromath_fast_cases.py <scripts/select_micromath_fast_cases.py>` selects 64 KiB–2 MiB source
files, largest first with at most two per family, verifies historical input
hashes, excludes the previous pilot, and conservatively screens declared
Boolean function arguments and positive-arity Boolean results. This lexical
screen is not a complete SMT-LIB parser or a proof of checker compatibility.
`ite` is allowed through the normal lowering pipeline. The selector can join
`shard-*/answers.csv` and `provenance.json` with `--history DIR
--historical-source-git REV`, or consume prejoined `--records FILE`; it checks
completed, unchanged-input records and confirms the supplied source revision
matches the recorded binary version. Both forms run corpus selection in SLURM.

A five-second `none` export is the admission check. If it fails to establish main
UNSAT, CORE/MUS runs are skipped for that input. Reduced exports have a 30-second
limit; downstream stage limits and the shared 60-second profile budget remain.
Historical native timings and current export/checking timings are separate
measurements. Large source bytes alone do not establish stress on the proof
framework: exported CNF size, collected/retained lemmas and proof sizes must also
be reported. This revised selection and producer are awaiting fresh SLURM
qualification; the historical pilot above validates their earlier revisions only.

## Container and literal syntax

A file contains exactly one S-expression, with these sections occurring once in
the order shown. Keywords and the version are literal tokens; `*` below means
zero or more entries and is not part of a file.

```text
(primo-conflicts
  (version 1)
  (logic QF_UF)
  (signature declaration*)
  (cnf (clause literal*)*)
  (conflicts (conflict literal*)*))
```

The wrapper is custom syntax, not an SMT-LIB command script. Inside it, use
SMT-LIB lexical conventions, including whitespace, semicolon line comments, and
quoted symbols such as `|left endpoint|`. Section names are reserved only at
their structural positions. Version 1 has no additional sections or attributes.

The signature contains `declare-sort` commands for uninterpreted sorts of arity
zero and `declare-fun` commands for constants and functions. `Bool`, `true`,
`false`, equality, and Boolean operators have their usual SMT-LIB meanings.
Declare sorts before functions using them. Declare each user symbol once and
declare every symbol referenced anywhere in the CNF or conflicts, including
auxiliary symbols. Use the same signature for both sections. No definitions,
quantifiers, arithmetic, assertion-stack commands, or solver commands occur in
the container.

A literal is a ground Boolean atom or `(not atom)`. An atom is `true`, `false`,
a Boolean constant or uninterpreted Boolean-valued application, or binary
equality `(= term term)` between terms of the same sort. Terms must be ground
and well-sorted QF_UF terms; Boolean arguments and Boolean equality retain their
SMT-LIB meaning. Boolean expressions inside term arguments do not introduce
extra outer clause connectives. At clause level, use the wrapper's literal
sequence rather than an `or` term. Express disequality as `(not (= s t))`;
`distinct` and nested outer negations are not literal forms in version 1.

## Meaning and polarity

The CNF denotes the conjunction of its clauses. A clause denotes the disjunction
of its literals. An empty CNF is `true`; `(clause)` denotes `false`.

A conflict records **conflicting facts**: its literals are claimed to have an
inconsistent conjunction in QF_UF, independently of the input clauses. For
example:

```lisp
(conflict (= a b) (not (= (f a) (f b))))
```

The facts say that `a` and `b` are equal but their function results differ.
Congruence makes this impossible. Negating each fact gives the corresponding
theory-valid clause:

```lisp
(or (not (= a b)) (= (f a) (f b)))
```

For a conflict sequence with literals l₁, …, lₙ, the intended theory lemma is
¬l₁ ∨ … ∨ ¬lₙ. Negation here is semantic complementation: complementing
`(not atom)` yields `atom`. The conflict itself is not a clause to conjoin with
the input CNF. No explanation steps, equality paths, or congruence witnesses
accompany it.

An empty `(conflict)` claims that the theory alone is inconsistent. It is
structurally representable but is not a valid conflict in ordinary QF_UF.
Similarly, syntactic well-formedness does not make any other recorded conflict
valid. An empty conflicts section records no conflicts and makes no SAT claim.

Preserve literal order, record order, duplicates, and repeated conflicts.
They have no logical significance; version 1 requires no sorting, minimization,
or deduplication. Conflicts may mention declared atoms absent from the input
CNF. They must still be inconsistent without assuming input clauses.

The intended eventual use is that the input CNF together with the complemented
conflict sequences is propositionally UNSAT. Version 1 neither demonstrates
that property nor guarantees that the recorded collection is sufficient.

## Examples and boundary cases

This complete artifact records a congruence conflict:

```lisp
(primo-conflicts
  (version 1)
  (logic QF_UF)
  (signature
    (declare-sort U 0)
    (declare-fun a () U)
    (declare-fun b () U)
    (declare-fun f (U) U))
  (cnf
    (clause (= a b))
    (clause (not (= (f a) (f b)))))
  (conflicts
    (conflict (= a b) (not (= (f a) (f b))))))
```

The downloadable {download}`extended example <../examples/qf-uf-conflicts.sexp>`
also covers transitivity, opposing equality and disequality, multiple sorts,
predicates, Boolean arguments, a quoted name, duplicate literals, and repeated
conflicts. Its input CNF already contains the congruence contradiction above;
the additional conflicts illustrate the format and need not form a minimal set.

These are complete structural boundary examples:

```lisp
; Empty conjunction: true. No recorded conflicts.
(primo-conflicts
  (version 1) (logic QF_UF) (signature) (cnf) (conflicts))

; Input CNF contains false, so it needs no theory lemmas.
(primo-conflicts
  (version 1) (logic QF_UF) (signature) (cnf (clause)) (conflicts))

; Well-formed container, but its empty conflict is an invalid theory claim.
(primo-conflicts
  (version 1) (logic QF_UF) (signature) (cnf) (conflicts (conflict)))
```

Each S-expression above is a separate file. Semantic review cases for a future
consumer include reversing a conflict's polarity, omitting a required equality
premise, undeclared symbols, and ill-sorted equality. These must not be treated
as valid theory evidence merely because parentheses balance.

## Representation and integration

The public representation is typed term text and literal sequences. It contains
no Primo term IDs, SAT variable numbers, flattened terms, congruence-class
representatives, or references to solver memory. There is no ordering or
numbering dependency on Primo's allocation history. An independent consumer
needs only this specification and SMT-LIB term semantics.

The standalone {preparedexportsource}`smtlib::ConflictDocument <src/smtlib/conflict_document.hpp>` and
{preparedexportsource}`print_conflict_document <src/smtlib/conflict_document.cpp>` remain available for independently authored
text documents. The solver exporter instead owns a {preparedexportsource}`smtlib::ConflictSpool <src/smtlib/conflict_spool.hpp>`
with its {preparedexportsource}`implementation <src/smtlib/conflict_spool.cpp>`: three buffered anonymous temporary files for
signature, CNF, and conflicts. It stores conflict byte offsets and lengths only
when reduction needs selection. `TMPDIR` can select node-local scratch. Securely
created temporary files are unlinked immediately and closed on every return.
Temporary-storage failure aborts export; it does not fall back to text in RAM.

{preparedexportsource}`export_qf_uf_cnf <src/smt/qf_uf_export.cpp>` owns one `Frontend`, its term
manager, the spool, and collector through solving and serialization. It validates
the whole command sequence and QF_UF sorts, then calls
{preparedexportsource}`check_sat_frontend_export_once <src/smt/solver_session.cpp>`. That entry point
borrows the frontend and runs the same production preparation and dispatch as
ordinary queries. Signature printing waits until collection binds, so declarations
include fresh symbols introduced by the existing preprocessing/lowering passes.

{preparedexportsource}`BooleanClausifier::ClauseObserver <src/smt/clausifier.hpp>` is an optional
synchronous callback at the clausifier-to-SAT boundary. One insertion helper
invokes it and forwards the same clause to the backend. The observer copies data
it retains and must not mutate the clausifier, term manager, or backend, or reenter
clausification. Both the Boolean and EUF solver leaves accept a collector; without
one they retain ordinary clause insertion and early-return behavior.

{preparedexportsource}`QfUfConflictCollector <src/smt/qf_uf_export.hpp>` borrows the frontend's term
manager and spool. It copies initial clauses as SAT literals while the clausifier
builds its atom mapping. After Boolean argument registration, binding copies the
SAT-variable-to-TermId mapping, assigns fresh Boolean auxiliary names, and streams
initial clause text. Later clausifier emissions are rejected. The current ground
EUF path does not introduce new atoms after binding.

Lazy conflicts and model-propagation clauses are recorded at
{preparedexportsource}`insertion <src/smt/solver_engines.cpp>`. The
{preparedexportsource}`online propagator <src/smt/solver_engines.cpp>` records external conflict clauses and reason clauses when their
terminating zero is requested by SAT. Thus queued but unused propagation reasons
are not exported. Duplicate delivered clauses may remain. No SAT-learned clause
is exported. The SAT adapter sees only propositional literals and has no exporter
or core-term dependency.

Primo's [EUF explanations](../implementation/euf.md#follow-one-conflict) already
use learned-clause polarity. The collector complements those literals to produce
the conflicting facts specified here. An unmapped theory literal is an error.
Each term is streamed by {preparedexportsource}`print_ground_term <src/smtlib/term_printer.cpp>`,
using an iterative traversal without a cache of expanded subterm strings.
No expanded atom, clause or conflict strings are retained by collection.

Collection costs expected linear time in captured literals plus names and expanded
term output. Printing repeated shared terms can still produce large files and
consume substantial CPU. The spool retains bounded I/O buffers and optional
per-conflict offsets, rather than all output text. Numeric CNF copies remain in
memory for CORE/MUS; `none` releases initial raw clauses after binding and stores
no raw theory clauses. The original script and core term DAG remain in memory.
After main UNSAT, the existing SAT-only reducer returns indices; it never reads
text or spools. Final publication copies selected byte ranges through a bounded
buffer. Empty selection emits no conflicts; `none` emits every conflict.

The spool is sealed and flushed before the output path is opened. Spooling errors
therefore leave a pre-existing artifact unchanged, just as SAT/unknown and
interrupted reduction do. A failure during final publication can still leave a
partial output file. `:export.main-unsat` distinguishes the main result from
postprocessing; `:export.solve-and-collect.us`, `:export.format-input.us`,
`:export.format-conflicts.us`, `:export.reduction.us`, `:export.final-write.us`,
and `:export.spool.{signature,cnf,conflict}-bytes` expose work and staged byte counts.
Solve/collection time includes the formatting counters, so these times must not
be summed. See the [storage design note](../leads/cnf-export-spooling.md).

{lemmareductionsource}`CheckSatOptions::export_cnf <src/smt/check_sat.hpp>` and
{lemmareductionsource}`check_sat_smtlib <src/smt/check_sat.cpp>` expose the same buffered
single-query route to library callers. Direct `SmtSession` construction rejects
a nonempty export filename because that streaming API cannot validate the
whole script before a query. The reduction option without an export filename
is also rejected by the library entry point. Incremental queries, branching and
source-to-CNF conversion proofs require further producer design. The original standalone-format examples remain illustrative,
rather than captured exporter output.

This page is an original output-format and adapter design note. The adapter
reuses Primo's normal solver pipeline and the ground term printer; it introduces no new
congruence-closure or proof algorithm. The external checking route above covers
the stated subset; a checker for the full format remains future work.
