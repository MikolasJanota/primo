# Diversifying the DPLL(T) search

**Status: Open lead. Owner: unassigned.** Nothing here is implemented.

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

The CaDiCaL restarts within the 30 s, 62,873-conflict `xs_28_48` run under
the option apparently do not leave
the region; restart counts were not recorded. A possible reason is that
restarts keep saved phases,
so the same assignment is rebuilt. The stuck state is also made of decisions
the arithmetic theory and the model-based combination keep re-proposing,
which SAT-level phase heuristics may not reach. Both points are hypotheses to
check in a trace; neither is established.

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
- Evaluate on QF_UFLIA and QF_LIA at 60 s against the current default, with a
  rerun of the default to bound timing noise. The 2026-11-03 reruns found
  QF_UFLIA reproducible exactly and the QF_LIA default within five files.
- **Falsifier:** with `--lia-primitive-int-rows` on, the mechanism must
  recover at least 20 of the 30 lost `wisas` files. It must also not lose,
  net, files that the same configuration without the mechanism solves on
  QF_LIA or QF_UFLIA, beyond the noise the reruns bound. Otherwise reject
  it. Measured with the options off, it must not lose net files against
  the default.

The time-sliced portfolio of separate configurations is a different project;
see the [portfolio lead](portfolio.md).
