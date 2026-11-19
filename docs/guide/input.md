# SMT-LIB input and results

Use the [capability map](../capabilities.md#supported-routes) to choose a
logic and session mode. This page explains the input conventions and how to
interpret responses. Source links identify the implementation described.

## A command stream

Start with `set-logic`, then declare symbols, assert Boolean formulas, and
call `check-sat`. Model options must precede `set-logic`; see [](models.md).
Assertions accumulate until retracted by `pop`, `reset-assertions`, or `reset`.
Each filename on the command line starts a separate session.

The implemented commands are:

| Task | Commands and boundaries |
| --- | --- |
| Select logic/options | `set-logic`, `set-option`; accepted SMT-LIB options are `:produce-models`, `:print-success`, and `:global-declarations false`. Solver tuning uses CLI options. |
| Declare symbols | `declare-sort` with arity zero, `declare-const`, `declare-fun`. |
| Define abbreviations | Nonrecursive `define-fun`, with typed parameters and result. |
| Solve | `assert`, `check-sat`; `get-model` requires an enabled, current SAT model. |
| Scope and restart | `push`, `pop`, `reset-assertions`, `reset`, `exit`; see [](sessions.md). |
| Metadata | `set-info` accepts metadata syntax; this does not make annotations operational solver options. |

Commands such as `check-sat-assuming`, `get-value`, `get-proof`, and
`get-unsat-core` are not implemented. In particular, the CLI option
`--incremental-mode=assumptions` does not enable the SMT-LIB
`check-sat-assuming` command.

The command parser and session interpreter are
{source}`src/smtlib/parser.cpp` and {source}`src/smt/smt_session.cpp`.

## Terms and arithmetic

Supported forms include Boolean connectives, equality, `distinct`, typed
`let`, `ite`, uninterpreted applications, and the linear arithmetic operations
admitted by the chosen logic. Use explicit sorts; declarations and applications
are type-checked.

Real arithmetic uses exact rational values. Write a negative number as
`(- 2)` and a fraction as `(/ 1 3)`. Multiplication in a linear expression
requires a literal coefficient; division requires a nonzero literal divisor.
Products of two symbolic arithmetic terms are outside the supported fragment.
Primo does not implement floating-point arithmetic or general nonlinear solving.

For integer input, `div`, `mod`, and `abs` are accepted extensions, with a
nonzero literal divisor for division/remainder. Acceptance is not a claim of
strict QF_LIA grammar conformance. Integer solving can return `unknown` even
for accepted input. See [integer arithmetic](../implementation/arithmetic/lia/preparation.md#input-lowering-and-integer-bounds)
for the lowering and search limitations.

Quoted symbols are supported. A quoted and unquoted spelling of the same
ordinary symbol name denote the same symbol; malformed metadata, leading-zero
numerals and unquoted reserved names are rejected. The
[lexical audit](../implementation/frontend.md) records the tested boundary, not a
claim of complete SMT-LIB conformance. Term admission is implemented in
{source}`src/smt/frontend.cpp`.

## Quantified UF

Select `UF` for the supported quantified uninterpreted-function route. For
example, this universal assertion contradicts the instance at `a`:

```{literalinclude} ../examples/quantified.smt2
:language: lisp
```

```bash
./build/primo --quiet docs/examples/quantified.smt2
```

Expected output:

```{literalinclude} ../examples/quantified.txt
:language: text
```

This route can prove contradictions, but a surviving candidate is not in
general a SAT result: only the implemented covered cases return `sat`;
otherwise the solver returns `unknown`. Quantified arithmetic, E-matching and
printed models for quantified formulas are not supported. The
[quantifier chapter](../implementation/quantifiers.md) explains the precise covered cases and
implementation. Quantifiers are not enabled in assumptions mode.

## Reading the result

| Response | Meaning |
| --- | --- |
| `sat` | A satisfying interpretation was found for this query. Request a model separately. |
| `unsat` | The active assertions are inconsistent. This is not an emitted proof certificate. |
| `unknown` | The query was not decided, for example because of incomplete reasoning, a resource limit, or cancellation. |
| `unsupported` | The requested logic, feature, command or option is not supported. A diagnostic is sent to stderr. |
| `(error "...")` | A syntax, state or other command error occurred. The CLI stops processing that stream. |

An `unknown` response does not establish whether the formula is satisfiable.
The included `unsupported.smt2` file requests `QF_BV` and produces
`unsupported`, not `unsat`.

Inspect both the response and the [process exit status](../reference/options.md#output-and-exit-status).
Do not interpret every non-SAT response or nonzero exit as UNSAT.
