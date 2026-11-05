# Diversifying the DPLL(T) search

**Status: Open lead. Owner: unassigned.** Nothing here is implemented.

The current CLI names are `--initial-theory-phase` and
`--arithmetic-model-phase=on|off|auto`. The older spellings in the dated
investigations below remain compatibility aliases; see
[phase option names](../reference/options.md#phase-option-names-and-compatibility).

## The problem

On some inputs the run time depends on which part of the search space the
first decisions commit to, and Primo has no mechanism that reliably leaves a
bad region. The clearest recorded case is the QF_UFLIA `wisas` family under
`--lia-primitive-int-rows` ({ref}`integrality evaluation
<integrality-evaluation>`):

- The first decisions push a specifier-count slack $t$ to the top of its
  range.
- When that value is fractional, integer completion rejects it quickly and
  the search settles on a feasible value.
- When it is integral, the solver stays there. Refuting it needs a counting
  argument that the DPLL(T) loop learns one increment pattern at a time.
- The region is hard for the four solvers tried. Forcing $t\ge14$ in
  `xs_28_48.smt2` makes Primo and z3 time out at a 30 s limit, and cvc5 and
  yices at a 25 s limit (tested at $t\ge14$ and $t\ge17$ only). The
  unconstrained file is SAT in 0.3 s by default.
- Forcing model-guided phases (`--lra-model-phase`) makes the default run time
  out on the same file. So the default also succeeds by search order, not by
  a mechanism that would find its way out.

The {ref}`four-profile follow-up <qf-uflia-m124-review>` at `5dec036`
confirms that removing integer slack repair obligations does not recover the
30 lost `wisas` files. On every lost file, each M2-enabled profile reports
that all recorded completion calls started integral and scored zero patch
candidates. These are partial statistics from timed-out runs, not new traces
of the chosen slack values. They place the next investigation in the search
that follows integral completion rather than in the patch candidate rule.

The CaDiCaL restarts within the 30 s, 62,873-conflict `xs_28_48` run under
the option apparently do not leave
the region; restart counts were not recorded. A possible reason is that
restarts keep saved phases,
so the same assignment is rebuilt. The stuck state is also made of decisions
the arithmetic theory and the model-based combination keep re-proposing,
which SAT-level phase heuristics may not reach. Both points are hypotheses to
check in a trace; neither is established.

## Alternatives to restarting: source audit on 2026-11-05

The unlucky search explanation does not determine the remedy. There are two
questions: can we avoid the difficult integral commitment, and can we make
reasoning within it less expensive? The following are hypotheses to test,
not implemented improvements or evidence of a speedup. The audit used
`b0215bcc`; solver source is unchanged from the measured `5dec036` binary.

### Steer arithmetic decisions before the value is pinned

The historical traces settle near the smallest feasible integer with options
off and near the largest with M2 (primitive integer rows). A candidate
heuristic is to prefer existing
bound literals compatible with a small-magnitude feasible integer target,
then let normal backtracking explore alternatives. This changes decisions,
not the asserted problem: neither `t = 12` nor `t <= 13` may be installed as
a permanent restriction without a proof. The timeouts under `t >= 14` do
not prove that region unsatisfiable.

The intervention has to occur before SAT's threshold choices fix the slack.
A model mutation that preserves the current assigned literals cannot change
`t = 17` to `t = 12` while those literals still force 17. Merely changing
the initial tableau assignment may also be overwritten by the same bound
choices. This direction therefore needs the decisions and their arithmetic
meaning traced together, rather than just a different simplex starting point.

There is a concrete option-coverage gap to address before interpreting a
phase comparison. `--theory-phase` writes `CheckSatOptions::theory_phase`,
but the nonincremental QF_UFLIA recipe and
{settingssource}`shared driver <src/smt/component_solver.cpp>` do not consume
that field. Their `prepare_boolean` hook registers EUF Boolean arguments;
it does not install the initial theory phases used by other routes. With
the default online components and incremental EUF, however,
`LiaTheoryComponent::reset` forwards `--lra-model-phase` to
`ComponentLraExecution`, whose phase output reaches the SAT backend
({settingssource}`recipe and arithmetic component <src/smt/solver_engines.cpp>`).
This is a source finding, not a measured phase result. The two controls
must not be treated as interchangeable. On that same online path, the existing theory-decision hook supplies
arithmetic atoms in rotating order with their true polarity;
it is not the proposed numeric-target heuristic.

### Reconsider the equality choices made from a model

Both production EUF/LIA component descriptors have `model_mutation = false`.
The QF_UFLIA recipe puts LIA first as the model reconciliation priority;
`online_order = {1, 0}` independently schedules EUF before LIA callbacks.
The {settingssource}`coordinator <src/smt/component_model_coordinator.cpp>`
considers component-suggested pairs first and picks the lexicographically
first model disagreement. If none is found, it checks edges connecting
members of each model's equal-value classes. Ordinary choices try equality
first. Consequently the
selected pair reflects model disagreement, while its preferred polarity is
not chosen by a comparison of the estimated costs of the two branches.

Potential experiments are choosing another relevant disagreement, trying its
opposite polarity first, and constructing a compatible arithmetic model with
fewer accidental shared equalities before choosing an equality. These affect
search order or model choice. A failed mutation attempt is not a proof of
equality, and a model coincidence must remain reversible. Integer model
mutation must preserve integral inputs, all asserted bounds and mandatory
interface relations; the Real route's freedom-interval sweep cannot simply
be reused with rational moves. Also, the current mutation scheduler asks
non-leading components to adapt toward the leading component. Setting only
the LIA mutation capability would not schedule an independent diversification
of the leading arithmetic model.

The [existing model-based combination lead](model-based-combination.md)
records the larger protocol work. For `wisas`, first determine which
relevant shared values can actually move under the current trail. If the
bad slack is fixed, the earlier bound-decision direction is required to move
that slack; other model freedoms may still remove unrelated coincidences.

The model-choice count limit is not a budget for work inside one SAT call.
{settingssource}`solve_component_recipe <src/smt/component_solver.cpp>`
checks the limit before entering `backend.solve()`. In the recorded
`xs_28_48` M124 timeout there are 17 model decisions and no complete fallback;
the default limit is 512. The fast options-off run has 96 model decisions.
These termination snapshots do not identify which assumption causes the
long call, but show why fewer outer choices need not mean cheaper search.
A trial that runs out of a work budget must not generate a blocking clause
as if its assumptions had been proved inconsistent.

### Learn consequences that survive changes of counter-chain pattern

The historical 55–56-literal conflicts motivate checking which congruence
steps recur across different patterns. Clause length alone does not prove
that antecedents are redundant. A stronger alternative to repeatedly
explaining whole patterns is to expose selected functional-consistency
clauses to SAT. For unary `f`, the theory-valid clause is

```text
x != y OR f(x) = f(y)
```

It can propagate argument disequality from result disequality on a later
branch, before a complete congruence conflict is rebuilt. For a function of
several arguments the argument disequalities form a disjunction; they must
not be inferred individually from unequal results.

Dutertre and de Moura's [*The Yices SMT Solver*](https://yices.csl.sri.com/papers/tool-paper.pdf),
2006, Section 2, p. 3, selects a clause when its congruence step participates
in a conflict. Primo has an optional propagation subset over available atoms,
but not the full conflict-explanation selector, as documented in the
[Yices survey](../papers/yices.md) and
[theory-lemma lead](theory-lemmas.md). The earlier rejection of repeated
candidate scanning does not evaluate this selector. The connection to the
`wisas` conflicts is a hypothesis; the old traces did not measure clause
reuse or establish that these clauses summarize the necessary counting
argument.

A first diagnostic should identify repeated application pairs in conflict
explanations, the equality atoms that are missing, and whether the resulting
clauses become unit or conflicting on other patterns. Keep the existing
EUF checker complete. Original implementation would require a design note
mapping explanation pairs to SMT-owned atom creation, deduplication and
budgets, with SAT owning clause propagation. Measure candidates inspected,
clauses emitted, useful propagation, reuse and memory. This is separate from
shortening the final EUF explanation or asserting a guessed aggregate bound.

### Order of investigation

1. Verify the effective decision/phase controls on this route and trace the
   threshold decisions that first pin the four already studied slacks.
2. Test numeric-target decisions and equality-choice polarity/order
   independently under a bounded diagnostic, then compare on the full lost
   set and family controls. Retain the existing correctness and model gates
   before accepting a behavior change.
3. Audit recurring congruence steps and the missing-atom cost for
   conflict-driven Ackermannization. Promote it to an implementation proposal
   only if the trace shows reusable propagation opportunities.

Care-guided combination offers another structural reduction of arrangement
search. Its full correctness contract and model construction remain open in
the [care-guided lead](care-guided-combination.md). Changing that acceptance
condition is a larger project than testing decision order while retaining
the current complete partition checks.

## Literature

- **Heavy tails and restarts.** Gomes, Selman, Crato and Kautz,
  [*Heavy-tailed phenomena in satisfiability and constraint satisfaction
  problems*](https://doi.org/10.1023/a:1006314320276), JAR 24 (2000)
  67–100: backtrack search run times are often heavy-tailed, and randomized
  rapid restarts remove the tail. Luby, Sinclair and Zuckerman,
  [*Optimal speedup of Las Vegas
  algorithms*](https://doi.org/10.1016/0020-0190(93)90029-9), IPL 47 (1993)
  173–180, give the universal restart schedule for an unknown run-time
  distribution.
- **Variability in MIP solvers.** Lodi and Tramontani,
  [*Performance variability in mixed-integer
  programming*](https://doi.org/10.1287/educ.2013.0112) (2013), survey run-time
  differences between equivalent inputs. Fischetti and Monaci,
  [*Exploiting erraticism in search*](https://doi.org/10.1287/opre.2013.1231),
  Operations Research 62 (2014) 114–122, propose bet-and-run: several short
  randomized runs, then the most promising one is continued. Fischetti, Lodi,
  Monaci, Salvagnin and Tramontani, [*Improving branch-and-cut performance by
  random sampling*](https://doi.org/10.1007/s12532-015-0096-0), MPC 8 (2016)
  113–132, use several optimal LP bases.
- **Restarts inside branch-and-bound.** Anderson, Hendel, Le Bodic and
  Viernickel, [*Clairvoyant restarts in branch-and-bound search using online
  tree-size estimation*](https://doi.org/10.1609/aaai.v33i01.33011427), AAAI
  2019, restart when the estimated remaining tree is large.
- **Generic bet-and-run.** Friedrich, Kötzing and Wagner, [*A generic
  bet-and-run strategy for speeding up stochastic local
  search*](https://doi.org/10.1609/aaai.v31i1.10645), AAAI 2017.
- **Phase resets in CDCL.** Biere and Fleury, [*Chasing target
  phases*](https://fmv.jku.at/chasing-target-phases/), POS 2020 (authors'
  page), describe rephasing: saved phases are reset at intervals to
  diversify the search.

These summaries come from the abstracts and publisher metadata; the papers
have not yet been read into a design note, as the paper-grounded rule
requires before implementation. A web search on 2026-11-03 found no published
description of a mainstream SMT solver that restarts at the theory or
DPLL(T) level beyond the CDCL restarts of its SAT engine.

In Primo, CaDiCaL restarts reach only part of the state that makes the
commitment:

- Branch-and-bound is not made of SAT decisions. It is an explicit DFS
  inside {source}`branch_and_bound <src/theory/arithmetic/lia_branch_and_bound.cpp>`,
  rebuilt in each integer completion call ([LIA](../implementation/lia.md)).
  It is not state that a SAT restart could reset.
- Model-based combination equalities, after de Moura and Bjørner,
  [*Model-based theory
  combination*](https://doi.org/10.1016/j.entcs.2008.04.079), ENTCS 198
  (2008) 37–49, are applied as SAT assumptions
  by the shared driver ([theory
  combination](../implementation/combination.md#search-fallback-and-inconclusive-results)),
  and a restart
  keeps assumptions.
- The threshold atoms on the slack are ordinary SAT variables, but their
  saved phases survive a restart, apart from CaDiCaL's own periodic
  rephasing.

## Candidate mechanisms

1. **Theory-side rephasing.** When conflicts keep naming the same pinned slack
   value, reset the saved phases of that slack's threshold atoms and of the
   combination equalities built on it.
2. **Restart epochs on a Luby schedule.** At each epoch, change one
   search-only choice: phase policy, the order of combination decisions, or a
   random seed. Each choice must be sound by construction because it changes
   only the search order.
3. **Bet-and-run inside one process.** Several short runs with different
   search-only choices, then continue the run with the best progress measure.

## Obligations and falsifier

- First trace the `wisas` case to establish which state survives a CaDiCaL
  restart: saved phases, the theory's re-proposed decisions, or both.
- Every mechanism must keep answers, models and incremental behavior intact.
  It needs the merge fuzz gate and model validation like any search change.
- Evaluate at 60 s against each route's default (on QF_LIA since `5dec036`
  the four integrality options are on; on QF_UFLIA they are off), with a
  rerun of that default to bound timing noise. The 2026-11-03 reruns found
  QF_UFLIA reproducible exactly and the QF_LIA options-off configuration
  within five files. The all-options arm, now the QF_LIA default, solved 53
  fewer on its rerun, almost all near the cutoff, so a QF_LIA comparison
  needs a paired run.
- **Falsifier:** with `--lia-primitive-int-rows` on, the mechanism must
  recover at least 20 of the 30 lost `wisas` files. It must also not lose,
  net, files that the same configuration without the mechanism solves on
  QF_LIA or QF_UFLIA, beyond the noise the reruns bound. Otherwise reject
  it. Measured with the options off, it must not lose net files against
  the options-off configuration, which is the QF_UFLIA default and, on
  QF_LIA, the `--no-lia-*` form.

The time-sliced portfolio of separate configurations is a different project;
see the [portfolio lead](portfolio.md).
