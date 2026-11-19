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
pending. The configured cube source links are pinned to implementation commit
`65b6f48a`, with include/test hygiene cleanup `84654ade`; the source links
now name that cleaned source commit. Writer checks: strict books,
lint and diff whitespace pass; source-link resolution is checked after that commit pin.

Captured help was refreshed from source `84654ade`, binary SHA-256
`0102030d508c15a51ee3d2f35a5ff19aa5d0183b396f65aa8d7796cfcda8d495`.
The invocation name is normalized to `build-slurm-rel/primo` and trailing
whitespace is stripped; accepted values/defaults come from that binary.

## QF_LIA cube development evidence, 8 November

A separate read-only reviewer checked the development evidence page, changed
implementation lifetime/complexity paragraphs, cube lead, literature/coverage
status and TOC against source `84654ade`, method/data `96355a2a`, raw timing
rows and replay coverage. Required CUBE-EVD-01–03 repairs clarified that
preservation excludes new instrumentation, the full matrix has four profiles,
and 3,000 generated inputs reused across four profiles produce 12,000 answer
checks. The evidence also separates 437 current-build SAT replays from 107
previous-build controls. All required findings were fixed and rechecked.
Accepted development evidence blob: `890ce8ca`; qualification metadata:
`b4c140f2`. Full-corpus results were explicitly pending and outside acceptance;
they require a separate result review. No reviewer solver execution occurred.


## Cube audit/cactus reproduction, 8 November

A separate read-only reviewer checked only the added reproduction paragraphs
and commands against the analyzer and cactus reader diff from `3209f172`.
No required findings: argument names, nested shard input, paired direction and
conditional execution after qualification agree with the scripts. Accepted
blobs: documentation `5f8a1b3a`, analyzer `fac32a5a`, cactus `7c379f3b`.
A real 200-input development replay reproduces all prior metrics unchanged;
the additional mode/reference comparisons and direct nested-layout cactus
smoke agree with the recorded rows. Strict docs, local/Git links and lint pass.
Full-corpus results remain pending and outside this acceptance.


## Full QF_LIA cube evaluation, 8 November

A separate read-only reviewer audited the full-result evidence page, lead/index
status, experiment metadata, summary/rows/cactus, first run-log entry and
reporting-only analyzer diff against baseline `22c218bb` and qualified solver
source `84654ade`. CUBE-FULL-01–04 repaired native-only validation scope, stale
development/full pending text, run-ledger status, and the M124 definition at
first use after moving full results first. All required findings were fixed
and rechecked; no outstanding findings or terminology issues remain.

The reviewer independently reproduced all 53,224 public timing rows, all
20,942 Dolmen passes and parent artifact hashes, paired/PAR2/common-runtime
metrics, identical 842 cube-model input sets, 581 direct model gains, family
concentration and reference gaps. Independent accounting parsing confirms both
1,000-task arrays completed successfully and reproduces memory peaks. The
rendered cactus matches the data. No reviewer solver execution occurred.

Accepted blobs: evidence `9cf9dbe7`, summary `26e6c81e`, compressed rows
`ddc00ef2`, figure `f1f5757c`, analyzer `11dfb599`. Acceptance covers this
full-result unit and source/data agreement, not default promotion or reader
acceptance. Writer validation covers strict books, local/Git links, lint and
rendered full-result/table/cactus inspection.


## Relative corpus roots in qualification, 8 November

A separate read-only reviewer checked the new qualification path-handling
section against the runner normalization and focused mocked-runner regression
from baseline `44bb99c1`. No findings: relative/absolute roots, corpus-relative
identities, manifest containment, duplicate rejection and the four-profile
test characterization agree with the code. Accepted blobs: documentation
`491fb502`, runner `6527f000`, regression `cab26689`. No reviewer solver
execution occurred. The single mocked regression, lint, strict docs-site and
local/Git links pass; this Python workflow fix requires no new solver binary
and does not claim a rerun of full solver qualification.


## Arithmetic efficiency and thesis mapping, 8 November

A fresh read-only reviewer checked the new efficiency evidence, thesis
checklist and rounding example against baseline `bf889d32`, profiled C++
`84654ade`, the supplied primary thesis sections and the recorded artifacts.
Required finding AE-R1 is resolved: the linear coefficient merge is distinguished
from backlink-repair searches and rational bit complexity. Optional AE-S1 is
resolved by expanding CITE on first use. No required findings remain, and the
terminology review preserves the distinction between model choices, entailed
constraints, scratch mutations and unimplemented candidates.

Accepted blobs: evidence `6c325d6c`, paper guide `f4e8659f`, summary `70afa22d`,
experiment `72233df1`, analyzer `2e4764dc`, profile batch `9537a9b4`, flattening
batch `9f7070e4`, TOC `3af4a67e`. Run-log evidence was reviewed at `85f0e841`;
its final verification field records the writer's subsequent checks. Acceptance
covers documentation/evidence/source agreement, not a general implementation
audit, default promotion or new full solver qualification. The reviewer ran
no solver or tests.

Writer verification: `make lint`, strict `make docs-site` (including
`make docs`), the local/Git link checker and the twelve-case artifact audit
pass. Rendered evidence text, repeated-cap table, rounding equations and guard
explanations were inspected. SLURM `365599` and `365629` are fully complete;
all ten successful native SAT timing rows passed separate Dolmen replay.
Initial setup job `365583` failed before any solver run and is not performance
evidence. No experiment process remains locally, and none of these jobs is
active. Reader acceptance and implementation of the proposals remain separate.

## EUF performance implementation and correctness, 18 November

A separate read-only reviewer checked the V2 selector, semantic witness budget,
query/backend lifetimes, typed statistics policy, mixed model-fact repair and
qualification prose against source `6e53a710` and the recorded artifacts.
Required EFP-1–7 and EPFE-1–7 repairs were rechecked. Acceptance covers the
correctness/method stage: 63 fresh gate receipts, 21,000 generated checks,
6,826 mutations, 318 regression measurements, 19,125 Dolmen validations and
20 focused repair replays. Resource/timeout outcomes are separated from passes.
The whole-tree scans cover 141 translation units each; their existing advisory
warnings remain documented. Native 49/382 and ASan 49 test-group totals, plus
12/12 manual examples/help checks, agree with the retained logs.

The writer built the strict documentation, assembled the site, checked local
and pinned Git links, and inspected the rendered evidence table. Captured help
comes from native source `6e53a710`, SHA-256
`8cf501e1752a4f5eeaa2909af42395af504d5ebb22dc9abaaa3ac29da276e875`;
only argv0 and trailing display whitespace are normalized. Neither this review
nor the correctness gate accepts a performance improvement. Paired timings and
their independent SAT-model validation are the next stage; defaults remain
experimental/off for conflict-selected Ackermann emission and demand naming.

## 2026-11-18 EUF mechanical/demand main timing evaluation

Read-only reviewer `conflict_ack_docs_review` accepted the main evaluation:
5,862 native rows, 2,753 exact-option Dolmen replays, all receipt hashes,
19 paired comparisons, 22 rollups, table cells, printed-counter coverage,
PAR2 censoring and reproduction instructions. EPR-1 fixed timeout rows carrying
a printed unknown; EPR-2 corrected allocation ownership and the recorded
logical-CPU affinity. Accepted artifact blobs: CSV `e36b77a7bb73`, paired analysis
`62635206efb2`, rollup `452181617c7d`, tables `d91ec396be4a`, provenance
`c1a62cad5d66`, receipts `c87db047499e`. The writer inspected rendered timing prose
and both SVG figures. This accepts the main comparison only; the 225 extra
regression measurements and their separate SAT models are still running.

Validation: `make lint`; six focused Python metadata tests; `make docs`;
`make docs-check-links` (308 pages, 47,112 local links, 1,380 unique Git links,
zero failures; 532 external URLs deferred). Native C++ remains source
`6e53a710`; its SLURM native/ASan/generated/mutation/static gates remain the
qualification basis. No new solver timing/default promotion is inferred from
near-neutral arithmetic differences.

The additional regression stage is now independently accepted by the same
read-only reviewer: 225 native rows, 75 receipt groups and all 50 exact-profile
Dolmen replays. Four stable losses recur; 20 repeated slowdowns recur; the
remaining Certora case has an additional demand timeout and is excluded from
common-solved ratios. Accepted blobs: evidence `a0427ef64d1c`, follow-up summary
`fccbf2200648`, receipts `6995124d64f9`, native CSV `6dd8189abb8e`, auditor/exporter
`f69050e86b25`. Follow-up results remain separate from the main plots/tables.
The writer reran lint, six metadata tests, documentation and local/Git links,
and inspected the rendered follow-up section. All experiment arrays are
terminal and the SLURM queue is empty; no local native experiments remain.

## 2026-11-18 full SMT-COMP UF/UFLIA follow-up methods

Independent read-only reviewer `conflict_ack_docs_review` accepted the methods,
full official 1,104 UF / 300 UFLIA selection, four undecided UFLIA cases, hashes,
paired order, 120-second/8-GiB limits and global timing/model audit barriers.
EFS-1 replaced two nonexistent CSV counter keys with the registered congruence
and BFS explanation-edge counters. The controller's whole-array wildcard repair
matches saved SLURM metadata; selected array elements remain rejected. Accepted
auditor/controller blobs were `e8a9329bef14` / `e3f035abbd42`.

Archive reuse was independently checked for all 5,616 rows: four reference
profiles covering all 1,404 unique input hashes, byte-identical selected rows,
no original decisive-answer disagreements, and correct censoring of 83 runs
beyond 120 seconds. CSV blob `eca138a98169`. Optional EFS-S1 was implemented:
the exporter checks original answers before censoring and cross-reference
agreement for expected-unknown inputs; a negative regression covers a hidden
original disagreement. The exported CSV is unchanged. Fourteen workflow tests,
lint, batch syntax/validation, qualified-binary helper preflight and documentation
checks pass. Native source remains qualified `6e53a710`; no new native/default
behavior is introduced by these workflow changes. Active performance/model
results and the subsequent whole-branch review remain outside this acceptance.

## 2026-11-19 arithmetic consistency review before merge preparation

Independent read-only reviewer `merge_arithmetic_docs_review` compared the
arithmetic implementation, lead, reference and evidence sections with `main`
`fcc274c2` and branch snapshot `1b4f9ee3`. Five required findings were repaired:

- ARD-01 distinguishes implemented opt-in ordinary integer splits from absent
  proof-derived combination splits.
- ARD-02 labels the propagation-filter checklists as historical and links the
  completed opt-in acceptance.
- ARD-03 distinguishes QF_LIA's implemented default integrality preparation
  from QF_UFLIA's default-off switches.
- ARD-04 labels the cube qualification as the historical pre-timing receipt,
  preserving its measurements and source fingerprints while linking the
  completed full timing/model summary. The original receipt SHA and Git blob
  remain recorded.
- ARD-05 repairs two source-role links to the revision containing the intended
  bound assertion and atom preparation definitions.

The reviewer rechecked every repair and found no required issue remaining in
this unit. Accepted blobs: integer chapter `ab88c9a01c0f`, propagation lead
`c4fde8f4cd83`, cube evidence `12c8c965a29d`, historical receipt `26798abfeb1d`.
All 14 hybrid qualification artifact hashes matched during the review; the
QF_LIA default-promotion record includes paired evidence, campaign fuzzing,
7,015 SAT-file model checks and its explicit TypeFuzz tool limitations.
This is source/checklist and evidence-traceability acceptance, not independent
certification of every primary-paper argument. Remote raw artifacts, the
ongoing fresh UF/UFLIA timing run and whole-branch acceptance remain outside
scope. The route-applicable override comment is a separate source hygiene
repair, also outside this documentation acceptance.

## 2026-11-19 EUF and statistics consistency review before merge preparation

Independent read-only reviewer `merge_euf_stats_docs_review` rechecked current
EUF/statistics source navigation, lead status, the Yices reference note and
historical evidence. EUF-DOC-1–4 were repaired: demand naming is implemented
and rejected as a performance default; symmetry repair is qualified; current
growth links target `59d81fee`; the old integrated timing entry records its
completed 5,862-row / 2,753-model scope. A writer regression, EUF-DOC-1a, now
accurately says callbacks create no core terms or SAT variables; owning queue
containers can allocate. Required findings are closed. Accepted blobs before
the later full-evaluation extension were lead `65e470531d9c`, Yices note
`91fadcfcdab8`, evidence `3b1e1acae535`, growth note `3bf4e326f9f5`, configuration
`53a834768cd5` and run log `f9a2da180213`.

The reviewer checked code and local paper notes; the Yices primary PDF timed
out in this fresh review. This does not recertify the original paper proof.
No new algorithm or default promotion is claimed by these documentation fixes.

## 2026-11-19 completed full SMT-COMP UF/UFLIA comparison

Read-only reviewer `conflict_ack_docs_review` independently recomputed native
coverage, all 4,212 receipt groups, all 4,020 exact-profile Dolmen records,
8,424 CSV rows, pooled/family PAR2 and RSS, paired ratios and common printed
counters. EFS-2 clarified solved/total in the family table. EFS-3 strengthened
the launcher's restart guard to expand compressed SLURM model-task ranges and
require complete canceled/unstarted coverage; negatives now cover missing,
started, duplicate and out-of-range accounting. The actual saved 4,212-element
accounting passes the stronger guard. The original audit traceback is retained.
Tail scheduling observations lack a standalone `scontrol` receipt, explicitly
limiting independent acceptance of that execution-history detail.

Native sources remain repaired candidate `59d81fee` and corrected comparator
`0647d85a`. The separate hybrid-resource repair is outside these timings and
this evidence acceptance. No reference benchmark was rerun, and this comparison
does not measure the entire branch against `main`. Whole-branch readiness and
any merge remain separate work.

Final scoped acceptance: evidence `543626bc679f`, summary `a38d18463727`, CSV
`123ad750a124`, receipts `f74e156229bf`, tables `2fd23544710b`, execution notes
`61725747551c`, SVG `1673bfa8d144`, figure provenance `341249bb9a3e`, launcher
`669740b4f09b`, plotter `dd19498c24e5`, audit batch `2ef845c04ca8` and tests
`8bb437f27f00`. Ledger entry accepted at blob `e611c92443c0` (the adjacent
running hybrid-repair entry is outside this acceptance). The writer ran sixteen
workflow/controller tests, lint, strict books and assembled local/Git checks,
and inspected the rendered result table, cactus and execution-recovery text.


## 2026-11-19 hybrid resource repair and branch review record

Independent read-only reviewer `merge_arithmetic_docs_review` accepted the new
merge-readiness page, updated limit ownership/checklist and qualification record.
It independently reran the 18-receipt gate audit: 6,000 generated inputs (5,956
passes, 44 timeouts), 2,096 mutants (2,084 passes, eleven native timeouts and one
diagnosed private node-limit unknown), and 74 regression measurements. Model
coverage is 5,512 ordinary models plus 27 scoped SAT queries and two focused
bounded-input replays: 5,541 direct Dolmen passes. The reviewer checked exact
arguments, source/native hashes, all model metadata sets, the resource replay,
141-unit static reports and the focused include-warning correction. Current
runtime differences from qualified `2d6daadc` are comments and one help literal;
the regression differs only by its direct `cstddef` include.

No required findings remain. Accepted blobs: report `6a26b4048de2`, LIA chapter
`80fbc2125573`, hybrid lead `bb9f806d7e5b`, qualification `c8a66f450fcf`, run log
`c55b439182a2`, TOC `d107df131567`. Subsequent edits to this ledger record only
acceptance and writer-check bookkeeping. The report distinguishes root source
self-review from independent documentation/evidence review; it makes no new
mathematical proof or whole-branch timing claim against `main`. Existing static
advisories remain. Main integration/publishing is outside this acceptance.

The writer runs lint, the strict books, assembled local/Git links and rendered
review/limit sections before the final commit. All 281 tracked/new batch scripts
pass syntax/heredoc validation; sixteen focused workflow/controller tests pass.
Qualified native/ASan/gate work runs only in SLURM. The exact test/help hygiene
supplement is job `434152`; the qualified application hash is preserved.


The same reviewer accepted the reproduction follow-up: all four frozen cards
match retained bytes/hashes; the displayed protocol contains executed
qualification fields only. Inherited timing fields in the preserved campaign
were unused and imply no new comparator or resource-limit claim. Updated
accepted blobs were report `adeca594b860`, qualification `cb68bfa198b7`, README
`3725d800bda0` and ledger `9c6c9005022c`. Later wording changes only replace the
vague method-input phrase with “flags, seeds and input manifests” and link the
repository execution procedure.


Final writer checks pass: `make lint`, sixteen focused workflow/controller
metadata tests, 281 batch syntax/heredoc checks, strict `make docs-check-links`
and a final incremental assembly/link check (310 pages, 47,851 local links,
1,388 unique Git links, zero failures; 536 external URLs deferred). Chromium
inspection covers the review findings/qualification and the current hybrid
ownership/limit sections; the data figure was inspected separately. The
review source remains qualified `2d6daadc` plus the documented non-behavior
hygiene changes. No new profile/default promotion or main merge is performed.


Final phase-limit recheck accepted qualification `af3698f9ccfe` and run log
`7e83823dfdb3`: tracked-seed native/reference/model commands use 120 seconds
and Dolmen 30; generated/mutation model replays use 30/30; focused replay
limits remain separate. Counts and raw card hashes are unchanged. Final local
process inspection finds no native/fuzz/batch experiment processes, and the
remote queue is empty. The documented initial static metadata preparation
failure is separate from the 26 successful qualification allocations.

## 2026-11-19 QF_LIA default update

Independent read-only reviewer `qflia_defaults_docs_review` accepts the QF_LIA
policy/reference/history sync and completed correctness/protocol/results unit.
The {download}`review record <../benchmarks/qflia-defaults-20261119/documentation-review.json>`
retains scope, identities, findings and limitations. D1–D5 repair the historical
four-switch claim, current source references, captured help, hybrid heading and
stale qualification-ledger status. The final wording attributes repeated losses
to the compared profiles without isolating history, budget or SAT handoff.

The reviewer independently checks 21 correctness receipts, 7,000 generated cases,
2,520 mutants, 84 original-seed checks, 65 tracked measurements and 7,452 gate
model/query passes. It also checks all 832 timing rows and 332 timing SAT-model
passes: 7,784 Dolmen checks in total. Tables, PAR2, conditional ratios, memory,
mechanism counters, all model-parent dependencies, physical-core topology and
the hash-matched declared-status fallback agree with raw artifacts. The four
selected losses reproduce twice; other losses retain their single-pass scope.

No required finding remains. Accepted final unit SHA-256 begins `dc3d903a5483`;
evidence page `a7833c75d0b0`, qualification `383ee1fa14e1`. Subsequent ledger edits
record acceptance and writer-check bookkeeping only. The qualified source is
`b3218ec9`; direct-include milestone `c1abfeee` reproduces both affected Release
objects byte-identically. This acceptance does not establish a mathematical
termination result, a full-corpus combined-profile win or global tuning.

The writer passes lint, eleven focused report tests, strict books/local-Git
links and rendered result/resource-table inspection. Native49/Python400/ASan49
and twelve CLI/example checks run in SLURM; static scans cover all141 C++ units.
Existing static advisories and all incomplete outcomes remain recorded.


## 2026-11-19 merge-preparation documentation consolidation

Independent read-only reviewer `merge_docs_structure_review` accepts the
25-file documentation/policy unit against `6e4f0cfd`. The unit includes current
capabilities and entry points, the research index, the evidence hub and topic
navigation, status corrections on retained investigation records, equality
implementation/evidence separation, project-work status and authoring policy.
No solver source, option defaults, executable examples, measurements or raw
experiment metadata change in this pass.

DS-01–DS-06 correct mixed research/history navigation, incomplete current-lead
coverage, stale symmetry/equality evaluation status and old LIA default claims.
DS-07 preserves renamed current-manual anchors. DS-08 removes an overbroad
qualification heading that included the still-blocked preprocessing experiment.
DS-S01–DS-S03 are addressed by topic grouping, the Project work label and moving
experimental chronology out of the equality implementation chapter. All existing
TOC pages remain exactly once; only the evidence hub and live merge checklist
are added. Original still-open numbered questions remain in the research index;
closed dispositions move to evidence. No required finding remains.

The accepted content SHA-256 is
`aba6c1b7a7918444e37e749d4536a64f7dfe11f7e692f73f24964a4613658653`,
computed over sorted path/content pairs with NUL separators. The later checklist
status, final trailing-blank-line cleanup and this ledger entry record
acceptance and writer verification only.
The reviewer checks the policy owners and retained qualification metadata,
including the qualified source/native identity and object-identical include
repairs. It does not perform a fresh whole-branch implementation acceptance,
solver/fuzz/timing campaign, raw remote receipt audit or hosted-settings check.
The historical preprocessing-bound qualification remains explicitly unresolved.

Writer checks pass: `make lint`, `make docs`, strict `make docs-check-links` and
`git diff --check`. The assembled check covers 313 pages, 49,075 local links and
1,391 unique Git links with zero failures; 542 external URLs are deferred to
Sphinx linkcheck. Navigation preserves every old page exactly once and rendered
HTML retains eight checked renamed/retained anchors. Chromium inspection covers
the research and evidence navigation, capability/default table, live checklist
and revised equality evidence/implementation sections. No new solver batch is
needed for these documentation-only changes; prior source qualification retains
its original scope. Current `main` was freshly fetched over HTTPS and remains
an ancestor at `fcc274c2`; no merge or publication is performed.

## 2026-11-19 arithmetic reading structure

Independent read-only reviewer `arithmetic_tree_review` accepts the 40-file
restructuring against `d5dd3799`. The accepted content SHA-256 is
`8d8cd212e6c59f0186d7859e33c3243fcaee3a8776d1de7c7e5776412a4e2c5c`,
computed over sorted path/content pairs with NUL separators. This subsequent
ledger entry records acceptance and writer verification only.

The [arithmetic entry point](implementation/arithmetic/index.md) groups LRA and
LIA overviews with eight focused child chapters. The former 1,434-line LRA and
1,251-line LIA pages become 195- and 294-line overview/reference pages. Existing
paths remain, with forwarding entries for moved sections; internal links lead
directly to the detailed chapters. General preprocessing and theory-combination
contracts retain their architectural owners. Research and evidence remain
separate categories, linked from the arithmetic reading guide.

R1–R3 repair the flat reading structure, duplicate propagation ownership and
rejected CITE chronology inside current preprocessing. R4 corrects an inherited
claim that hybrid is universally opt-in: QF_LIA selects it by default, while
QF_UFLIA retains private branching. R5 defines the tableau assignment at its
first use in the standalone cut derivation. R6 links the Real exact fallback to
its combination owner using the existing explicit target. S1–S4 move historical
integer-equality qualification into evidence, add arithmetic gateways, distinguish
private search from hybrid history, and label the shared propagation destination
accurately. No required finding remains. Terminology and ownership boundaries
are accepted within this unit.

Writer checks pass: `make docs`, `make docs-check-links` with both strict books,
`make lint` and diff whitespace checks. The strict assembled check covers 322
pages, 52,237 local links and 1,391 unique Git links with zero failures; 560
external URLs are deferred. A separate TOC inventory retains every previous
page exactly once and adds nine arithmetic pages. Rendered-ID comparison
preserves all 132 checked section/named anchors across LRA, LIA, preprocessing
and integer-equality integration. All twelve LRA and eleven LIA display formulas
are unchanged. Chromium inspection covers the arithmetic overview, nested LRA
and LIA navigation, tableau state and the corrected hybrid policy/explanations.

The review checks moved-text preservation, prerequisites, notation, links,
classification and source/test discoverability. It does not recertify the primary
papers, solver algorithms or the entire manual. No solver source, defaults,
executable examples or measurements change; no fresh solver/fuzz/model campaign
is claimed or needed for this documentation-only restructuring. Prior source
qualification keeps its original revision and limits.
