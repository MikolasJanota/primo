# Spooling the CNF certificate during solving

This original implementation note specifies an output/storage change, not a new
solving or minimization algorithm. Export must use the normal query preparation
and solver dispatch, including the existing extended-term lowering. No second
`ite` lowering is introduced. Main UNSAT must be established before CORE/MUS
reduction starts. Fresh SLURM qualification of this change is pending; the
tests described below are implementation coverage, not a completed run claim.

The query owns its frontend through collection and serialization. An optional
{preparedexportsource}`QfUfConflictCollector <src/smt/qf_uf_export.hpp>` is passed through the {preparedexportsource}`normal session <src/smt/solver_session.cpp>` and
{preparedexportsource}`solver leaves <src/smt/solver_leaves.cpp>`;
with no collector the ordinary control flow is unchanged. Signature printing
occurs when the clausifier binds, after preprocessing has created its symbols.
Both {preparedexportsource}`Boolean <src/smt/boolean_solver.cpp>` and
{preparedexportsource}`EUF <src/smt/solver_engines.cpp>` leaves observe initial clauses. EUF collection additionally
observes model-propagation clauses, lazy conflicts, and complete external
conflict/reason clauses actually delivered to SAT. A reason is recorded when
its terminating zero is requested, rather than merely when a candidate is queued.

Preprocessing can decide UNSAT before SAT is called. Export then serializes the
resulting false goal through the common Boolean clausifier (an empty clause),
without another SAT solve. CORE/MUS sees this same numeric empty clause. Such a
certificate proves the prepared CNF; source-to-CNF justification remains future
work. Branching strategies and portfolios cannot be concatenated into one CNF
and are rejected. No engine, propagation, purity or clausification setting is
silently overridden.

`QfUfConflictCollector` must retain only SAT clauses for the subset oracle and
SAT-variable-to-TermId mappings for printing. Formatted declaration, clause and
conflict text is written immediately to private temporary section files. No
expanded atom or lemma strings are retained. The existing string-returning term
printer remains available to other callers; a {preparedexportsource}`ground streaming printer <src/smtlib/term_printer.cpp>` traverses
terms iteratively without caching expanded subterm strings.

{preparedexportsource}`smtlib::ConflictSpool <src/smtlib/conflict_spool.hpp>` owns three buffered, anonymous temporary files and,
when selection is enabled, an offset/length pair per conflict. Files are created
securely under `std::filesystem::temp_directory_path()` (so `TMPDIR` can select
node scratch) and unlinked immediately. Destruction closes the files on every
normal/error return; process termination also releases the anonymous files.
The spool has no SAT, theory or core-term dependencies. Callers supply literal
printing callbacks synchronously and may not retain the stream or mutate its
borrowed term manager. Callback/I/O failure poisons the spool and prevents sealing.

Record indices align with raw theory clauses in collection order. Reduction
calls {preparedexportsource}`sat::select_unsat_subset <src/sat/clause_subset.cpp>` on raw CNFs only; it never reads the temporary
files or term text. An UNSAT reduction returns ascending indices. The writer
checks selection bounds/order before output and copies only selected byte ranges
through a fixed-size buffer. Empty selection and no selection are distinct:
CORE/MUS can emit no conflicts, while `none` copies every record. Neither mode
changes original clause bytes or retained conflict bytes.

The spool is flushed and sealed before the final output is opened. SAT,
unknown, rejected requests and interrupted reduction leave an existing output
untouched. An error while publishing final output can leave a partial file, as
before. A temporary-storage failure can now reject an export before UNSAT;
this requirement is explicit rather than falling back to unbounded text RAM.
Only bounded stdio/copy buffers, traversal state, names and record metadata are
retained for text processing. Numeric SAT CNFs remain in memory when reduction
needs them. The original SMT-LIB script and term manager also remain in memory.

Memory for serialized text no longer scales with total emitted bytes. File
space and formatting work still do: the format repeats terms rather than
sharing subterms, and minimization removes whole lemmas only. Streaming is not
a performance claim. Counters expose staged section bytes, plus timings for
solve/collection, input/conflict formatting, reduction and final writing so that
these phases are not confused in subsequent measurements.

Tests compare spooled bytes against the existing standalone serializer, cover
nonadjacent/empty/all selections and records exceeding the copy buffer, reject
invalid selections, and exercise temporary-file errors without touching a final
artifact. A counting stream tests expanded shared ground terms without building
an output string. The normal CLI/reference/fuzz/ASan and external Micromath
checks still apply. The {preparedexportsource}`benchmark selector <scripts/select_micromath_fast_cases.py>` uses recorded fast UNSAT results
with matching hashes; historical/default timings and current export timings must
be labelled separately. Previously timed-out inputs are excluded from this
framework stress selection.
