# Public launch preparation

This is the October 2026 launch record and owner runbook. Its audit counts and
reported GitHub settings describe that preparation, not a verification of the
current branch or hosted configuration. The [merge checklist](merge-readiness.md)
tracks the later arithmetic branch; recheck publication settings on the approved
revision before launch.

**Status: symmetry soundness bug and all six technical review follow-ups repaired
and qualified; owner acceptance and final GitHub publication checks remain pending.**

The first launch publishes the repository and manual as a research snapshot.
A new numbered release, results-repository/SQLite migration, and solver research
leads are separate work. Primo-owned material retains MIT. New contributions
require DCO sign-off; the historical record will not be rewritten to add it.
Repository visibility has not been changed.

## Completion record

| Milestone | State |
| --- | --- |
| Documentation correction and replacement | Current book in `docs/`, historical book in `docs-history/`; strict builds, local/source links and 1,306 historical targets checked. |
| Editable source archives and dependency notices | Prepared and tested, including documentation rebuild without Git and a SLURM source-archive build. Dependency notices travel with rendered HTML. |
| Public CI and Pages workflow | Prepared and actionlint-checked; owner reports a downloadable CI documentation artifact. Pages deployment remains pending; record the approved revision and run URLs. |
| Correctness qualification | [Fresh follow-up gate recorded](../evidence/publication-followups.md): 8,000 generated inputs/sessions, 212 real-seed mutants, 800 bounded-budget replays, independent ground-model checks, release and ASan tests. Resource-incomplete outcomes are reported separately. |
| Page-by-page source review | {ref}`Symmetry payload finding <symmetry-payloads>` reproduced and repaired; fresh SLURM tests, fuzzing and model checks passed. All six supporting-tool/resource-control findings are also repaired and qualified. |
| Local Git publication audit | {download}`Refreshed 27 October <../coverage/secret-audit-20261027.json>` at `7bdfa8b`: no secret detections across the tracked tree, historical file contents and Git metadata. The [October 2 report](../coverage/publication-audit.json) remains historical. Hosted assets remain unaudited. |
| Consolidated reader acceptance | Pending owner review of the [assembled manual](../review.md). |
| Institutional/contributor publication authorization | Pending owner resolution. |
| Imported benchmark publication terms | Resolved for the [19 inventoried imports](../coverage/third-party-inputs.json): byte-identical members of SMT-LIB release 2025.08.04, whose metadata declares CC BY 4.0; attribution recorded. |
| GitHub configuration and public visibility | Owner reports DCO installed and required with `build-and-test` and `docs`; verify enforcement and remaining settings through the [handoff](github-handoff.md). Visibility was reported private in October. |

## Remaining publication work

The 27 October local secret audit covers 1,700 reachable commits and 8,211 complete
historical file contents, including freshly fetched tips of all 36 remote branches
and two annotated tags. Four Gitleaks 8.30.1 scans report zero detections: Git patches, the
1,240 tracked working-tree files, complete historical file contents, and Git
messages/ref names/tag annotations. Explicit default rules, an empty ignore file
and disabled inline allow comments prevent project-specific suppressions.
The filename inventory finds no credential/SSH files, reading PDFs or archives.
These checks do not establish publication rights or cover hosted Actions logs,
artifacts, release assets or issue/PR attachments. The raw local evidence is in
`results/secret-audit-20261027/`; the downloadable report records exact scope,
scanner and input fingerprints, commands and the patch-count limitation.

The [technical correctness findings](../coverage/page-review.md#correctness-follow-ups)
are closed with regression and qualification evidence. This does not resolve
the remaining publication decisions below.

The owner must resolve institutional/contributor authorization, accept the manual,
and review hosted
attachments, release assets and Actions logs before changing visibility. Remote
Git branches and tags are already covered by the refreshed local audit.
The prepared workflows then need to run on the approved revision. Verify
required checks and DCO, and complete the protected Pages environment and private
vulnerability reporting using the handoff. Verify the public clone and deployed site after
publication. No new numbered release is needed for this launch.

During that preparation SSH Git access worked, but GitHub CLI/API authentication
was unavailable. Repository settings, hosted artifacts, workflow execution and
deployment were therefore not independently verified here. The owner can perform those steps with
an authenticated browser or CLI; credentials need not enter this workspace.

The empty-dependency-cache SLURM build timed out downloading CLI11. Builds with
provisioned dependencies and the Git-free source archive passed, but fresh
bootstrap remains unverified until GitHub-hosted CI or another connected
allocation runs it. External-link checks retain four current-book and 13
historical-book access failures in [their report](../coverage/external-links.json).
They are separate from passing local/source-link checks.

## Benchmark license verification, 7 October 2026

The [SMT-LIB release record](https://zenodo.org/records/16740866) and its
[API metadata](https://zenodo.org/api/records/16740866) declare CC BY 4.0.
All 19 inventoried imports match 18 distinct release members byte-for-byte,
including the renamed online-reason-cache regression. The QF_UF, QF_LRA, and
QF_UFLRA archive checksums match the published release checksums. The
[inventory](../coverage/third-party-inputs.json) records exact members, hashes,
and reproduction instructions; the
{download}`benchmark notice <../../LICENSES/SMT-LIB.md>` supplies collection and
individual attribution, the license link, and the unchanged-content statement.
This closes the missing-license finding for those files. They remain available
to tests and experiments, under CC BY 4.0 rather than Primo's MIT license.
The separate institutional/contributor and hosted-content reviews remain open.

## Reproducing qualification

{project}`public_launch.sbatch <scripts/qualification/public_launch.sbatch>`
covers QF_UF, QF_LRA, QF_LIA,
QF_UFLRA, QF_UFLIA, quantified UF and the QF_UF/QF_LRA assumptions engines.
Each route has four 250-input shards. Tests/ASan, static analysis, archive
rebuilding and real-seed OpFuzz mutations have separate job recipes in the same
directory. Use the
{project}`workspace-preparation helper <scripts/slurm_prepare_benchmark_workspace.py>`
with `--build-via-slurm` and its built-in preflight before solver jobs.
The owner runs solver batches on SLURM; contributors may use any suitable
machine and must supply the required fuzzing evidence.

{project}`summarize_public_launch.py <scripts/qualification/summarize_public_launch.py>` rejects missing or failed
shards, mixed binaries, skipped SAT-model checks and overlapping session seed
ranges. It reports distinct input hashes separately: independent seeds can still
produce the same small template. Timeouts remain incomplete results. See the
qualification record and run ledger for the exact selected attempts, commands,
resource limits and limitations. These samples make no full-corpus performance
claim; unresolved historical paper-table mappings remain [evidence work](evidence.md).
