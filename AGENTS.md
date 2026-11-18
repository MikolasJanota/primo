# Primo Agent Guide

## Execution location: SLURM required

The owner's laptop is an interactive workstation. Every batch run of the solver
belongs outside it, in SLURM: full solver test suites, fuzzing campaigns,
benchmarks, and corpus/model validation. Local builds for interactive debugging
with gdb or similar tools are allowed. Use
`scripts/slurm_prepare_benchmark_workspace.py --build-via-slurm` for builds
supporting cluster qualification. An available
local binary, many CPU cores, or a missing remote dependency is not permission
to fall back to local execution. Do not run compute on the SSH login host.

Local work also includes editing, Git operations, reading results, syntax checks,
and explicitly bounded individual debugging/smoke runs expected to finish within
two minutes. Do not combine smoke runs into a local batch. Use the SSH
route supplied in the session; keep connection details outside tracked files.
If remote execution is unavailable, preserve the work and report that blocker.
Before finishing or changing execution location, inspect the process tree and
terminate every experiment process started locally, including descendants.

## Clean-room policy

Primo is a new solver, not a continuation of LLM2SMT.

The old repository may be used only for:

- understanding previous design decisions
- identifying missing tests
- comparing behavior
- extracting benchmark ideas
- writing retrospectives
- checking which development practices failed or succeeded

The local reference checkout is normally at `/tmp/llm2smt` on this workspace.
Check that path first when comparing against LLM2SMT behavior.

Do not copy source code, generated code, parser grammars, build files, comments, tests, or documentation text from LLM2SMT or from other SMT solvers.

When implementing an algorithm, use:

- the cited paper
- our own design notes
- newly written tests
- newly written code

If a design choice is inspired by LLM2SMT, document it as a retrospective note, not as an implementation source.

## Paper-grounded implementation rule

This solver is implemented from papers and original design notes.

Do not copy code from existing SMT solvers.

If a required paper is hard to access or download, stop and tell the user which paper is needed. Do not substitute a blog post, secondary summary, or guessed details for the unavailable paper.

Every nontrivial algorithmic component must cite the paper, book chapter, or design note it is based on.

Citations are not decoration. Before implementing or claiming a paper-backed
algorithm, read the relevant paper section and turn it into a local design note
or implementation checklist. The checklist must identify the actual data
structures, invariants, operations, complexity expectations, and proof
obligations being implemented. If the implementation is only a scaffold,
baseline, or ordinary textbook subset, say that explicitly and do not name it as
an implementation of the paper's optimized algorithm.

When using a paper to accelerate development:

- implement from the paper's algorithmic content, not from memory of a solver or
  from a previous codebase;
- map each paper operation to the Primo layer and C++ type that owns it;
- add tests that exercise the paper-specific invariant or asymptotic fast path,
  not only generic correctness examples;
- add benchmark counters that reveal whether the paper mechanism is actually
  being used;
- compare the implementation against the paper's stated preconditions and mark
  unsupported cases as limitations;
- if the code does not yet implement the paper mechanism, document the paper as
  motivation/future work, not as implemented behavior.

For each such component, include:

- bibliographic reference
- section or theorem used, where applicable
- implemented subset
- deviations from the paper
- known limitations
- tests derived from the algorithm

Code comments should identify algorithmic basis without overclaiming.

## Documentation synchronization rule

Documentation is part of the implementation, not a follow-up task.

Do not consider a solver change complete unless the relevant documentation is up to date. For changes to parser, terms, symbols, SAT integration, clausification, theory solving, options, build behavior, or solver control flow:

- update the nearest page under `docs/`
- describe the implemented C++ types, ownership, lifetime, invariants, and limitations
- explain whether paper-backed ideas are implemented, partially implemented, or only planned
- keep examples and supported-fragment claims synchronized with the code
- run `make docs`
- commit documentation together with the implementation or in an adjacent commit

If documentation cannot be updated in the same task, state that as a blocker in the final response.

For substantive documentation writing or restructuring, use the writer–reviewer
loop in `skills/documentation/SKILL.md`. One agent owns edits; a separate,
read-only agent reviews source accuracy and exposition and rechecks the fixes.
This explicitly authorizes that bounded delegation for documentation work.
An isolated typo or link repair needs the relevant checks, not a new agent.
If an independent reviewer is unavailable, report that limitation rather than
describing self-review as independent acceptance.

## Goal and completion discipline

For broad solver tasks, define the goal as the full user-visible outcome, not
as the next command, the next local experiment, or a pointer to another file.
The goal is the whole thing the user is asking to get done. A performance goal
includes correctness, implementation, documentation, verification, benchmark
evaluation, result analysis, and commits.

When the user asks to use the goal primitive, create one goal whose objective
spells out the end-to-end acceptance criteria in the goal text itself. Include
the target benchmark set, baseline or reference comparison, required execution
environment, hard correctness conditions, required verification commands,
documentation expectations, and commit expectations. Do not create a goal like
`continue`, `read the plan`, `run benchmarks`, or `fix performance`.

Do not mark that goal complete until all required source, tests, docs, scripts,
and benchmark metadata are committed and the requested evaluation is summarized,
or until a concrete blocker is reached. If a blocker is reached, report the
blocker, the commands or job ids proving it, the last successful verification,
and the remaining acceptance criteria that are still incomplete.

Do not treat any of the following as completion:

- a local-only timing run when the user requested SLURM or full-benchmark
  evaluation;
- a solved-count or PAR2 improvement with known wrong answers;
- an unsound implementation that happens to improve performance;
- uncommitted source, documentation, tests, scripts, or benchmark metadata
  needed to reproduce the result;
- untracked generated implementation files;
- benchmark logs without a summarized comparison against the stated baseline or
  reference solvers;
- an active experiment whose winning or losing status has not been decided and
  committed as either accepted implementation or rejected result artifact;
- a final answer that omits commit hashes, benchmark artifact paths, job ids, or
  verification commands for a broad solver task.

Before the final response for broad solver work, run `git status --short` and
`git log --oneline -3`, then report any remaining tracked or required untracked
files. If committing is feasible, commit coherent milestones before reporting
completion. If committing is not feasible, state the exact blocker and do not
present the work as complete.

## Architectural rules

Preserve these boundaries:

- `smtlib/` parses, prints, and type-checks SMT-LIB.
- `core/` owns sorts, symbols, terms, rewriting, substitutions, and models.
- `sat/` owns propositional variables, literals, clauses, assumptions, and the IPASIR-UP backend adapter.
- `smt/` owns Boolean abstraction, clausification, assertion stacks, and DPLL(T).
- `theory/` owns theory solvers and theory-specific explanations.
- `scripts/` contains reproducible command-line workflows.
- `benchmarks/` contains only input instances and benchmark metadata.
- `docs/` contains the MyST/Jupyter Book documentation site, design notes, literature summaries, and optional experiment notebooks.

Do not introduce cross-layer shortcuts.

In particular:

- SMT-LIB AST nodes must not be used by the SAT or theory solvers.
- SAT literals must not directly contain terms.
- Theory solvers must communicate via theory literals and explanations.
- All terms must be created through the term manager.

## Coding practices

- Do not stop at a partial milestone when following an explicit implementation
  plan or performance objective. Continue through implementation, verification,
  documentation, evaluation, result comparison, and commits until the objective
  is complete, or until there is an obvious roadblock such as a missing required
  paper, unavailable benchmark data, inaccessible cluster, failing dependency
  install, or a correctness failure that needs a design decision from the user.
  If a roadblock occurs, state the concrete blocker, the evidence for it, the
  last completed verification, and the exact unfinished work.
- Use C++20.
- Do not use borrowed references, pointers, iterators, or views after an operation
  that may invalidate their backing storage, including indirect helper calls.
  In particular, snapshot needed IDs/payloads before any term-creating call and
  reacquire `TermManager::get` references afterwards; do not depend on sibling
  argument evaluation order. Apply the borrow/mutation/use check in
  `skills/code-review/SKILL.md` and allocation-boundary regression guidance in
  `skills/testing/SKILL.md` when fixing invalidation bugs.
- Keep C++ source formatted with `clang-format` using the repository `.clang-format`.
- Keep Python scripts runnable with the standard library unless a dependency is documented.
- C++ formatting is `clang-format` with the repository `.clang-format`, currently based on Google style with C++20 and a 100-column limit. Do not hand-format around it.
- C++ warnings are intentionally strict for owned code. The default CMake build treats warnings as errors; external headers are isolated from that policy.
- Use `make asan-test` when touching ownership, parser, term storage, SAT backend, or other memory-sensitive C++ paths.
- Run `make lint` before committing implementation changes.
- Run `make check` before committing behavior changes when feasible.
- Run the fuzz gate described in "Fuzzing discipline" before presenting solver,
  parser, preprocessing, SAT, theory, model, or SMT-LIB behavior changes as
  complete. If the gate cannot run, state the exact blocker and keep the change
  out of headline/default performance claims until fuzzing is done.
- Run the `smt-code-review` skill as a pre-commit self-review for implementation changes. At this stage, review all tracked non-generated project files, not only the diff. The review must include ordinary hygiene: unused includes, dead helpers, missing or misleading comments, narrow API surface, formatting, and lint readiness.
- If `clang-tidy` is available, run it on all tracked non-generated `.cpp` files while the project is establishing its baseline. Also run `misc-include-cleaner` as an advisory scan and inspect it for real unused includes; paired `.cpp`/`.hpp` self-includes are not automatically removable. Later, after the whole-tree baseline is clean, diff-focused clang-tidy plus periodic whole-tree sweeps is acceptable. If clang-tidy is not run, state why.
- Prefer small, coherent commits after each working milestone.
- If a change is rejected after measurement, either revert it cleanly or leave no
  source changes from it, then commit the reproducibility artifact or note that
  records why it was rejected when that artifact is part of the benchmark trail.

## Statistics performance policy

Recurring solver operations must use `util::StatKey` or numeric component-local
counters. Do not construct, concatenate, format, hash or look up statistics names
inside SAT callbacks, propagation/explanation loops, branching, merges or rollback.
This applies to helper functions too. `--stats` controls printing, not collection.
String-based reporting is allowed only at documented cold/dynamic boundaries.
Local counters require explicit publication on completion, resource exits and
reset. Preserve public names, meanings and missing-versus-zero behavior; use new
keys when semantics change. Review statistics storage/copy cost as well as CPU cost.
New exceptions to the statistics lint policy require a cold-path rationale.

## Solver performance and benchmark discipline

Correctness comes before performance: stop tuning on wrong answers, invalid
models, crashes or untriaged reference disagreements. Keep experimental evidence
in `docs/run-log.json`, with exact provenance and resource limits reported
separately; do not claim improvements from unvalidated or stale evidence.

For benchmark/evaluation work, use `skills/benchmarking/SKILL.md`; for optimization,
also use `skills/performance/SKILL.md`. They own the detailed procedure, including
profiling, SLURM preparation, reporting and reproducibility requirements.
For real-corpus model checks and post-benchmark SAT validation, use
`skills/primo-model-validation/SKILL.md`. Full evaluations and default promotions
require its separate correctness stage; model-checking time must not enter
the original speed measurements. A skill does not authorize extra external work.

## Fuzzing discipline

Fuzzing is a correctness gate, not an optional cleanup. Any change that affects
parsing, type checking, term construction, rewriting, preprocessing,
clausification, SAT integration, DPLL(T) control flow, theory solvers, model
construction, generated lemmas, or solver options that alter behavior must run a
fresh fuzz gate against the current commit before it is described as complete.

Use the smallest gate that matches the change risk:

- Smoke gate: for narrow parser/printer/core rewrites with low solver impact,
  run at least 100 generated or mutated inputs locally if the generator is
  installed, plus the relevant unit tests. This gate is not sufficient for
  default-profile, theory, SAT, or preprocessing changes.
- Merge gate: for ordinary solver/theory/preprocessing/SAT behavior changes,
  run at least 1000 generated or mutated inputs against a trusted reference, or
  a stronger existing scripted gate such as `make fuzzsmt` when it covers the
  touched logic. Prefer SLURM when runtime is nontrivial.
- Campaign gate: before promoting a performance profile, online propagation
  mode, preprocessing transformation, theory solver, or SAT interaction to
  default/headline status, run at least 3000 QF_UF FuzzSMT-style cases for QF_UF
  work, and a comparable corpus for other supported logics. Also run a
  seed-mutation campaign such as YinYang/typefuzz on real benchmark seeds when
  available.

Every fuzz run must record:

- commit hash;
- command line and reference solver command;
- generator, seed, count, timeout, host or SLURM job id;
- summary counts for pass, wrong answer, crash, parser/type errors, timeout,
  unsupported, and reference disagreement;
- artifact directory containing generated failures.

Record the same information in `docs/run-log.json`. A fuzz run without a
run-log entry is not acceptable evidence for completing a solver change, even if
the raw `summary.csv` exists.

Wrong answers, crashes, parser rejections of valid generated input, invalid
models, and reference disagreements block performance work until fixed or
precisely triaged. Timeout-only fuzz results do not block correctness, but they
must be summarized separately from pass results.

Stale `bugs/` directories are not evidence either way. Before relying on an old
bug corpus, rerun each saved file against the current binary and references,
classify the outcome, and either add a regression test, document an unsupported
feature, or delete/ignore it as stale generated output. Do not report "fuzzing
passes" if unclassified old bug files are still being treated as active.

If a fuzzing dependency is missing locally or on SLURM, install it when feasible.
If it cannot be installed, report the failed install/access command as the
blocker and say exactly which gate remains incomplete.

## Core invariants

### Term invariant

- Terms are hash-consed.
- Term equality is by `TermId`.
- Every term has an explicit sort.
- Boolean formulas are terms of sort `Bool`.
- Applications are represented uniformly.
- All terms are created through the term manager.

### SAT invariant

- Primo does not implement its own SAT solver.
- SAT solving is delegated through IPASIR-UP, initially backed by CaDiCaL.
- The SAT backend sees only clauses over SAT literals.
- SMT atoms are mapped to SAT variables by the SMT layer.
- The SAT backend must not inspect theory terms.
- Assumptions are distinct from permanent clauses.
- Backend-specific details must stay behind the SAT adapter.

### Theory invariant

A theory solver receives asserted theory literals.

It may return:

- consistent
- conflict clause
- propagated literal with explanation
- model fragment

### DPLL(T) invariant

- The first implementation may use lazy theory checking.
- Theory propagation is optional until the EUF checker is correct.
- Every generated theory lemma must be independently checkable by tests.
