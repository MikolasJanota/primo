# Command-line options and output

Run `./build/primo --help` for the complete option inventory of your binary.
The {download}`captured help <../examples/primo-help.txt>` belongs to the
documented implementation; its binary/source identity is recorded in the
review ledger. This page explains common controls; the captured inventory
below supplies accepted values and defaults.
[Effective policies](policies.md) explains automatic selection and option dependencies.

## Everyday use

| Option | Default | Use and boundary |
| --- | --- | --- |
| `--help`, `-h` | Off | Print grouped CLI help and exit. |
| `--version` | Off | Print revision/build information and exit. |
| `--quiet`, `-q` | Off | Suppress the startup banner; SMT-LIB responses remain enabled. |
| `--stats` | Off | Write query statistics to stderr. See [](../reference/statistics.md). |
| `--check-model` | Off | Check original assertions against a requested model. Set `:produce-models true` separately; see [](../guide/models.md). |
| `--incremental-mode` | `rebuild` | Choose `rebuild` or the restricted `assumptions` session engine. See [](../guide/sessions.md). |
| `--preprocessing-mode` | `auto` | Choose `auto`, `off`, `basic`, or `strong`. `off` does not bypass required lowering. Assumptions mode admits only `auto` or `off`. |
| `--initial-theory-phase=MODE` | `negative` | Initial SAT truth-value preference: `negative`, `positive`, `none`, `polarity`, or `auto`. See [phase policies](policies.md#initial-phase-hints) for route coverage. |
| `--arithmetic-model-phase=MODE` | `auto` | Refresh arithmetic truth-value preferences from the tableau: `on`, `off`, or `auto`; see {ref}`automatic-lra-heuristics`. |
| `--monotone-elimination`, `--no-monotone-elimination` | `auto` | Enable or disable one-sided Real elimination under the same policy; assumptions sessions reject explicit enabling. |
| `--max-theory-conflicts=N` | Unlimited | Positive budget, up to 1,000,000,000. Counts aggregate SMT-reported conflicts across supported routes; see [conflict-limit accounting](statistics.md#uniform-conflict-budget-implementation-contract). Exhaustion returns `unknown` with `ResourceLimit`. |
| `--qf-uflia-tableau-equalities[=MODE]` | Off | Experimental pre-integer equality delivery: `off`, `scan`, `names`, `fixed`, or `all`. Bare flag selects `all`; use `=MODE` for explicit values. See [policy meanings and limitations](../implementation/uflia-equality-ablation.md) and [prior results](../implementation/uflia-equality-propagation.md). |
| `--lia-cube=MODE` | `off` | Experimental pure QF_LIA model attempt: `off`, `uniform`, or `difference`. QF_UFLIA is unaffected. See {ref}`lia-cubes`. |
| `--lia-cube-steps=N` | `1000` | Repair-step cap for the optional cube LP; exhaustion resumes existing integer search. |
| `--lia-branch-limit=N` | `100000` | Bound integer branch-and-bound nodes; reaching the bound can return `unknown`. See [](../implementation/lia.md). |
| `--cancel-grace-seconds=N` | `1` | Cooperative cancellation grace period after SIGINT/SIGTERM, from 1 to 3600 seconds. This is not a solve-time limit. |

For example:

```bash
./build/primo --quiet --stats docs/examples/first-solve.smt2
./build/primo --quiet --check-model docs/examples/model.smt2
./build/primo --quiet --incremental-mode=assumptions docs/examples/session.smt2
```

Options accept only their declared values/ranges. A name beginning with
`experimental` or a diagnostic switch does not identify a qualified default.
For component-specific controls, use [](../implementation/preprocessing.md),
[](../implementation/euf.md), [](../implementation/lra.md), [](../implementation/quantifiers.md), and
[](../reference/policies.md). The help snapshot includes every registered CLI
option; the prose above is intentionally a practical subset.

The two automatic-budget options, `--euf-prop-budget` and `--euf-prop-delivery`,
accept exactly `auto`, `unlimited`, or unsigned decimal digits fitting in
`std::size_t`. Zero and leading zeros are accepted; signs, whitespace, trailing
characters and overflow are CLI errors detected before solving by
{repairsource}`parse_auto_budget <src/main.cpp>`. The options'
different zero meanings are described in [effective policies](policies.md).

## Phase option names and compatibility

`--initial-theory-phase` chooses the policy installed at initialization;
`--arithmetic-model-phase` controls later tableau-based preferences for
arithmetic atoms. Both control truth values, while SAT keeps variable
selection. Neither option introduces numeric assumptions or a target-value
search policy. The rename preserves defaults and route behavior.

The arithmetic option already uses the current assignment without additional
pivots for phase selection. Its [implementation and paper basis](../implementation/lra.md#model-phase-hints)
describe this existing mechanism and its delivery differences from the literature.

The old `--theory-phase` spelling is an alias of
`--initial-theory-phase`. Legacy `--lra-model-phase` (including
`--lra-model-phase=false`) and `--no-lra-model-phase` remain accepted in the
help's compatibility section. If both legacy flags are supplied, the negative
form retains precedence. Do not mix the new arithmetic policy with either
legacy flag: this is a command-line error, even when their values agree.
Use `--arithmetic-model-phase=auto` explicitly to request automatic selection.
C++ policy fields and existing statistic keys keep their names. The
{phaseclisource}`CLI implementation <src/main.cpp>` performs the rename
and compatibility resolution; [phase-control literature](../papers/arithmetic-phase-control.md)
explains the separate heuristic investigation.

## Preprocessing diagnostic

`--preprocess-only` stops before search. Use it with `--stats` to inspect
preprocessing cost and formula-size counters independently of solving. It
returns `unsat` if ground preprocessing proves a contradiction and `unknown`
otherwise, including when simplification leaves no assertions. It does not
print the transformed formula. With quantified `UF` input it stops after the
frontend: ground preprocessing does not support binders, and neither
instantiation nor SAT/theory search runs. Unsupported inputs remain unsupported.
The option defaults off and is not accepted in assumptions mode.

## Output and exit status

SMT-LIB answers, models and errors go to stdout. Diagnostics, startup information
and optional statistics go to stderr. Use `--quiet` when scripting.

The command loop returns zero when it finishes without a command/unsupported
failure; this includes normal `sat`, `unsat` and `unknown` responses. It returns
2 for a command error or an unsupported command/solving result. File-opening and
most explicit option-validation failures return 1. The incompatible combination
of `--qf-uflra-online-euf` and `--model-equality-branching` returns 2; CLI parsing
can return its own nonzero status. Cancellation and external termination need separate treatment.
An exit code alone never distinguishes SAT from UNSAT.

Each input file creates a fresh session. A command error stops the current
stream; an unsupported response marks a nonzero status but can allow later
commands to be processed. For reliable automation, capture the full response
sequence and stderr, and identify which `check-sat` each answer belongs to.

These conventions come from {source}`src/main.cpp` and
{source}`src/cli/response_printer.cpp`.

## Where defaults live

CLI registration and validation currently live in {source}`src/main.cpp`, enum
spellings in {source}`src/cli/option_names.hpp`, and solver defaults in
{source}`src/smt/check_sat.hpp`. Strategy selection can determine whether a
particular option takes effect. The placeholder {source}`src/cli/options.cpp` and
{source}`src/cli/options.hpp` are not the authoritative option implementation.

The documentation example checker compares the captured help with the built
binary. Update the help snapshot only after reviewing changed defaults and the
affected manual pages; it is evidence for this revision, not a substitute for
explaining new behavior.

## Complete CLI inventory

The snapshot is checked by the existing documentation example checker. The
word “scaffold” in the banner is the current CLI description, not a capability
classification. Experimental split counts are bounded by the available atoms,
as explained in [tactics](tactics.md).

```{literalinclude} ../examples/primo-help.txt
:language: text
```
