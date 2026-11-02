# QF_UF conflict-output format, version 1

**Status: experimental single-query exporter.** Use `--export-cnf FILE` to
write this format for one QF_UF problem that Primo reports UNSAT. The input may
contain nested Boolean structure. The file records the generated CNF and theory
conflicts; producing it does not independently validate conflicts or certify
UNSAT. There is no product checker in this milestone.

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

The exported CNF is the starting problem for future checking. Establishing its
relationship to the original formula is still future work; no CNF conversion
proof is emitted. The ground fragment handled here excludes `ite`, quantified
terms, arithmetic, and other extended terms that require a separate lowering
step. This is a limitation of the first producer, rather than a requirement
that the input have no Boolean structure.

Export selects the existing lazy EUF engine with theory propagation and purity
filtering off. It bypasses the ordinary preprocessing/tactic route; options for
those routes do not change this exported problem. Incremental assumptions mode,
portfolio solving, and preprocess-only mode are incompatible. SAT backend
options, cancellation, and the theory-conflict limit remain applicable. This
mode is intended for output experiments, with no performance claim.

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

The {conflictexportsource}`smtlib::ConflictDocument <src/smtlib/conflict_document.hpp>` value owns declaration text, clause literal
sequences, and conflict literal sequences.
{conflictexportsource}`print_conflict_document <src/smtlib/conflict_document.cpp>` serializes
that value without parsing, validating, or consulting solver state. Its caller
must provide well-sorted SMT-LIB text. There are no borrowed references in the
stored document.

The SMT adapter {lemmareductionsource}`export_qf_uf_cnf <src/smt/qf_uf_export.cpp>` owns one `Frontend`, its term manager, and the
output document until solving and serialization finish. It validates the whole
command sequence and the ground fragment, and prints declarations for all
symbols in that frontend. The {source}`term printer <src/smtlib/term_printer.hpp>`
supplies term-text utilities.

{cnfcapturesource}`BooleanClausifier::ClauseObserver <src/smt/clausifier.hpp>`
is an optional synchronous callback at the
clausifier-to-SAT boundary. Each clause passes through one {cnfcapturesource}`insertion helper <src/smt/clausifier.cpp>`,
which invokes the observer and then forwards the same clause to the backend.
The observer must copy data it retains and must not mutate the clausifier,
term manager, or backend, or reenter clausification.
Without an observer, the ordinary clause insertion path remains active.

{lemmareductionsource}`QfUfConflictCollector <src/smt/qf_uf_export.hpp>` borrows the adapter's term manager and document during
the solve. The observer copies initial clauses as SAT literals while the
clausifier is still building its atom mapping. After the ordinary Boolean
argument registry is complete, the collector copies that mapping into an owned
hash table, assigns fresh names to propositional auxiliaries, and translates
the captured clauses to owned text. It then seals the initial CNF; a later
clausifier emission is an error rather than a silently omitted clause. The
selected lazy path introduces no further clausification during search.

Every reported theory conflict is translated to owned literal strings before
SAT receives its learned clause. An unmapped theory reason literal is an error.
The initial CNF observer is separate from theory-conflict collection, so theory
lemmas do not get mixed into the input section. The SAT backend continues to see
propositional literals only; it does not know about the exporter or inspect terms.

Primo's [EUF explanations](../implementation/euf.md#follow-one-conflict) use
learned-clause polarity: reasons already negate the asserted assignments.
The collector complements those literals to produce the conflicting facts
specified here. The lazy engine returns theory conflicts only; online
propagation and model propagation are disabled, so their reason clauses do not
need separate collection in this producer. No SAT-learned clauses are exported.

Atom binding and input-clause translation take expected linear time in the
number of declared symbols and sorts, theory atoms, and captured literals,
plus name/text processing and term printing. Conflict
collection takes expected linear time in the number of reason literals, plus
term printing. The document retains all emitted text until the solve returns;
there is no streaming, and repeated terms are printed in full. Optional
reduction retains raw clause copies and creates a separate SAT instance as
described above.
This can use substantially more memory than ordinary solving.

{lemmareductionsource}`CheckSatOptions::export_cnf <src/smt/check_sat.hpp>` and
{lemmareductionsource}`check_sat_smtlib <src/smt/check_sat.cpp>` expose the same buffered
single-query route to library callers. Direct `SmtSession` construction rejects
a nonempty export filename because that streaming API cannot validate the
whole script before a query. The reduction option without an export filename
is also rejected by the library entry point. Preprocessing, extended-term lowering, incremental queries, conversion proofs,
and collection from online propagation require further producer design. The original standalone-format examples remain illustrative,
rather than captured exporter output.

This page is an original output-format and adapter design note. The adapter
reuses Primo's existing lazy solver and term printer; it introduces no new
congruence-closure or proof algorithm. Independent checking is future work.
