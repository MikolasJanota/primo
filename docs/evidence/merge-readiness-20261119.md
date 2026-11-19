# Branch review before merging with main

This review compares `lia-integrality` with `main` at `fcc274c2`. It covers the
branch's solver changes, defaults, tests, active qualification workflows and
supporting documentation. It does not merge or push either branch. Recheck the
base and any conflicts if `main` changes before integration.

The implementation review is a self-review using the repository code-review
and efficiency-review checklists. Separate read-only reviewers checked the
arithmetic/EUF documentation and the completed SMT-COMP evidence. Their
acceptance applies to those units, not an independent audit of every solver
line or a new mathematical proof.

## Findings and repairs

The fresh review found one implementation defect: the optional hybrid
split-atom budget restarted when model guidance recursively entered complete
arrangement. A one-atom cap admitted two bound atoms; the peak also omitted
the still-live first backend. The public
{hybridfallbacksource}`component driver <src/smt/component_solver.cpp>` now owns
one consumption counter shared by internal fallback. Each backend retains its
own bound-to-literal registry; a bound allocated in a new SAT space consumes
another unit. Budget exhaustion remains a resource-limit `unknown`, and
completion without further allocation remains possible at the cap. A new
public driver invocation gets a fresh budget; this does not introduce a budget
shared across separate strategy subgoals.

The {hybridfallbacksource}`UFLIA regression <tests/unit/qf_uflia_solver_tests.cpp>`
uses a bounded SAT instance with both arithmetic and UF constraints. It covers
online/offline execution, direct complete arrangement, guided fallback, caps
one/two and unlimited completion. Separate CLI replays show caps one/two stop
at exactly one/two allocations; cap four and unlimited both produce checked
SAT models with four allocations and peak four. The original measurements and
new ownership checklist remain in the [hybrid lead](../leads/lia-hybrid-branching.md).

The active benchmark restart guard also needed stronger model-task accounting:
it now expands compressed SLURM ranges and rejects missing, started, duplicate
or out-of-range old tasks. All 4,212 saved canceled tasks pass that stronger
check. Nine stale arithmetic/EUF documentation claims were repaired, including
implemented ordinary splits, current demand naming, route defaults and completed
historical evaluations. Review records and finding IDs are in
[the documentation ledger](../review.md).

Hygiene fixes add the missing direct `cstddef` include to the new test, clarify
route-supported overrides and global-tuning status, and put row-limit comments
beside their fields. CLI diagnostics now say “unconditional” rather than
“root-level.” These follow-up changes alter comments, one help description and
a test include; solver behavior remains the qualified `2d6daadc` implementation.

## Source and invariant coverage

| Area | Review focus and result |
| --- | --- |
| Integer preparation and settings | Exact inward rounding, primitive rows and integral slack obligations; route-resolved settings copied by value; explicit switches and strategy identities checked. |
| Private/hybrid completion | Exact negative/infinitesimal floors, history ownership, budget arithmetic, restoration, private-premise discharge and retained SAT split reasons. Ordinary growth adds no tableau row or arithmetic variable; cuts remain in private working copies. |
| Cubes and patching | Candidate-only shortcuts validate the original bounds and all integer obligations; failure never proves UNSAT. Exact lattice arithmetic owns its values. |
| Bound preprocessing and propagation | Only unconditional premises; sign/strictness/rounding checked. Row memo invalidates on backtrack/growth and tracks trail/basis changes; signed queues own their explanations. |
| SAT/component coordination | Detached growth, no borrowed term/atom storage across registration, pending splits block model acceptance, failed assumptions preserve opposite choices, resource/cancel exits precede optional growth. |
| EUF and models | Use-list/checkpoint lifetimes, bounded conflict witness selection, semantic equality growth and complete-fact export checked. Symmetry composition and stale-vocabulary repairs retain their separate qualification. |
| Statistics | Typed keys in recurring solver work; alias/order/storage lifetimes checked. The cold-boundary inventory is a guard, not proof that every helper is cold. |
| Tests and workflows | Arithmetic boundaries, scoped assertions, both SAT polarities, model reconstruction and allocation growth checked. Active answer/status/PAR2/model auditors reviewed; historical experiment records remain tied to their frozen sources. |

The whole-tree checks include all tracked non-generated C++ translation units,
recursive Python lint, formatting, batch syntax/heredoc validation and strict
books/local/Git links. Historical campaigns were checked through their retained
qualification and status records; this review does not rerun every old campaign.

## Fresh qualification

The {download}`qualification record <../../benchmarks/merge-readiness-20261119/qualification.json>`
binds source `2d6daadc`, native SHA-256, exact flags/seeds, 18 correctness receipts,
model coverage, tool reports and job IDs. The
{download}`reproduction instructions <../../benchmarks/merge-readiness-20261119/README.md>`
and frozen cards retain exact flags, seeds and input manifests.
Inherited timing fields in the campaign card were not executed in this
qualification and do not name its comparator or resource limits.
The native application was built
through the preparation helper with `--build-via-slurm` (job `434124`).

- `make check`: 49 native groups and 393 Python tests; `make asan-test`: 49
  native groups. Verification array `434125` completed successfully.
- Six fresh 1,000-case profiles: LIA private/hybrid, UFLIA private/hybrid
  online/hybrid offline, and UF default. There are 5,956 passes and 44 timeouts.
- Real-seed checks: 2,096 mutants, 2,084 passes, eleven native timeouts and one
  diagnosed integer branch-node limit. Replay `434150` confirms the exact
  resource diagnostic and budget counter; Z3 returns SAT. It remains unsolved.
- Seventy-four tracked regression measurements pass, including scoped sessions.
  Every selected successful SAT model/query passes Dolmen: 5,539 across these
  gates, plus two focused bounded-input replays, for 5,541 total.

There are no wrong answers, crashes, invalid models or unexplained disagreements
in this qualification scope. Timeouts and the resource-limited unknown are
incomplete outcomes, not correctness passes. Gate arrays are `434128`–`434130`;
focused replay is `434133`.

Both whole-tree static scans cover 141/141 translation units without tool,
missing-input or unused-include failures (`434131`). They record 881 tidy and
408 include-cleaner advisory signatures. The only added signature relative to
`59d81fee` was the test's missing `cstddef` include; it is fixed. Supplemental
job `434152` compiles/runs the current test under Release and ASan, checks its
analyzer/include-cleaner output against the existing baseline, and compiles
and inspects the revised help. The qualified native application remains
byte-identical. Existing advisory findings remain; this is not a warning-free
claim. The first static preparation failed on archived log paths omitted by
the workspace helper; the corrected inventory retains all 141 source units.

## Defaults and performance scope

Private branching remains the default. Hybrid branching, cubes, unconditional
bound preprocessing, row propagation, UFLIA tableau-equality export and conflict
Ackermann emission/demand remain opt-in. QF_LIA's four previously qualified
integrality switches remain enabled; QF_UFLIA keeps them disabled. This review
promotes no new performance profile. Global parameter tuning remains future work.

The completed [SMT-COMP comparison](euf-performance-fixes.md#completed-120-second-comparison)
measures `59d81fee` against corrected comparator `0647d85a`, with default private
branching. Both solve 1,363/1,404 cases; UF has a modest mechanical gain and
UFLIA is neutral. All 8,424 timing rows and 4,020 separate SAT-model replays pass.
The four reference solvers are archived context; no reference benchmark was
rerun. These timings exclude the later optional resource repair and do not
constitute a whole-branch performance comparison against `main`.

No further blocking issue was found in the implementation review. The retained
advisories, opt-in status, partial Griggio policy and its lack of a termination
claim remain explicit limitations. A merge with `main` is a subsequent action.
