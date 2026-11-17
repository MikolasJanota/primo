# Conflict-selected Ackermann pilot

This study builds and evaluates a bounded opt-in prototype of the
conflict-selection idea in Dutertre and de Moura (2006), §2 p. 3
{cite:p}`dutertre2006yices`. It exposes a full congruence clause to SAT when
that congruence step appears in an online EUF conflict explanation. It requires
existing equality atoms; it never creates missing atoms. The
[design checklist](../leads/conflict-ackermann-prototype.md) states the
representation, ownership, proof obligations and exclusions.

The candidate native source is `13230b6d`, SHA256
`39b346b31f830e7c79aeeb3829e9cd469e2ee9399830a26756b4a2d412f6af7e`.
Both options default off. This is a development pilot; it supplies no default
promotion or representative solver-ranking claim.

## Frozen comparison

The {download}`protocol <../../benchmarks/conflict-ackermann-20261117/protocol.json>`
and {download}`selection <../../benchmarks/conflict-ackermann-20261117/selection.json>`
were frozen before candidate corpus timing. Each logic has forty inputs:
twenty family-round-robin priority cases and twenty deterministic hash-ordered
family-round-robin fills. A family is the first path component. UF priorities
use historical native/fast-reference gaps; UFLIA priorities use printed
congruence-explanation activity from the prior arithmetic combination study.
That activity includes propagation explanations and does not establish
conflict-only opportunities. This target enrichment prevents global conclusions.

| Profile | Native source | Additional option |
| --- | --- | --- |
| BASE | Frozen `4f02a8b7`, production-equivalent to pre-change `fca39be2` | None |
| OFF | `13230b6d` | None |
| DIAG | `13230b6d` | `--euf-conflict-ackermann-diagnostics` |
| ON | `13230b6d` | `--euf-conflict-ackermann` |

All use `--quiet --stats`, existing defaults, one-sided explanations and
private integer branching. No preprocessing/row/hybrid option is changed.
Each input gets three paired repetitions, four profiles per allocation,
30 seconds per native process and an 8 GiB address-space limit. Profile order
rotates/reverses by input and repetition. Three repetitions do not fully balance
four positions. Allocations request one CPU and 10 GiB SLURM memory; recorded native
affinity contains two logical CPUs per allocation. Replication
arrays run sequentially; profiling and correctness jobs finish before timings.
This gives 960 native rows over 240 allocations. Unknown and timeout rows are
unsolved; PAR2 charges each unsolved row 60 seconds.

One selected Certora input has unknown expected status; historical references
also timed out. Its expected answer is not invented. Paired/cross-repetition
answer agreement is checked, and every native SAT timing row must be replayed
separately through Dolmen. Validation uses 180 seconds for native replay and
60 seconds for Dolmen, outside the speed measurements.

## Correctness qualification

The {download}`qualification summary <../../benchmarks/conflict-ackermann-20261117/qualification.json>`
contains receipt hashes. Six exact logic/profile tasks cover QF_UF and QF_UFLIA
with OFF, DIAG and ON:

| Stage | Coverage | Result |
| --- | ---: | --- |
| Generated differential checks against Z3 | 6,000 | 5,999 pass; one existing integer-node-limit unknown |
| Tracked regression measurements | 87 | Passed, including scoped query/model checks |
| Real-seed original measurements | 72 | Passed |
| Real-seed mutated inputs | 1,951 | 1,935 pass; 16 native recheck timeouts |
| Independent SAT-model checks across these stages | 5,416 | Direct Dolmen pass |

Generated seeds start at `2026121700`; mutation seeds at `2026121800`, plus
task slot. The repaired build restarts qualification with these seeds; initial
`427f5618` gates are superseded. UF uses FuzzSMT; UFLIA uses the repository's
mixed depth-three generator. Each task mutates twelve real seeds through
YinYang 0.3.0 with thirty iterations per seed, a 600-second campaign limit and
five-second mutation solver limits. Rechecks allow ten seconds. Campaign output
counts can differ; at least one hundred mutants per task are required. Native
SAT inputs are replayed with `--check-model` and independently checked by Dolmen
0.10. Generated, original-seed and mutant replays use 30 seconds native /
30 seconds Dolmen; tracked regression and scoped checks use 120 seconds native /
30 seconds Dolmen. Z3 is 5.1.0. The
{download}`tool receipt <../../benchmarks/conflict-ackermann-20261117/tools.json>`
records versions and available binary hashes.

No wrong answer, crash, invalid model or unexplained reference disagreement was
observed. Resource-incomplete results remain separate from passes. The generated
unknown was replayed at thirty seconds and diagnosed as the existing integer
branch-and-bound node limit; no mutant unknown required another diagnostic replay.
These gates exercise the exact three profiles at default explanation/combination
settings, not every option combination.

The helper built the native through SLURM. Repaired `make check` passed forty-nine
native groups and 369 Python tests; ASan passed all forty-nine groups. Three new
pure workflow tests and `make lint` also pass. The
{download}`static coverage record <../../benchmarks/conflict-ackermann-20261117/static-coverage.json>`
keeps the original whole-tree failure visible: 141 translation units were
attempted, with a Clang initialization error fixed in `13230b6d`. The two changed
bodies and `main.cpp`, which consumes the generated revision header, were
rescanned successfully. The other 138 units have unchanged owned inputs.
This composed coverage is not a fresh successful whole-tree aggregate, and
baseline advisory warnings remain. The new selector has no rescan finding.

## Bounded instruction profiles

The {download}`profile summary <../../benchmarks/conflict-ackermann-20261117/profiles.json>`
records OFF/DIAG Callgrind 3.22.0 captures on `PEQ/PEQ011_size8.smt2` and
`mathsat/EufLaArithmetic/hard/hard14.smt2`. Each returns the expected resource
unknown at one hundred reported theory conflicts. The sixty-second profiling
deadline was not reached after disabling remote debug-symbol downloads.
Earlier attempts stalled with those downloads enabled and are excluded.
These are capped diagnostic executions, not successful full-query timings.

On the UF capture, preprocessing accounts for 54.24% of OFF's inclusive
instruction count, including symmetry breaking at 26.74%; the recorded
`explain_equal_into` entry accounts for 6.54%. On the UFLIA capture,
model reconciliation accounts for 34.95%, and class merging for 6.59%.
Inclusive costs overlap. DIAG increases total instructions by about 0.69%
and 1.33%, respectively, in these single capped pairs. These numbers do not
establish a whole-query runtime overhead.

The UF DIAG capture visits 18,438 congruence steps in one hundred conflict
explanations, retains 3,897 prefix entries and catalogs 717 semantic pairs.
It reports 716 missing-result events and 340 missing-argument occurrences;
forty-two traces are truncated. The UFLIA capture has only three traced EUF
conflicts and three semantic pairs by the mixed-theory hundred-conflict cap;
all three lack a result equality. Thus historical congruence-explanation
activity alone can overestimate the opportunities available to this selector.
Missing-event counts are overlapping outcomes, not a partition of pairs.

## Paired timing result

All 960 native rows agree on the same solved inputs in every profile and
repetition: 34/40 QF_UF and 28/40 QF_UFLIA. There are 216 timed-out rows,
no normal resource unknowns, and no errors or answer disagreements.
All **348 successful native SAT timing rows** pass separate direct Dolmen
validation. The
{download}`full audit <../../benchmarks/conflict-ackermann-20261117/full/analysis.json>`
checks all 240 allocation receipts, exact options/input hashes and model
coverage. The
{download}`measurement CSV <../../benchmarks/conflict-ackermann-20261117/full/measurements.csv>`
retains every repetition, runtime, RSS and available mechanism counter.
Family summaries in the audit use the first path component; solved input
sets are unchanged within every family as well.

| Logic | Profile | Solved in each repetition | Mean PAR2 seconds, repetitions 0 / 1 / 2 |
| --- | --- | ---: | --- |
| QF_UF | BASE | 34 | 11.049 / 11.072 / 11.072 |
| QF_UF | OFF | 34 | 11.058 / 11.067 / 11.067 |
| QF_UF | DIAG | 34 | 11.117 / 11.110 / 11.109 |
| QF_UF | ON | 34 | 11.115 / 11.112 / 11.121 |
| QF_UFLIA | BASE | 28 | 21.129 / 21.145 / 21.130 |
| QF_UFLIA | OFF | 28 | 21.139 / 21.138 / 21.120 |
| QF_UFLIA | DIAG | 28 | 21.142 / 21.128 / 21.136 |
| QF_UFLIA | ON | 28 | 21.146 / 21.143 / 21.131 |

For each input solved in all three repetitions by both compared profiles,
first take the median of its three paired runtime ratios. The geometric mean
over inputs with OFF median runtime at least one second is 1.0290 for ON/OFF
on UF (fourteen inputs) and 1.0078 on UFLIA (twelve inputs). DIAG/OFF gives
1.0318 and 1.0078. ON/DIAG is approximately 1.0014 and 1.0011, respectively.
The feature shows no speedup here; most observed UF cost is already present in
the diagnostic profile. OFF/BASE ratios are 1.0005 and 0.9996 on the analogous
baseline-qualified sets. No common solved input meets the auditor's follow-up
rule of being at least 10% and 0.5 seconds slower in all three pairs.
These small selected-set differences are descriptive, not significance tests.

Across all three repetitions, median/max process RSS in KiB is 20,352/195,560
for UF OFF and 20,690/198,120 for UF ON; UFLIA OFF gives 16,086/691,300 and ON
16,078/696,844. RSS is measured per process, including unsuccessful rows.
Matched successful-row counters show no meaningful reduction in search:
UF SAT conflicts are 230,622 OFF versus 230,625 ON, with 590,667 versus 590,685
reported theory conflicts. UFLIA has identical 73,572 SAT conflicts and
232,368 theory conflicts. These are sums over three repetitions on the same
34 UF / 28 UFLIA common solved inputs.

### Mechanism coverage and limits

On successful UF ON rows, the selector records 590,685 conflict explanations,
121,364,325 congruence-step visits, 22,071,933 retained prefix entries and
138,723 semantic-pair entries. It constructs and delivers only thirty clauses
(sixty literals), five per repetition on each of two small QG inputs:
`QG-classification/qg5/dead_dnd044.smt2` and
`QG-classification/qg5/iso_icl089.smt2`. SAT-conflict counts do not improve
materially. Missing-result events number 138,681 and missing-argument
occurrences 105,759; these overlap. Counts span repeated solves and, where
applicable, rebuilt catalogues, so they are not globally distinct pairs.

The bounded policy also reaches its engineering limits: successful UF rows
have 236,319 truncated traces and 7,575,603 catalogue-limit exits; peak catalogue
size is 4,096 and peak pending queue is one. A full catalogue can exclude later
opportunities even if they would have all required atoms. Coverage therefore
measures this bounded prefix/catalogue, not all conflicts or all potential
Ackermann clauses.

Two additional QG inputs time out in every profile while ON delivers 37 and
21 clauses, respectively, in each printed timeout snapshot. These 174 delivered
clauses across six unfinished runs are kept separate from the thirty clauses
on successful rows. All eighteen UF incomplete rows per profile print some statistics.
For UFLIA, printed snapshots are available on 22 OFF, 24 DIAG and 23 ON of the
36 incomplete rows per profile; unprinted counters remain unavailable.
No printed UFLIA row has a ready or sent clause. Successful UFLIA ON rows have
39,240 traced EUF conflicts and 50,298 catalogue entries, all with missing-result
events; peak catalogue size is 363. The broader combination search's conflicts
are not all EUF conflict-explanation opportunities.

### Decision

The bounded existing-atom policy is retained as an explicitly experimental,
default-off prototype and diagnostic aid. It is not adopted as a performance
profile: it adds modest UF overhead without solving another input or reducing
search materially in this pilot. This result does not reject conflict-selected
Ackermannization with fresh atoms. It identifies missing result equalities and
bounded selection saturation as concrete limitations to address in a separate
extension, with safe atom registration and new correctness/evaluation gates.
No budget tuning, atom-growth implementation or default promotion is performed
in this pass.

## Reproduction and artifacts

Source/checklist: {conflictacksource}`selector <src/smt/conflict_ackermann.cpp>`,
{conflictacksource}`trace and callback owners <src/smt/solver_engines.cpp>`.
The {download}`run ledger <../run-log.json>` records provenance. Workflows live in
`scripts/qualification/conflict_ackermann/`, with immutable shared timing/model
runners under `scripts/qualification/lia_hybrid/` and
`scripts/qualification/arithmetic_combinations/`. `configure.py` recreates all 242 private timing cards from a qualification
campaign and the frozen selection; byte-for-byte reproduction was checked.
Private SSH configuration,
cluster workspace mappings, hostnames and job identifiers remain outside tracked
files; raw artifacts are under `results/conflict-ackermann-20261116/`.

## Performance code review and full-search follow-up

This review examines the implementation at `a67acd37`; its native solver
remains `13230b6d`. It changes no solver behavior or default. The existing-atom
pilot is not a test of the complete Yices policy: the 2006 paper's §2 describes
both dynamic literal creation and conflict-selected congruence clauses. Missing
atoms and the bounded selection policy are explicit Primo restrictions, not
requirements attributed to Yices. This review uses the paper and Primo code;
no other solver's implementation was read or copied.

### Full-search samples

The earlier hundred-conflict Callgrind captures cannot identify the dominant
cost of a complete query. In the full PEQ011_size8 OFF timing at repetition
zero, printed preprocessing time is 95,586 microseconds against 13.490735
seconds process time, about 0.71%. The capped profile's 54.24% inclusive
preprocessing instruction share concerns a different execution prefix and
quantity. It should not direct whole-query optimization toward preprocessing.

The {download}`full-search stack inventory <../../benchmarks/conflict-ackermann-20261117/review/full-search-samples.json>`
contains eight completed UNSAT GDB/MI captures: PEQ011_size8 and SEQ026_size7,
OFF/ON, two repetitions with reversed profile order. The native is the same
qualified binary. Sampling uses jittered 0.05-second intervals, a ninety-second
profiler deadline per query, one requested CPU and 10 GiB allocation memory;
there is no native address-space cap under GDB. All eight complete normally.
These are 3,242 wall-stack observations, not CPU events or fresh native timings.
The {conflictacksource}`sampler <scripts/profile_gdb_samples.py>`,
{download}`workload <../../scripts/qualification/conflict_ackermann/review_profile.sbatch>`
and {download}`auditor <../../scripts/qualification/conflict_ackermann/review_audit.py>`
record the command, input identity and output hashes.
A group counts each stack at most once; groups overlap, inlining can hide
frames, and stacks are limited to forty-one frames.

| Input | Profile | Samples | Closure-update group | Explanation group | Selector group |
| --- | --- | ---: | ---: | ---: | ---: |
| PEQ011_size8 | OFF | 527 | 71.73% | 13.09% | 0.00% |
| PEQ011_size8 | ON | 542 | 71.40% | 15.50% | 0.92% |
| SEQ026_size7 | OFF | 1,071 | 81.61% | 38.38% | 0.00% |
| SEQ026_size7 | ON | 1,102 | 80.76% | 39.75% | 1.45% |

Closure-update means a `propagate` or `merge_classes` frame; explanation means
an explanation or `append_edge_reason` frame; selector means a selector method
or semantic-pair extraction frame. Explanations can be nested inside closure
updates, and both inside the SAT callback stack. Their percentages must not be
added. Frequently sampled top frames include `process_use_list`,
`merge_classes`, `explain_equal_into` and `restore_trail_entry`. This points to
closure maintenance and explanation work as the larger follow-up investigation;
it does not attribute their inclusive cost to one allocation or prove a Yices
implementation comparison.

An initial workload guard assumed quoted MI target output; native GDB instead
printed a plain `unsat` line. Both initial tasks failed that reporting assertion
after their first query completed normally. Both were rerun in a fresh output
directory with a guard accepting either transport. Only the final eight
complete captures enter this inventory. No SAT model is produced in these UNSAT
profiling runs, and no new solver/fuzz correctness claim is made.

### Located findings and proposed checks

**CAR-1 — baseline performance: avoid full disequality-list work for an empty
losing list.** In {conflictacksource}`merge_classes
<src/theory/equality/flat_congruence_closure.cpp#L522-L547>`, every nonconflicting
merge constructs a union vector and merges both sorted lists. With active
checkpoints it also snapshots both lists for rollback. An empty union need
not allocate. If the losing class has no disequalities and the surviving
class has D entries, the live list is unchanged, yet the operation copies and
scans O(D) data. Repeating k singleton merges into that class can cost O(kD)
for this bookkeeping alone. Add the no-op fast path, then consider reversible
moves for empty-destination cases. A focused probe should attach D unrelated
disequalities to one class, merge k disequality-free terms into it, verify
nested rollback/conflict explanations, and count copied entries. These are
proposed repairs, not implemented speedups. The samples identify the containing
function as hot, not this individual copy's measured share.

**CAR-2 — baseline performance: retain the existing use-list buffer for undo.**
{conflictacksource}`process_use_list
<src/theory/equality/flat_congruence_closure.cpp#L550-L561>` copies the losing
use list into `flat_vector_trail_`, swaps its original buffer into a local
vector and then destroys that original buffer. This adds a full allocation/copy
for each checkpointed nonempty list. Consider moving the original buffer into
the undo payload and iterating the saved payload by a stable index. First prove
that iteration cannot cross an invalidating outer-vector mutation; current
`enqueue_congruence` only queues work, but a future helper could change that.
A high-fanout merge/pop probe should verify identical notifications, signatures
and explanations while distinguishing copied from moved list entries.

**CAR-3 — opt-in overhead: use fixed counters in the hot selector.**
{conflictacksource}`ConflictAckermannSelector::count
<src/smt/conflict_ackermann.cpp#L20-L22>` constructs a fresh concatenated
`std::string` and uses a hashed string-key lookup for every event. Successful
UF ON rows contain 8,723,238 duplicate-pair events and 7,575,603 catalogue-limit
events across the pilot's three repetitions. These long, repeatedly constructed
keys add avoidable allocation/hashing work even when statistics printing is off.
Use the existing dense `StatKey` path or explicit numeric counters with a safe
flush boundary. Check identical counters, resource-return snapshots and query
reset behavior; measure actual allocation reduction before attributing a share
of the observed three-percent overhead to this change.

**CAR-4 — opt-in overhead: reuse conflict scratch storage.**
{conflictacksource}`check_disequality
<src/theory/equality/flat_congruence_closure.cpp#L686-L689>` assigns an empty
trace aggregate before each conflict explanation, and
{conflictacksource}`pop <src/theory/equality/flat_congruence_closure.cpp#L143-L146>`
move-assigns its checkpoint trace. This discards trace-vector capacity on common
conflict/backtrack paths. In addition,
{conflictacksource}`conflict_application_pairs
<src/smt/solver_engines.cpp#L1498-L1515>` grows a fresh semantic-pair vector.
Keep reusable storage or a bounded representation while retaining the existing
ownership/rollback invariant, including push/pop while already inconsistent.
A repeated conflict/pop probe should show allocations stabilizing after warm-up;
ASan and the existing nested-conflict test remain required.

**CAR-5 — opt-in overhead: do not construct a doomed emitting clause.**
{conflictacksource}`consider <src/smt/conflict_ackermann.cpp#L66-L88>` continues
through all argument equalities and can allocate a clause after discovering
that the result equality has no SAT literal. Successful UF ON rows have 138,681
missing-result events among 138,723 catalogue entries; every successful UFLIA
entry has a missing result. A cheap emitting path could return at that point,
with full overlapping missing-argument diagnostics retained only in an explicitly
detailed diagnostic path. Such a change must deliberately update the documented
counter contract; silently dropping diagnostics is not an accepted repair.
Test that an absent result literal performs no clause allocation in the cheap
path, while a subsequent registration can still make the pair eligible.

**CAR-6 — selection-policy limitation: misses consume the useful-pair budget.**
{conflictacksource}`consider <src/smt/conflict_ackermann.cpp#L34-L49>` inserts
first-seen pairs before testing eligibility. After 4,096 missing pairs fill the
catalogue, a new pair is rejected before checking whether every required atom
exists. Later conflicts can therefore lose all new opportunities while repeatedly
paying the failed-admission cost. This is a documented bounded-policy limitation,
not unsoundness or a violation of the previous prototype scope. Separate a
bounded evictable negative cache from admitted/emitted-pair deduplication, or
compare another explicit bounded admission policy. Extend the current saturation
test with a fully eligible pair arriving after the missing pairs. Similarly,
the raw sixty-four-step prefix is filled before semantic/prefix filtering and
pair deduplication; a useful later witness can be excluded. Evaluate eligibility
coverage and processing cost together rather than simply increasing both caps.

### Review disposition

No new soundness defect was identified in clause construction, signed equality
literals, n-ary disjunctions, stable term ownership, conflict-first delivery or
backtracking of the reviewed feature. Performance findings remain unimplemented
and separately testable. Prior native/ASan/fuzz/model qualification still covers
the unchanged solver; this follow-up adds sampling and static review only.

The next small implementation candidates are CAR-1/2 for the hot default EUF
core and CAR-3/4 for the opt-in overhead. Their payoff is not yet measured.
Missing-atom registration is a separate algorithm/integration milestone needed
to evaluate a less restricted conflict-selected policy. Neither this pilot nor
two sampled problems establish why current Yices is fast in general, or that
adding these repairs would close its performance gap.
