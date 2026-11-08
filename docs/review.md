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

## Completed equality-delivery evidence review, 7 November

A separate read-only reviewer accepted the full/selected ablation evidence,
metadata, CSVs, SVG and changed implementation/lead sections against native
source `4a0c6bee` and analysis methods `cb65253d`. It independently checked
3,295 full timing rows, family/PAR2/paired results, 1,983 direct Dolmen passes,
reference coverage including the fresh cvc5 gap check, frozen-source counter
controls, CSV hashes, input identities and pinned qualification workflows.
Required findings E1–E4 corrected aggregate-counter/trace wording, timer scope,
missing command provenance and stale planned-status text; S1 refreshed the
canonical ledger. No required findings remain.

Accepted blobs are evidence page `07d2d88c4db2bbb0fe68e3dd96fe4dec9f7bbcc7`,
propagation page `5ddd726c7835e69d853bd5533e4e7250bb476e78`, full metadata
`02539786dbc967aef57d31557697484f61748548`, and qualification metadata
`fbee45d0c8478ba760703408f11bf0fb048a3cf8`. Other reviewed page/CSV/SVG blobs
were unchanged during the final recheck. The writer checked strict books,
assembled local/Git links, the rendered introduction/selected table, full
result/family tables in the browser's print rendering, and the cactus SVG.
Live scheduler/process checks belong to the writer, not the independent review.
Reader acceptance and default promotion are separate; default off remains.

## QF_LIA cube-test plan, 8 November

The user narrowed the next integer-arithmetic target from Dillig's general
proof-derived splitting to the cube test described in Bjørner–Nachmanson,
*CAV 2024*, §4.2. A separate read-only reviewer checked the plan against that
primary section, the current arithmetic owners and the existing cube reading
note. Required finding CUBE-R1 added the M124 definition/exact flags and
corpus/manifest provenance. No required findings remain. Accepted plan blob:
`10ea31bf810f38f0152dffa8ffa30c8e3ba3bd07`; source baseline `71a784e6`, whose
C++/tests match the linked qualified `4a0c6bee` sources. Other accepted blobs:
lead index `b58711d768e4bc44b905720563490c0f02472a85`, LIA chapter
`9814c38849d84b66e40d839c915b71d7c4a11c15`, Dillig note
`91509da522ffb121e37efc9cf075a50b05eee35e`, and TOC
`6d2f433c69e7f18b83931286f543cf39d6ab4c85`.

This accepts a documented design, not an implementation or performance claim.
Strict books/local-Git links, lint and rendered-plan inspection are writer
checks. No solver benchmarks or implementation qualification were run for
this documentation-only change; the plan defines those future SLURM gates.


## Optional QF_LIA cube implementation, 8 November

A separate read-only reviewer checked the changed cube section of the LIA
chapter, cube lead/checklist, lead index, Bromberger guide and option reference
against the numerical helper, completion/tableau, SMT preparation/settings,
CLI and tests at implementation baseline `aeaf2823`. CUBE-DOC-01–05 required
source navigation, sorting complexity, aggregate-skip terminology, current pure
integer reconstruction status and correct result/helper ownership. All were
fixed and rechecked; no required findings remain. The subsequent optional
wording fix says extra tableaux may be allocated, since patching can finish first.
The acceptance covers source/prose agreement only; runtime qualification is
pending. The configured cube source links will be pinned to the implementation
commit in the adjacent qualification record. Writer checks: strict books,
lint and diff whitespace pass; source-link resolution awaits that commit pin.
