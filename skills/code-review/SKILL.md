---
name: smt-code-review
description: Review SMT solver code and diffs before commit for ordinary C++ hygiene, unused includes/helpers, missing or misleading comments, lint readiness, architectural violations, solver unsoundness, bad explanations, and insufficient tests. Use after implementation work, before committing solver/parser/preprocessing/build/docs changes, and whenever the user asks for review.
---

# SMT solver code review skill

When reviewing code, prioritize correctness over style, but always do an
old-fashioned hygiene pass. At Primo's current stage, review all tracked,
non-generated project files, not only the diff. The goal is to establish a clean
baseline before the codebase grows. Later, after the baseline is clean and
documented, reviews may be diff-focused with periodic whole-tree sweeps.

The old-fashioned pass catches the small problems that make later solver work
unpleasant: spurious includes, dead helpers, unclear local invariants, broad
APIs, missing comments around non-obvious algorithms, stale docs, and lint gaps.

## Review scope

The current review scope is all tracked non-generated files:

- include source, tests, scripts, docs, skills, build metadata, and benchmark
  smoke/seed files
- exclude build outputs, virtual environments, `docs/_build`, `contrib/local`,
  `contrib/downloads`, `contrib/src`, and untracked scratch data under `tmp/`
- use the diff as a focus area, not as the boundary of review

## Required commands

Run the commands that apply to the change before committing:

```bash
git diff --check
make lint
make test
make docs
```

If `clang-tidy` is available, run it over tracked non-generated `.cpp` files
with the current build directory while the repo is still establishing its
baseline:

```bash
make tidy
```

Also run the include-cleaner check as an advisory scan and inspect its output
for real spurious includes. Do not treat paired `.cpp`/`.hpp` self-includes as
automatically removable just because the `.cpp` file has no out-of-line symbols:

```bash
make include-cleaner
```

If a command is not available or not applicable, say why in the final response.

## Review order

1. Soundness
2. Invariant preservation
3. Layering violations
4. Incrementality and backtracking correctness
5. Explanation correctness
6. Model construction correctness
7. Test coverage
8. Old-fashioned C++ hygiene
9. Performance risks
10. Code clarity

## Things to look for

### Term layer

Check:

- all terms are created through the term manager
- sorts are checked at construction
- structurally equal terms share the same `TermId`
- rewrite rules preserve sorts
- pretty-printing round-trips through the parser where expected

### Borrowed storage across mutation

For each reference, pointer, iterator, span, or view into mutable storage, trace
the owner, intervening mutations, and subsequent uses. Reject a use after a
potentially invalidating operation unless the actual container/API contract
guarantees stability. Include indirect mutation through helpers, recursion,
callbacks, and aliases; `const` on a borrow does not freeze its owner.

For Primo, treat every `TermManager::mk_*` call (and any helper that can create
terms) as a potential invalidation boundary for references returned by `get`.
Copy the needed `TermId`s and scalar payloads before that boundary, then
reacquire any needed term reference by ID afterwards. Do not carry borrowed
child ranges or payload references across it. The owning manager must still
outlive all IDs. See `docs/implementation/architecture.md`, "Ownership and Lifetime".

Review expressions as well as statements: sibling function arguments need not
be evaluated left-to-right. An expression such as
`mk_ite(node.children[0], mk_times(k, node.children[1]), mk_times(k, node.children[2]))`
is unsafe when branch construction can invalidate `node`. Snapshot all needed
inputs first and construct branches in separate statements.

Apply the same reasoning to growing traversal stacks, symbol storage, and
other mutable containers, using their specific invalidation rules. Do not
assume every container mutation invalidates every kind of borrow. Extra
`reserve`, successful hash-cons hits, or a passing ASan suite are not a lifetime
proof. For a discovered invalidation bug, inspect analogous paths and require
an allocation-boundary regression under the testing skill's guidance.

### SAT layer

Check:

- clauses contain only SAT literals
- SAT solving goes through the IPASIR-UP adapter
- CaDiCaL-specific calls are isolated in the backend module
- permanent clauses and assumptions are passed to the backend with correct lifetimes
- learned or theory clauses sent to the backend are not tautological unless intentionally allowed
- assumptions are handled separately from permanent clauses
- model values are queried only after SAT result is SAT

### SMT layer

Check:

- Boolean abstraction is total and deterministic
- each SMT atom has exactly one SAT variable
- polarity is handled correctly
- assertion stack push/pop restores all relevant state
- generated theory lemmas are valid

### Theory layer

Check:

- theory literals are asserted incrementally in trail order
- backtracking restores all theory state
- conflicts include enough explanation literals
- explanations are implied by the asserted literals
- disequality conflicts in EUF are explained by equality paths

### Model generation

Check:

- every declared constant has an interpretation
- every declared function has default values
- values respect equivalence classes
- model validation script accepts produced models where possible

### Statistics hot paths

Require typed keys or numeric counters in recurring solver work, including helper
calls. Reject constructed/string-keyed instrumentation in callbacks and loops.
Check publication at resource exits, reset, stable output names and storage cost.
Review any cold/dynamic lint exception explicitly.

### Old-fashioned C++ hygiene

Check every tracked non-generated C++ file:

- includes are necessary and self-contained; remove spurious headers
- no unused local helpers, variables, aliases, structs, or dead branches remain
- helper names state the local invariant or effect clearly
- public headers expose only what callers need
- implementation-only types stay in `.cpp` anonymous namespaces
- comments explain why and invariants, not what obvious code does
- non-obvious algorithms have a short comment naming the source paper/design;
  do not rely on documentation alone when a reader needs the invariant in the
  implementation file
- error messages identify the unsupported input or violated invariant
- code follows existing ownership, lifetime, and exception style
- tests do not hide behavior behind overbuilt fixtures
- docs mention experimental/default-off status for risky options

Prefer small cleanup patches immediately after finding hygiene issues. Do not
leave known trivial cleanup for a future agent.

## Pre-commit gate

Before committing implementation work, perform a self-review using this skill.
The final response should mention either:

- "code review pass: no blocking issues found"; or
- the concrete issues fixed or intentionally deferred.

## Output format

Use this format:

1. Summary
2. Blocking issues
3. Soundness risks
4. Hygiene issues
5. Missing tests
6. Suggested patch plan
7. Non-blocking cleanup

Do not rewrite large files unless explicitly asked.
