# Portfolio of solver configurations

**Status: Open lead, separate project. Owner: unassigned.** Nothing here is
implemented, and it is independent of the in-solver
[search diversification](search-diversification.md) lead.

## Question

Can a time-sliced portfolio of existing Primo configurations recover the files
one configuration loses without giving up what another gains? The integrality
options are the motivating case. With all four on, QF_LIA gains files and
QF_UFLIA loses 31, 30 of them `wisas` ({ref}`integrality evaluation
<integrality-evaluation>`).

## First estimate

The estimate below combines the per-file times of the 2026-11-02 timing runs
at `2e120fa` (SLURM jobs 335585 and 335586, 60 s per file). For a schedule of
$a$ seconds of one configuration then $60-a$ seconds of the other, a file
counts as solved if either run solved it within its slice. This assumes the
second run's time does not depend on the first.

| Schedule | QF_LIA (13,306) | QF_UFLIA (659) |
|---|---|---|
| Options off, 60 s | 9,662 | 575 |
| All four options, 60 s | 9,798 | 544 |
| Off 5 s, then all 55 s | 9,936 | 574 |
| Off 10 s, then all 50 s | 9,887 | 574 |
| All 5 s, then off 55 s | 9,907 | 573 |
| Virtual best of off and all (not a schedule) | 10,073 | 575 |

This is an optimistic estimate from one run each, not a measurement or a
bound: timing noise can move files either way. The
QF_LIA rerun of 2026-11-03 (job 339382) reproduced the options-off arm within
five files. The all-options arm, though, lost 60 files that had solved in
53–60 s, 57 of them `nec-smt`. Its gains near the cutoff are fragile.

The same estimate from the paired 120 s rerun of 2026-11-03 (jobs 345183 and
345182), where both configurations ran on the same node, gives on QF_LIA:
off 9,696, all 9,749, off 5 s then all 9,909, off 10 s then all 9,848,
all 5 s then off 9,950, and a virtual best of 10,032. On QF_UFLIA the same
schedules give 575, 544, 574, 572, 573 and 575. On QF_LIA, the schedule that
starts with the options is the best of the three in this data, unlike in the
first estimate; on QF_UFLIA it is one file behind off-first.

## Literature

Wintersteiger, Hamadi and de Moura, [*A concurrent portfolio approach to SMT
solving*](https://doi.org/10.1007/978-3-642-02658-4_60), CAV 2009, 715–720,
run diversified Z3 configurations in parallel. Scott, Niemetz, Preiner,
Nejati and Ganesh, [*MachSMT: a machine learning-based algorithm selector for
SMT solvers*](https://doi.org/10.1007/978-3-030-72013-1_16), TACAS 2021, choose
a solver per input. The bet-and-run work cited in the diversification lead is
the sequential counterpart.

## Obligations and falsifier

- Define where the portfolio lives. A wrapper outside the solver changes
  nothing in Primo; a built-in schedule must reset all per-run state and keep
  incremental sessions out of scope or define their semantics.
- Measure the schedule as one solver on SLURM, with the same 60 s budget per
  file including restart overhead, and rerun it to bound noise.
- Every configuration in the schedule must already pass its own fuzz and
  model gates.
- **Falsifier:** if the measured schedule does not beat the best single
  configuration on QF_LIA while keeping QF_UFLIA at 575 or more, reject it.
  On QF_UFLIA the virtual best equals the default, so no off/all schedule
  can gain there.
