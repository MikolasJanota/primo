# Frontend: syntax, names, and typed terms

The frontend turns each supported SMT-LIB command into typed core terms.
The parser recognizes syntax; `Frontend` resolves names and checks sorts;
`SmtSession` decides whether the command is legal in the current session.
The SMT layer translates these terms into SAT literals and clauses and passes
theory literals to theory solvers. Neither interface exposes parser nodes;
the SAT backend does not inspect core terms.

## Representation and ownership

| Owner | State and lifetime |
| --- | --- |
| {source}`Parser <src/smtlib/parser.hpp>` | Owns a copy of string input, or borrows an input stream. The parser cannot be copied. The stream must remain alive while parsing. |
| {source}`Lexer <src/smtlib/lexer.hpp>` | Borrows input storage or a stream buffer; each returned token owns its text and byte offset. |
| {source}`SExpr <src/smtlib/ast.hpp>` and {source}`Command <src/smtlib/commands.hpp>` | Own syntax, including lexical category and canonical symbol text. Session journal entries own commands. |
| {source}`Frontend <src/smt/frontend.hpp>` | Owns the symbol table, term manager, definition bodies, binding environments, and scope trails through its implementation object. |
| {source}`TermBuilder <src/smt/frontend.cpp#L38-L116>` | Translates syntax during a command using an explicit work stack. Its syntax references do not escape that translation. Results are manager-owned `TermId`s. |

Term creation goes through `TermManager`; the [core chapter](core.md) explains
identity, hash-consing, and storage invalidation. The frontend returns IDs, not
borrowed references into a growing term vector. A term ID remains meaningful
only with its owning manager. Replaying a session can create a different manager
and different IDs for the same input text.

## Reading a command from a live stream

{source}`TokenSExprParser::parse_one <src/smtlib/parser.cpp#L24-L67>` keeps
one frame per open parenthesis. Closing a list moves its elements into an
`SExpr`; closing the outer list returns the command immediately. It does not
request the next token first. This matters for an interactive client that sends
`(check-sat)` and waits for the answer before sending more bytes.

The {source}`CLI loop <src/main.cpp#L87-L125>` parses one command, passes it
to the session, prints its response, and flushes output. There is no separate
framing parser. A truncated list or quoted token is a syntax error; clean EOF
between commands ends input. Parsing uses space proportional to the syntax
being retained and explicit nesting frames, rather than C++ recursion per list.
This does not establish stack safety for every later solver operation.

## Names and lexical categories

The {source}`lexer <src/smtlib/lexer.cpp>` distinguishes symbols, reserved
syntax, numerals, decimals, strings, keywords, and binary/hexadecimal tokens.
Recognizing a token does not mean its theory is supported: bit-vector literals
can be lexed while bit-vector solving remains unsupported.

`gate` and `|gate|` have the same canonical name. In contrast, numeral `12`
and symbol `|12|` retain different categories. Similarly, quoted `|let|` is a
name, not the binding construct. Symbol lookup must use both the grammatical
role and the canonical name; removing delimiters without preserving category
would conflate these cases.

The implemented compatibility policy accepts commas in simple names and literal
backslashes/control bytes inside quoted names. A backslash does not escape a
closing pipe. It rejects leading-zero numerals and decimal integer parts,
and recognizes TAB, LF, CR, and space as whitespace. Comments end at LF or CR.
These choices are explicit extensions or restrictions, not a claim of complete
SMT-LIB conformance. UTF-8 well-formedness is not separately checked.

{source}`Attribute validation <src/smtlib/attributes.hpp>` checks the generic
keyword/value grammar for metadata and annotations. Accepting an annotation's
syntax does not implement its semantics: named assertions do not provide an
unsat-core service. Quantifier patterns have the limited role described in
[quantifiers](quantifiers.md).

## Binding and definition expansion

{source}`Local lookup <src/smt/frontend.cpp#L295-L315>` searches visible
binding scopes before global declarations. Each binding records its term or
binder level. Quantified variables become the indexed representation described
in [quantifiers](quantifiers.md#what-a-de-bruijn-index-means).

### Let bindings

`let` is resolved during term construction; it does not introduce a core term
node. At the same quantifier depth, a use of a bound name reuses the stored
`TermId`. Inside additional quantifiers, lookup adjusts bound-variable indices;
that adjustment can create new hash-consed terms through the term manager.
The local lookup above implements these scope and index rules.

`let` is parallel: every right-hand side is built before the new bindings become
visible. For example, with global `a`, `(let ((a true) (b a)) b)` denotes the
global `a`, not `true`. Duplicate names in one `let` use the selected
last-binding-wins extension; all their right-hand sides still use the outer
environment. See {source}`dispatch_let <src/smt/frontend.cpp#L764-L800>`.

### Definition expansion

A parameterized `define-fun` stores its body syntax and parameter sorts.
Arguments are translated in the caller's environment, then the definition body
is translated with only its own parameters and body-local bindings visible.
{source}`Definition expansion <src/smt/frontend.cpp#L1135-L1190>` records a
visibility boundary so caller-local names cannot capture free names in the body.
Consider:

```lisp
(set-logic QF_UF)
(declare-const a Bool)
(define-fun f ((x Bool)) Bool a)
(assert (not a))
(assert (let ((a true)) (f a)))
(check-sat)
```

The argument to `f` is `true`, but its body still denotes global `a`. The last
two assertions therefore require both `not a` and `a`: the answer is `unsat`.
The existing {source}`lexical-binding regression
<tests/unit/smt_session_tests.cpp#L574-L587>` exercises this distinction.

{source}`Nullary definitions <src/smt/frontend.cpp#L420-L489>` are compiled
once per frontend generation and reused by ID. Their generated side assertions
are retained with the compiled result and replayed when needed. Scope rollback
also rolls back the record of already-emitted side assertions; otherwise a later
use could reuse an auxiliary whose defining constraint was popped.

The session validates parameterized definitions immediately. Rebuild mode uses
an isolated frontend so checking an unused definition does not alter query term
numbering. Assumptions mode validates in the persistent frontend using fresh
internal parameters. Recursive definitions, duplicate formal parameters, and
wrong argument/result sorts are rejected.

## Type checking and conversion

The frontend resolves declared sorts and function signatures before calling
typed core constructors. Assertions must have sort `Bool`. Function application
checks argument count and sorts; equality requires compatible operands.
Chained comparisons become conjunctions of adjacent comparisons. `distinct`
uses pairwise disequalities, which can create quadratically many pairs.

Arithmetic syntax is limited to the supported linear fragment: multiplication
requires a literal coefficient and Real division requires a nonzero literal
divisor. Integer `div`, `mod`, and `abs` reach the
[extended-term transformations](preprocessing.md); parsing them does not add
nonlinear arithmetic support. General mixed Int/Real input is not admitted.

Term construction may also create Boolean argument proxies and side assertions.
The default translation lowers Boolean equalities to propositional formulas.
The experimental [CNF exporter](../reference/qf-uf-conflicts.md) instead selects
{conflictexportsource}`FrontendBooleanMode::PreserveAtoms <src/smt/frontend.hpp>`:
it keeps Boolean equalities and application arguments as terms and introduces
no Boolean argument proxies. Its SMT collector must register every Boolean
constant/application subterm before theory checking, including operands of
Boolean equalities, to enforce Bool's two-element domain. Ordinary sessions
continue to use the default lowering mode.

The [EUF explanation](euf.md#connecting-boolean-terms-to-sat) describes their
meaning; [preprocessing](preprocessing.md) owns the later transformations.

## Failures and limitations

Lexical errors report byte offsets; many command-shape diagnostics use offset
zero. The stream interface does not distinguish every device-level I/O failure
from EOF. There is no recovery after a malformed command.

The distinction between `unsupported`, a terminating command error, and a
solver's `unknown` belongs to the [session interface](sessions.md#responses).
In particular, an `UnsupportedInput` exception during translation is caught as
a session error; the exception's C++ name is not itself the wire response.

## Sources and implementation status

Barrett, Fontaine and Tinelli, *The SMT-LIB Standard*, version 2.7,
release March 27, 2026, §§3.1–3.6 and 4.2.3, supplies the lexical and binding
specification {cite:p}`smtlib2026`. Primo implements the subset and compatibility
choices above. It does not implement the standard's complete command language,
theories, or annotation semantics.

The {source}`parser tests <tests/unit/smtlib_parser_tests.cpp>` cover lexical
categories, owned syntax, malformed input, and command parsing. The
{source}`session tests <tests/unit/smt_session_tests.cpp>` cover canonical names,
definition validation and scope restoration. Historical repairs and external
comparisons are retained in the [preservation map](../coverage/frontend-sessions.md);
this rewrite does not rerun or extend their qualification.
