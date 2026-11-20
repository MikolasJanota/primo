# Writing and checking documentation

Write current explanations under the appropriate implementation, guide, reference,
or contributor page. Keep project work, research leads and dated evidence separate.
Every implementation chapter needs concrete owners, operations, invariants,
limitations, an example, source links, and paper/test mappings.

## Where a change belongs

Give each claim one authoritative home:

| Question | Home |
| --- | --- |
| What works now, and with which defaults? | The implementation or reference chapter; [capabilities](../capabilities.md) provides a short linked summary. |
| Which research question is still unresolved? | [Open research questions](../leads/index.md), linked to a focused design note. State the implemented baseline and what evidence would close the remaining question. |
| What was tested, accepted, rejected or left inconclusive? | The relevant topic under [evidence](../evidence/index.md), with exact provenance in the run ledger. |
| Which review, publication or maintenance task remains? | [Project work](../work/merge-readiness.md) and the relevant work page. Do not imply every listed task is actively being worked on. |

When an investigation finishes, remove it from the current research navigation.
Preserve its results, limitations and original checklist as an investigation
record. Existing paths and anchors can remain: give the page a clear status
paragraph and place its navigation entry with the relevant evidence. New
investigation records belong under `evidence/`.

Extend an existing topic's evidence page for follow-up runs unless a distinct
method or scope needs its own explanation. Avoid a new narrative page for each
run. Keep commands, hashes, job identities and measurements in evidence or the
ledger; link them from current implementation documentation. Update navigation,
status summaries and links together. A completed implementation can still have
an open research question, but name that residual question explicitly rather
than leaving the original implementation plan in the backlog.

For a growing subsystem, group detailed chapters under its implementation
entry point. Split by the reader's question (state, feasibility, propagation,
completion or scheduling), rather than by experiment date. A shared mechanism
needs one detailed owner: integer relaxation links to shared arithmetic
propagation rather than repeating its queue/cache rules. Keep general
preprocessing and theory-combination contracts in their owning layers.
When splitting a page, update internal links to the detailed destinations,
preserve old page/fragment entry points, and check those fragments against the
previous rendered book. A successful link check alone does not inventory
unreferenced legacy anchors.

## Writing conventions

Explain what an operation does before describing its interface or limitations.
Use standard mathematical and solver terminology, with these conventions:

- Define a technical term or acronym at its first use when the reader needs
  it to understand the next step. For example, explain a strategy leaf as one
  tactic application, and expand CITE before discussing its simplification.
- Recheck first use at every chapter entry point after splitting a page. A
  definition earlier in the former combined chapter may no longer be available.
  Distinguish similarly named objects, such as groups of equal model values
  and components formed by proved equalities. Describe scheduling operations
  directly instead of introducing unexplained metaphors for them.
- Keep code identifiers exact, but describe their behavior in ordinary prose.
  Prefer “clauses generated before simplification” to an unexplained label
  such as “raw clauses.”
- Prefer established SMT and mathematical terms to project-specific labels.
  A C++ class name does not establish terminology: describe a `TheoryComponent`
  as a theory solver and link its interface. Define broader architectural uses
  explicitly, such as including quantifier reasoning under the theory solver
  interface. Check the context before replacing a term that has an established
  technical meaning.
- Use concrete verbs and objects. Write “the transformation must preserve
  satisfiability,” rather than “the transformation owes an equivalence.”
  Avoid strings of compressed modifiers where a sentence would be clearer.
- Keep warnings where they prevent a specific misunderstanding. State the
  scope of tests and experiments once in the evidence section, rather than
  repeatedly interrupting the algorithm with validation disclaimers.
- Keep architecture sections about ownership, operations and invariants.
  Put migration chronology, commit hashes, SLURM jobs, run IDs and measured
  counts on evidence pages. Link to that evidence from a clearly named section;
  readers should not need the development history to understand the design.
- Keep syntax-specific rules, such as `let` scope and definition expansion,
  in the frontend chapter. The architecture overview identifies the responsible
  layer and links to that explanation.
- Keep implementation-status summaries to one short sentence per feature,
  with links to the detailed chapters. Put limitations and open work separately;
  do not expand a summary bullet into an algorithm or experiment report.
- Distinguish terms with different meanings. In particular, a logical guard
  controls when a constraint applies; an acceptance check decides whether a
  proposed rewrite is worth performing. Do not use “guard” for both without
  explaining which is meant.


## Sources and review

Use the shared bibliography in `docs/references.bib`. Prefer the DOI URL as a
paper's primary citation link whenever a DOI exists. A publisher or author PDF
may be offered separately as a reading copy; it should not replace the DOI just
because the automated checker cannot access the publisher. For works without a
DOI, prefer a stable publisher or institutional-repository URL before a personal
webpage. Ignored local PDF names are not bibliographic identifiers. Read
relevant primary sections before claiming a mechanism is implemented, and state
the subset/deviations. Keep implementation links in the centrally configured
`source` role and historical snapshots in `record`; see the
{project}`role configuration <docs/_config.yml#L37-L44>`.
The `project` role selects the newer documentation/build-tooling snapshot.
Ordinary paragraphs should not carry repeated revision hashes. Link the
[bundled run ledger](../run-log.json) when referring to the manual's current
evidence record, rather than an older implementation snapshot of it.

Concrete source filenames in explanatory prose should be links, for example
{source}`src/smt/strategy.hpp`. Use the MyST `source` role so the centrally
configured revision supplies the GitHub target. Link the relevant definition
or operation when a whole file would make the reader search for it.

Track coverage in [the preservation audit](../coverage/index.md) and verification
separately from reader acceptance in [the review ledger](../review.md). Retain
old paths/anchors through the compatibility manifest.

## Writer and reviewer

Substantive documentation changes use a writer and a separate reviewer. The
writer supplies a bounded set of pages and their intended audience. The reviewer
reads those pages and the relevant code independently, then reports located
findings with evidence and suggested repairs. It reviews both source accuracy
and explanation: missing links, nonstandard or undefined terms, omitted mechanisms, and shifts
between architecture, algorithm, API reference and experimental history.

The writer resolves the findings and returns the revised text for re-review.
Required findings must be closed before acceptance; optional stylistic
suggestions remain distinct. If a disagreement needs unavailable evidence or
a design decision, record it explicitly. Repeating revisions without resolving
that question is not a completed review.

Keep the inspected files, patch identity, findings and dispositions, reviewer
verdict and verification in the commit or PR description. This is a scoped
technical review; it does not advance the whole manual's reader-acceptance
status. The {project}`documentation skill <skills/documentation/SKILL.md>`
provides the handoff and the
{project}`independent review checklist <skills/documentation/references/review.md>`.
Small typo and link repairs need the relevant checks without
starting another review cycle.

## Build and check

```bash
make docs
make docs-check-links
make docs-check-external
```

The {docsbuildsource}`Make targets <Makefile#L71-L100>` coordinate these commands.
The first command incrementally builds only the current manual.
Use `make docs-history` to build the frozen historical book separately. The local/Git-link target rebuilds
both books strictly, including the frozen historical book. The external checker produces
separate Sphinx linkcheck reports; distinguish genuinely broken targets from
transient timeout, rate-limit or authorization failures. Do not silently suppress
a new failure. Inspect rendered equations, tables, navigation and source links.

Run `make docs-check-external` before committing documentation changes. It is
the same command used by the
{actionsworkflow}`GitHub external-link workflow <.github/workflows/external-links.yml>`.
It checks both books and includes the strict builds and local/Git checks above.
Read each book's `_build/linkcheck/output.json` for per-URL statuses and errors.

Network results can differ between this workspace, a browser and GitHub's
runner. A timeout does not establish that a paper moved; HTTP 403/429 and TLS
certificate errors also need separate triage. Recheck the affected request when
feasible, repair confirmed bad links, and record unresolved access failures
with the command, date and execution environment. Preserve a working paper URL
unless there is evidence for a better destination. Do not bypass certificate
verification or add broad ignores to make the check pass.
The pinned Sphinx version returns nonzero for timeouts as well as broken links:
`linkcheck_report_timeouts_as_broken: false` distinguishes the reported statuses;
it does not make timeouts pass. Keep these failures visible when reporting
verification, even if local builds and source links pass.

Known access failures may be exempted by exact URL when requested by the owner.
The dated exception blocks in both book configs identify the observed error and
link to its evidence. Sphinx reports those URLs as `ignored` and makes no request
to them; a green result does not validate ignored targets. Keep both lists in
sync, retain DOI citations and TLS verification, and investigate new failures
before extending the list. Do not exempt an entire publisher or all HTTP errors.
A DOI verified to resolve to the intended paper may be retained as a durable
citation. Record the verification date and destination in the evidence record;
later automated access failures may be made advisory through an exact-URL
exception. That preserves the earlier verification without claiming current
availability. New or never-verified DOIs still need checking.
The owner may also defer a known publisher issue for an identified paper. The
20 November EasyChair exception covers only that paper's publisher page and
DOI URL, including its observed resolver 404; it does not exempt other missing
targets. Separate exact-file exceptions skip GitHub's dynamically rendered
fragment checks while still checking file availability.


Pinned Primo source commits must be reachable from the checked-out `HEAD`.
The local checker rejects commits present only in unrelated local history,
because a full CI checkout of the candidate would not include those objects.

The {project}`local checker <scripts/check_doc_links.py>` also flags unlinked concrete `src/` file paths in the main
book's prose. It excludes code blocks and the historical book from this style
check; their links are still validated. This catches filename omissions, not
unsupported claims or poor abstraction: those require the independent review.

Book builds do not execute notebooks or solver examples. Changed executable
examples need separate verification in the chosen test environment. Keep source
fixtures and expected outputs synchronized, and review a changed CLI snapshot
before replacing it. The full original {record}`authoring record <docs/development/documentation.md>`
retains logo asset, distribution and infrastructure history during migration.

## Publication and source archives

`make docs` builds the current manual in `docs/_build/html/`, and
`make docs-serve` serves that directory. Historical links require the combined
site: `make docs-site` builds both books and assembles `docs/_build/site/`.
`make docs-full` disables incremental reuse for both books and assembles the
combined site. Publication, full link checks, and `make archive` retain both books.
The {project}`site assembler <scripts/assemble_docs.py>` verifies every archived compatibility target before replacing
the previous site. Its generated redirects have ordinary-link fallbacks.
Documentation dependency installation disables pip's periodic version check,
so builds do not print pip upgrade notices.

The {project}`archive builder <scripts/create_source_archive.py>` invoked by
`make archive` packages tracked source, editable books and the assembled HTML,
with a source revision and SHA-256 checksum. It requires a clean checkout;
`--allow-dirty` is only for diagnostic snapshots. Reading PDFs and cached tools
are excluded. The archive rebuilds without Git history; source-link validation
still requires a checkout containing the pinned commits. See the root
`THIRD_PARTY.md` before distributing dependencies or benchmark material.

The {actionsworkflow}`CI workflow <.github/workflows/ci.yml>` builds documentation
previews on pushes to `main` and `docs/public-repository-migration`, on pull
requests, and on manual runs. It uses read-only repository access, including
while the repository is private. Download the `documentation-preview` artifact
from its `docs` job.
The separate {actionsworkflow}`Publish documentation workflow <.github/workflows/pages.yml>`
skips its build while the repository is private. Pages publication is restricted
to public `main` and the `github-pages` environment;
configure required approval there before enabling deployment. The site is served
under `/primo/`, including the historical book at `/primo/archive/original/`.
{actionsworkflow}`External-link checks <.github/workflows/external-links.yml>`
are a separate scheduled/manual workflow so publisher rate limits do not obscure
deterministic documentation failures.

The workflows pin reviewed action releases by full commit SHA and use Node.js 24
for JavaScript actions, including the Pages uploader's nested upload action.
The workflows use GitHub-hosted `ubuntu-24.04` runners. A self-hosted replacement
must support Node.js 24; [checkout documents runner version 2.327.1 or later](https://github.com/actions/checkout/blob/3d3c42e5aac5ba805825da76410c181273ba90b1/README.md#checkout-v5).
The action runtime is separate from Python 3.12, which runs the documentation
tools. Pages packaging explicitly includes generated dotfiles such as
`.nojekyll`; newer Pages upload actions otherwise exclude them by default.
