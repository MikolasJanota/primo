# Documentation closing review

**Status: 28 October consistency review covers all 97 current-book Markdown
pages. The earlier technical findings are closed; reader acceptance remains separate.**

The replacement manual is now the main book. `make docs-serve` previews the
current book at `http://localhost:8000`; `make docs-site` assembles both books. Publication
is a separate operation; changing the source tree does not deploy a website.

## Coverage and review record

The {download}`28 October consistency record <coverage/pre-publication-review-20261028.json>`
lists all 97 pages at baseline `e397439`, their fingerprints, assigned review
units, findings and dispositions. It also covers the root README, contribution
policy, security policy and pull-request template. The pass checked current prose against selected
owning code, commands, policies and evidence, and removed duplicated lead
summaries from the landing page. Historical results retain their measured
revisions; this review does not rerun solver qualification, re-prove the cited
papers, or independently verify GitHub settings.

The [2 October page-by-page review](coverage/page-review.md) covered all 84 pages
at its own baseline revision, recorded the corrections and listed source findings. The {ref}`numeric-payload omission in symmetry comparison
<symmetry-payloads>` was confirmed to produce false UNSAT answers, then repaired
and qualified on SLURM with regressions, differential fuzzing and separate model
checks. The six supporting-tool and resource-control findings were subsequently
[repaired and qualified](evidence/publication-followups.md).

The [preservation audit](coverage/index.md) accounts for 91 old pages, 1,150
section headings, 126 supporting entries, and 1,306 original URL targets.
The old book is retained as editable source in `docs-history/` and is built
under <a href="archive/original/index.html">archive/original</a>. It preserves the final
old book, including later corrections; all targets from the original
compatibility snapshot were checked against that book before the move.

```{csv-table}
:file: review.csv
:header-rows: 1
:widths: 18, 12, 14, 28, 28
```

The ledger records technical inspections at their original revisions. Its
stages do not automatically advance after a successful build. Research leads
remain proposals; accepting their descriptions does not approve implementation.
The [earlier page-by-page review record](review-history.md)
retains the detailed chronology and literature-reading notes.

(closing-corrections)=
## Earlier closing corrections, 2 October

- The frontend chapter now distinguishes core terms, theory literals, and SAT
  literals/clauses. Parser nodes do not cross these solver interfaces.
- The model guide distinguishes the rebuild engine from the assumptions engine:
  the latter constructs a model for `--check-model`; SMT-LIB `get-model` still
  requires `:produce-models true`.
- The statistics reference documents session counters and the incremental
  combined model-check/printing timer.
- The dependency introduction includes GMP.
- Current guides use the main book paths. Shared prose, examples, assets and
  bibliography are available without the former draft tree.

## Technical verification

The [publication qualification record](evidence/publication.md) and
[follow-up record](evidence/publication-followups.md) describe their SLURM checks,
bounded fuzzing and model validation at the recorded revisions. Both books build strictly;
local targets, pinned source links and historical routes are checked together.
The earlier browser inspection covered the project URL prefix, equations, navigation,
favicon and an archived route. External-host access failures remain explicit
in the link report. Technical checks do not substitute for reader acceptance.

## Acceptance review

Read the introduction/capability map, user guides, implementation chapters,
contributor procedures and evidence boundaries as one candidate. Check that
examples explain the observable behavior, mathematical notation is introduced,
source links identify the owning code, and paper citations distinguish the
implemented subset from future work. Inspect equations, figures, navigation,
search, old fragment routing, and the project-site URL prefix in the rendered
site. Record the reader's acceptance explicitly after that review.

[Publication preparation](work/publication.md) records verification and external
requirements. [Evidence work](work/evidence.md) retains unresolved historical
mappings and quantifier-checking limits. No full-corpus performance claim follows
from the documentation build or the publication qualification sample.

## Equality-delivery ablation documentation, 7 November

The independent read-only review accepted the implemented design and the
methods/options/help unit against solver source `4a0c6bee` and methods
`16ad8c01`. Required findings D1/D2 repaired source navigation and distinguished
enabled scan modes from off; D3 replaced a premature qualification reference
with a link to the separate run ledger. The accepted ablation-page blob is
`3ab3e65c77648e0feb0ce741ea92ea682eca6315`; configuration, options and help blobs
are `22d9b9306afb9b48a19835ff827f227e5a7b4097`,
`226f2d612c8895bd0aba93bc5eaeef609aeb7b81`, and
`861184aeae04e5877d369b9636d65b2429081358`. No required findings remain in
that unit. This acceptance does not cover qualification outcomes or timing
results. Both books and the assembled site's local/Git links passed; the changed
design/table was inspected in rendered HTML. Native gates and experiments are
tracked in `run-log.json`, independently of documentation acceptance.
