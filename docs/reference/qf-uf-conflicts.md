# QF_UF conflict-output format, version 1

**Status: experimental single-query exporter.** Use `--export-cnf FILE` to
write this format for one already-CNF QF_UF problem that Primo reports UNSAT.
The file records clauses and theory conflicts; producing it does not independently
validate the conflicts or certify UNSAT. There is no checker in this milestone.

```bash
./build/primo --quiet --export-cnf certificate.sexp input.smt2
```

The supplied CNF is the starting problem. Converting an earlier formula into
CNF, and establishing the relationship between those formulas, remain future
work. Conflict derivations and a propositional refutation are also deferred.

## Running the exporter

The option takes just the output filename. Supply one SMT-LIB input file as the
usual positional argument, or supply the script on stdin. The entire script is
buffered and validated before solving. It must contain one `(set-logic QF_UF)`,
followed by declarations and assertions, and exactly one final `(check-sat)`;
an optional `(exit)` and ignored metadata are accepted. Multiple assertions
jointly form the single problem. Push/pop, resets, definitions, SMT-LIB options,
model requests, commands after exit, and additional queries are rejected. The
CLI also rejects multiple input files and an output file equivalent to its input.

Assertions must already be CNF: conjunctions of clauses, with each clause a
literal or a disjunction of literals. Nested conjunctions and disjunctions of
the same kind are accepted. Input terms are translated through the existing
frontend in `FrontendBooleanMode::PreserveAtoms`, which keeps Boolean equality
and Boolean arguments as terms instead of introducing naming implications. Core
simplifications still apply; exported clauses describe those translated
assertions rather than preserving source spelling or duplicates
removed by that translation. This route performs no preprocessing or general
formula-to-CNF conversion. The first producer further restricts atom arguments
to constants, Boolean values, and uninterpreted applications: embedded Boolean
connectives, embedded equalities, and `ite` are rejected even though the format
can express more general QF_UF terms. Boolean-valued applications may themselves
be arguments, as illustrated in the extended example.

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
With `--stats`, `:export.cnf.clauses` counts exported input clauses and
`:export.theory.conflicts` counts recorded conflicts, including duplicates.

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

The `smtlib::ConflictDocument` value owns declaration text, clause literal
sequences, and conflict literal sequences. `print_conflict_document` serializes
that value without parsing, validating, or consulting solver state. Its caller
must provide well-sorted SMT-LIB text. There are no borrowed references in the
stored document.

The SMT adapter `export_qf_uf_cnf` owns one `Frontend`, its term manager, and the
output document until solving and serialization finish. It validates the whole
command sequence, checks the translated CNF shape and supported sorts, and
prints declarations for all symbols in that frontend. The
{source}`term printer <src/smtlib/term_printer.hpp>` supplies term-text utilities.

Internally, {source}`BooleanClausifier <src/smt/clausifier.hpp>` keeps the mapping
between SAT literals and core terms in the SMT layer. `QfUfConflictCollector`
borrows the adapter's term manager and document during the solve. After the
Boolean-argument registry is complete, it copies the atom mapping into an owned
hash table. Before that registry runs, the collector registers every Boolean
constant or application subterm, including operands of Boolean equalities.
Every such term receives a SAT-selected `true` or `false` value. This maintains
Bool's two-element domain when frontend Boolean lowering is bypassed; leaving
a Boolean operand unregistered could allow an invalid third value in EUF. No new atoms are introduced by the selected lazy path. Each reported
conflict is translated to owned literal strings before SAT receives its learned
clause. An unmapped literal is an error. Without export, the collector pointer is
null and the ordinary solver paths retain their behavior. SAT continues to see
propositional literals only.

Primo's [EUF explanations](../implementation/euf.md#follow-one-conflict) use
learned-clause polarity: reasons already negate the asserted assignments.
The collector complements those literals to produce the conflicting facts
specified here. The lazy engine returns theory conflicts only; online
propagation and model propagation are disabled, so their reason clauses do not
need separate collection in this producer. No SAT-learned clauses are exported.

Atom binding takes expected linear time in the number of atoms. Conflict
collection takes expected linear time in the number of reason literals, plus
term printing. The document retains all emitted text until the solve returns;
there is no streaming or minimization, and repeated terms are printed in full.
This can use substantially more memory than ordinary solving.

`CheckSatOptions::export_cnf` and `check_sat_smtlib` expose the same buffered
single-query route to library callers. Direct `SmtSession` construction rejects
a nonempty export filename because that streaming API cannot validate the
whole script before a query. Full formula-to-CNF export, preprocessing,
incremental queries, and collection from online propagation require further
producer design. The original standalone-format examples remain illustrative,
rather than captured exporter output.

This page is an original output-format and adapter design note. The adapter
reuses Primo's existing lazy solver and term printer; it introduces no new
congruence-closure or proof algorithm. Independent checking is future work.
