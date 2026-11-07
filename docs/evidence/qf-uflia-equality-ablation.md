# Why eager equality sharing can lose

The original all-rule integration combines three effects: scanning the feasible
tableau, naming additional interface equalities with full arithmetic bridges,
and asserting guarded proof clauses. The controls in this investigation
separate those effects. They preserve the input formula and the existing
integer-completion/fallback mechanisms. The
[implemented policies](../implementation/uflia-equality-ablation.md) describe
ownership, soundness and finite progress; defaults remain off.

The completed 659-input comparison finds that fixed-value-only delivery solves
580 inputs versus 575 off and 567 with all rules. Its PAR2 sum is 4.46% lower
than off, but it trades 15 gains for 10 losses and slows common solves. The
original all-rule bundle therefore hid a useful narrower policy; this is not
a universal improvement or a default promotion.

The qualified solver is `4a0c6bee`, Release/CaDiCaL
`cadical-3.0.0-7b99c07`. Its C++ source has not changed during this experiment.
The frozen comparison solver is qualified `8d84fdca`. Native arithmetic
settings and `--arithmetic-model-phase=off` are explicit in timing commands.
{download}`Qualification metadata <../../benchmarks/qf-uflia-equality-ablation-qualification-20261107.json>`
and the {download}`run ledger <../run-log.json>` record provenance and limits.

## Controls and selected cases

The 61-case selection contains every native gain/loss from the previous
[659-input comparison](qf-uflia-equality-sharing.md), ten slow common cases,
family controls and four profiled cases, deduplicated. It is deliberately
biased toward explaining changes, rather than estimating a corpus-wide win.
The {download}`selection metadata <../../benchmarks/qf-uflia-equality-ablation-selected-20261107.json>`
records each input SHA and reason.

At 60 seconds, the selected comparison gives:

| Policy | Solved / 61 | Gains / losses versus off | PAR2 seconds |
| --- | ---: | ---: | ---: |
| Off | 44 | — | 2315.1 |
| Scan only | 44 | 0 / 0 | 2319.8 |
| Names and bridges only | 43 | 16 / 17 | 2573.3 |
| Fixed-value proofs only | 55 | 15 / 4 | 1053.1 |
| All proofs | 36 | 17 / 25 | 3311.0 |

There are 427 timing rows, including old-off and old-all controls. All 302
successful answers agree with the prior fresh Z3 run on SHA-identical inputs;
all 168 successful SAT rows pass separate direct Dolmen replay. Old/current off
have identical non-time search counters on all 44 common solves; old/current
all agree on all 36 common solves. Off/scan likewise agree on all 44 common
solves, excluding equality-diagnostic and time counters. These controls do not
compare equal-length prefixes of timeout runs.

Scanning alone has a common-solve geometric mean runtime ratio of 1.055 versus
off on the selected cases lasting at least 50 ms in either arm. Names-only is
8.69, fixed-only 2.76 and all 37.20 on their respective common-solve sets. Those
sets differ and omit gains/losses; the ratios cannot replace PAR2 or be
compared as if they used the same inputs.

## What the ablation isolates

Scanning and owning explanation translation leave measured non-time search
counters unchanged on the completed controls. Most of the large regression therefore occurs after
delivery changes solver state. The names-only control keeps complete equality
semantics and omits optional proof clauses; it still reproduces substantial
slowdowns. Naming and arithmetic encoding alone are sufficient to cause some
regressions. They are not sufficient to explain every case.

Two selected examples separate the effects (seconds):

| Input | Off | Names only | Fixed only | All |
| --- | ---: | ---: | ---: | ---: |
| `wisas/xs_19_29.smt2` | 0.27 | 35.97 | 2.39 | 25.71 |
| `mathsat/Wisa/xs-14-16-4-1-1-5.smt2` | 0.17 | 0.28 | 0.15 | 15.24 |

The first shows a severe naming-only regression; the second shows a regression
that naming alone does not reproduce. This is a sufficiency test, not an exact
cost subtraction: after delivery, the arms may take different SAT/tableau
trajectories and emit different batches. The full bridge encoding remains
necessary for the existing driver's semantics; this experiment does not justify
omitting it.

The fixed-only policy retains the existing `FixedEq` rule: two shared variables
are fixed to the same value by explaining bounds. It still scans all four rules
and filters output; its advantage is not a cheaper discovery algorithm. It
retains all 13 selected MathSAT solved gains and avoids most selected Wisas
losses. It is not uniformly better: the selected Certora case ending
`39657_2866defdd1f2434b69ab_47_QF_UFLIA.smt2` takes 28.11 seconds off and times
out with fixed-only; `wisas/xs_29_49.smt2` is another fixed-only loss.

## Integer limits amplify the search change

An integer-completion resource limit causes the model-guided driver to retry
with a complete arrangement. This is distinct from the model-decision-budget
fallback that explained the previous MathSAT baseline loss. In the 61-case run,
all sharing reports 21 resource-limit fallbacks, names-only 13, and off/fixed
none. Missing timeout statistics remain missing. In addition, the fallback counter
increases only after its complete retry returns, so a timeout inside that retry
can still report zero. These are observed reports, not a count of every
resource limit reached.

For `mathsat/Wisa/xs-14-16-4-1-1-5.smt2`, off needs 62 materialized pairs and
543 LIA atoms. All sharing ends with all 2,628 pairs, 5,623 LIA atoms and 11,145
SAT variables after its resource-limit fallback. Equality scanning costs about
11.6 ms; integer completion accounts for 13.20 seconds, versus 3.68 ms off.
The component-growth timer reports about 27.7 ms, but excludes part of
materialization: bridge/term construction is not separately timed. The
13.20-second integer-completion measurement supports downstream integer search
and fallback as the dominant measured cost; scanning itself is small. Timers nest
and must not be added indiscriminately; some counters describe the complete
retry's state rather than cumulative distinct objects across both attempts.

For `wisas/xs_37_57.smt2`, off finishes in 1.70 seconds, names/all time out, and
fixed-only finishes in 4.32 seconds. Fixed-only avoids the timeout while still
costing more than off. The earlier [native profiling](qf-uflia-equality-sharing.md)
independently locates enabled-run costs in arithmetic/SAT work, including
integer completion and denser pivots.

## Correctness and limits

The fresh gates cover 7,000 generated cases: 6,998 reference matches and two
off-mode integer-node-limit unknowns. Both unknowns reproduce on the frozen
baseline. Fixed-only has 3,000 reference matches across three seeds, plus a
100-case real-seed mutation campaign. Across the modes, all 3,548 successful
generated SAT rows pass direct Dolmen. Forty original seed/regression checks
pass; 398/400 mutations pass at 10 seconds, and both timeout files return SAT
at 60 seconds. All 222 successful original/mutation/retry SAT rows pass direct
Dolmen. Unknowns and timeouts are not counted as correctness passes.

`make check` passes 45 native suites, 311 Python tests and lint. `make asan-test`
passes 45 native suites; another 150 generated ASan cases pass. Whole-tree
clang-tidy/analyzer/include-cleaner finishes on 133 tracked C++ translation
units. Existing advisories remain; the only added per-file diagnostic
occurrences are the new policy enum-size advisory and the changed existing
options-padding advisory. This is not a warning-free baseline. No new memory,
include or dead-code diagnostic appears in that comparison.

The gate metadata supplies seeds, binary/reference hashes, commands, limits,
job IDs, failures and artifact locations. Separate model-replay work does not
enter timing results. All solver execution is on SLURM compute nodes.

## Full-corpus comparison

All 659 inputs run under each of the five policies at 60 seconds, giving 3,295
Primo timing rows. The {download}`complete metadata
<../../benchmarks/qf-uflia-equality-ablation-20261107.json>` records input
hashes, binaries, commands, family results, paired gains/losses and committed
CSV hashes. Consolidated CSV metadata contains all timing rows and the prior
659-row Z3 reference, so the plot can be regenerated without rerunning solvers.

| Policy | Solved / 659 | SAT | UNSAT | Timeouts | PAR2 seconds |
| --- | ---: | ---: | ---: | ---: | ---: |
| Off | 575 | 405 | 170 | 84 | 11,368.82 |
| Scan only | 575 | 405 | 170 | 84 | 11,388.82 |
| Names and bridges only | 568 | 388 | 180 | 91 | 13,334.04 |
| Fixed-value proofs only | 580 | 397 | 183 | 79 | 10,861.73 |
| All proofs | 567 | 388 | 179 | 92 | 13,184.32 |

Only zero-exit SAT/UNSAT answers count as solved. PAR2 adds 120 seconds per
unsolved input; no errors, wrong answers, unsupported inputs or clean unknowns
occur. Off/scan have identical measured non-time search counters on all 575
common solves. Scan adds roughly 5.1% geometric mean overhead on 471 common
solves lasting at least 50 ms in either arm, without changing solved sets.

| Family | Off | Names only | Fixed only | All |
| --- | ---: | ---: | ---: | ---: |
| Certora (76) | 6 | 8 | 5 | 8 |
| TwoSquares (21) | 21 | 21 | 21 | 21 |
| MathSAT (454) | 441 | 454 | 454 | 454 |
| Wisas (108) | 107 | 85 | 100 | 84 |

Naming alone retains all 13 MathSAT gains. Optional guarded proof clauses are
not necessary for those gains under this control. Naming alone also reproduces
most Wisas losses. Thus the useful and harmful effects both include early
interface naming, rather than dividing cleanly into useful proofs and expensive
scanning. The arithmetic facts remain valid; delivery changes the SAT encoding,
the arithmetic problem representation and subsequent search.

Across the full corpus, off reports 14 resource-limit fallbacks, names-only 78,
fixed-only 5, and all 72, subject to the reporting caveat above. This supports
the selected-case diagnosis: the broad delivery policies frequently lead into
expensive integer search and complete-arrangement retries.

Fixed-only still loses seven Wisas solves and one net Certora solve against
off. Its paired comparison has 15 gains and 10 losses. On the 461 common
solves lasting at least 50 ms in either arm, its geometric runtime ratio is
1.242 and median ratio 1.042. The PAR2 improvement therefore accompanies a
meaningful common-solve cost and SAT regressions; it is not a generally faster
replacement.

All 2,865 successful Primo answers have independent reference agreement:
2,863 match the prior fresh Z3 run on identical inputs. One Certora input that
Z3 previously timed out on supplies the remaining two SAT rows (names/all);
a fresh cvc5 1.4.2 run answers SAT in 2.86 seconds. All 1,983 successful SAT
rows pass separate direct Dolmen replay with the exact timed binary and flags,
120-second solver and 30-second validator budgets. There are no invalid models,
changed answers, replay timeouts, unsupported cases or missing rows.

The frozen-source controls also agree on every non-time counter across 575
commonly solved off cases and 566 commonly solved all cases. Off preserves the
previous solved set. All still solves 567, but two near-limit cases swap:
Certora case 59 now finishes in 58.43 seconds after a previous timeout, while
`wisas/xs_20_40.smt2` now times out after previously finishing in 59.34 seconds.
This illustrates why one full run at the default SAT seed is insufficient for
a stable ranking of close profiles.

```{figure} ../_static/qf-uflia-equality-ablation-full60-20261107.svg
:alt: Cactus comparison of five equality-delivery policies and the prior Z3 reference on all 659 inputs.

Full corpus, no filtering. Z3 is the prior reference run on SHA-identical inputs;
it was not retimed with the new policies. Counts and PAR2 use the complete set.
```

## Decision and next leads

Keep sharing off by default and retain the explicit diagnostic policies. Fixed
only is a qualified experimental alternative, rather than grounds to promote
all four rules. The investigation separates a useful inference subset from
harmful delivery/search effects, but does not identify a universal dynamic
trigger.

The next delivery experiments should measure how many fresh names are useful
to EUF, whether already registered equalities can be handled first, and whether
proof/encoding growth can be limited while retaining existing fallback
semantics. These are hypotheses, not implemented optimizations. The
[active-equality lead](../leads/arithmetic-equality-exploitation.md) tracks them.
Any design that avoids arithmetic bridges needs a replacement correctness
argument for both equality polarities; this result does not authorize a
bridge-free shortcut. The existing discovery rules already emit spanning
representatives within fixed-value and matching-row groups, so replacing a
quadratic all-pairs scan is not the missing optimization here.
