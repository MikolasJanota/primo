---
name: smt-documentation
description: Write code-grounded architecture, algorithm, invariant, representation, paper-traceability, and user documentation for the SMT solver. Use when documentation should explain concrete implementation details such as term storage, ownership, memory management, hash-consing, SAT/theory interfaces, or how papers inform the code.
---

# SMT solver documentation skill

## Goal

Document the solver so that future code changes preserve the architecture, representations, paper-grounded design choices, and invariants.

Use classic Jupyter Book, matching the `llm2smt` `lra` branch documentation setup:

- author normal docs in MyST Markdown
- maintain `docs/_config.yml`, `docs/_toc.yml`, and `docs/index.md`
- use notebooks only for executable experiments and benchmark reports
- keep the documentation buildable with `make docs`
- keep `scripts/build_docs.sh` as the canonical build wrapper

## Documentation types

Keep current behavior, unresolved questions, completed evidence, and project
work distinct. Follow the placement rules in `docs/development/documentation.md`.
Close completed investigations in the research index; preserve their stable
paths as clearly labeled records under evidence navigation. Prefer extending an
existing topic's evidence page over adding one page per run. Current default
claims belong to the implementation/reference owner, with linked summaries.

### Architecture docs

Explain:

- module boundaries
- ownership and lifetime of data structures
- memory management strategy
- value identity versus object identity
- mutability and aliasing rules
- hash-consing or interning strategy
- stable and unstable identifiers
- major invariants
- extension points

Architecture docs must be code-grounded. Before writing or revising an architecture page:

1. read the relevant headers and implementations
2. cite the concrete local types, methods, and files being described
3. describe what is implemented now, not the intended future design
4. include known limitations and unsupported paths

### Algorithm docs

Explain:

- Boolean abstraction
- clausification
- DPLL(T)
- IPASIR-UP/CaDiCaL SAT backend integration
- congruence closure
- model generation
- push/pop
- explanation generation

Algorithm docs must connect mechanisms to representations. For each algorithmic component, include:

- concrete state owned by the component
- input and output types
- invariants before and after each public operation
- how errors, unsupported input, and unknown results are represented
- complexity expectations when they are known or inferable
- where the implementation intentionally diverges from the paper

### User docs

Explain:

- supported SMT-LIB fragment
- build instructions
- run instructions
- options
- examples
- known limitations

### Developer docs

Explain:

- how to add a theory
- how to add an SMT-LIB construct
- how to add a regression test
- how to run fuzzing
- how to run benchmarks
- how to build the Jupyter Book documentation

## Documentation gate

### Independent writer–reviewer loop

For substantive new text, rewrites, or documentation reviews, use two roles:
the writer owns edits, and a separate agent reviews without editing. Isolated
typos and link repairs need only the applicable checks. Repository `AGENTS.md`
authorizes this bounded delegation; it does not authorize solver experiments
or publishing changes.

1. Define a small review unit: pages/sections, intended reader, and the question
   each section should answer. Read the relevant code before drafting.
2. Give the reviewer a fresh context (`fork_turns="none"` when available), the
   user's requirements, file paths, the baseline revision, and
   [the review checklist](references/review.md). Let it inspect the draft and
   sources directly; do not supply the writer's preferred verdict or rationale
   before its first assessment. It may work alongside independent writer work,
   but freeze the review unit while it is being checked.
3. The reviewer returns located findings with stable IDs, separating required
   repairs from suggestions. The writer fixes each required issue or responds
   with code/paper evidence explaining a disagreement. Neither silently drops
   a finding nor treats personal preference as a correctness defect.
4. Return the revised files and finding dispositions to the same reviewer.
   It checks the fixes, surrounding prose, and any new claims. Repeat until
   no required findings remain. After two rounds on the same unresolved issue,
   identify the missing evidence or actual design choice; do not continue
   cosmetic rewrites or declare success because a round limit was reached.
5. Run `make docs`, the local/Git link checker, and inspect the changed rendered
   sections. Re-review semantic edits made after acceptance. Record reviewed
   files, baseline and final patch identity, findings/dispositions, the reviewer's
   verdict, validation, and unchecked scope in the commit or PR description.
   Independent acceptance applies only to that material, not the whole manual
   or the user's final reader acceptance.

Use `python3 scripts/check_doc_links.py docs/_build/site` after an incremental
build; use `make docs-check-links` for a full strict build and link check.
Before committing a change that touches documentation, run
`make docs-check-external`. This is the same target used by
`.github/workflows/external-links.yml`; it includes both strict books, assembled
local/Git links and Sphinx's external-link check for each book. An incremental
HTML build or local/Git-only check does not replace it. Inspect the two
`_build/linkcheck/output.json` reports. Classify failed requests by their actual
status and diagnostics; a timeout, HTTP 403/429 or certificate failure alone
does not prove that a paper URL is obsolete. Recheck the affected request when
feasible and record unresolved access failures explicitly. Do not disable TLS
verification, add blanket ignore patterns or silently make the check advisory
to obtain a green result. Local success does not guarantee access from GitHub's
runner, and a browser download need not behave like the automated checker.
Owner-requested exceptions for confirmed access failures may use exact URL
patterns in both book configs, with a reason, review date and evidence record.
Keep the lists synchronized. Sphinx skips listed URLs completely and reports
`ignored`; do not describe them as verified or suppress new failures by status
or publisher. Keep unlisted missing-target errors active. An owner-requested
policy permits treating later automated access failures for a previously
verified DOI as advisory: record when it resolved to the intended paper and
the destination, then use an exact-URL exception in both configs. Preserve
that verification as historical evidence, not proof of current availability;
new or never-verified DOIs still need checking. An owner-requested
deferral for a known publication issue may cover that paper's exact publisher
and DOI URLs; record it separately from general access failures.


Missing reviewer tools or primary evidence must be reported explicitly. Continue
authorized, useful work, but do not label an incomplete review as passed.

Do not treat an implementation task as complete until the documentation says how the implemented representation works.

For any change to parser, terms, symbols, SAT integration, clausification, theory solving, options, or solver control flow:

1. identify the nearest documentation page before editing code
2. update that page with the implemented C++ types, ownership, invariants, and limitations
3. explain how paper-backed ideas are used, partially used, or deferred
4. run `make docs`
5. commit documentation with the implementation or in an adjacent commit

If the implementation intentionally omits documentation, state that explicitly in the final answer as a remaining blocker.

## Required Representation Sections

When documenting a core subsystem, include a "Representation" section. Answer these questions explicitly when relevant:

- What are the primary C++ types and where are they defined?
- Who owns the memory?
- Are objects moved, copied, interned, or referenced by numeric ids?
- Which ids or references remain stable after insertion?
- What is hash-consed, canonicalized, or deduplicated?
- What equality notion is used: syntactic identity, semantic equality, SAT literal equality, or congruence equality?
- Which operations may allocate new state?
- Which operations are pure queries?
- What invalid states are prevented by constructors or checked dynamically?

For term documentation specifically, cover:

- term node storage
- `TermId` stability
- hash-consing keys
- sort checking
- Boolean simplification
- interaction with symbol declarations
- how applications, equalities, and Boolean connectives are represented

## Paper Traceability

Prefer DOI URLs as primary paper citation links whenever a DOI exists. Keep
publisher/author PDFs as optional reading copies. A bot's HTTP 403, rate limit
or timeout does not justify replacing a DOI with a personal webpage. For a work
without a DOI, prefer a stable publisher or institutional repository before a
personal page.

Every page that mentions a paper-backed component must include a "Paper Traceability" or "Sources and Implementation Status" section:

- list the relevant papers or notes
- state which ideas are implemented now
- state which ideas are only planned or deliberately deferred
- map each implemented idea to the local module or type where it appears
- avoid implying that a paper was fully implemented when only a small interface or stub exists

Use cautious wording:

- "Primo currently uses X from paper Y in module Z."
- "Paper Y motivates future work on X; Primo does not yet implement that algorithm."
- "This is an implementation inference from the paper, not a direct claim from the paper."

## Style

Use precise technical prose.
Prefer small examples.
Prefer concrete type and file references over generic descriptions.
Avoid marketing summaries and vague architecture prose.

Every nontrivial algorithm doc should include:

- input
- output
- representation
- invariants
- ownership and lifetime
- failure modes
- one worked example
- sources and implementation status

## Output format

When documenting a feature:

1. identify what changed
2. update the nearest relevant doc
3. read the relevant source files and document the actual representation
4. add or update paper traceability if papers motivate the design
5. add examples if useful
6. check that docs do not claim unsupported features
7. run `make docs` when documentation tooling is available
