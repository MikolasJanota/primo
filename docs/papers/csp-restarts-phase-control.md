# CSP restarts and branch orientation

**Status: related work and experiment motivation; no solver change.**
The finite-domain results below do not qualify a Primo phase policy.

## Source and reading scope

Luís Manuel Tremoceiro Baptista, *Using Restarts in Constraint Programming
over Finite Domains — An Experimental Evaluation*, PhD thesis, Universidade
NOVA de Lisboa, September 2017 {cite:p}`baptista2017restarts`.
The [institutional record](https://run.unl.pt/entities/publication/c1b32213-bd2b-4759-8d7d-60b692354d5f)
provides the public version. The user supplied `_external/csp.pdf` was read on
2026-11-05; its SHA-256 is
`e024ef6ef5409ee33170601daa51f4de9f17db41df483f2a2ade0671190e6322`.
The supplied PDF stays outside tracked documentation assets.

Reading focused on §§3.1–3.4 and §6.4, including §6.4.1 and Tables 6.5–6.6;
selected passages in §§2.1.2–2.1.3, 6.1–6.2, 6.6–6.7 and Chapter 7 supplied
context. Page references below use printed numbering: printed p. 96 is PDF
page 114. The full restart/nogood construction and its proof were not reviewed
for implementation. Background descriptions of other authors' heuristics are
an overview, not a substitute for reading their original algorithms.

## Findings relevant to the current investigation

The thesis separates variable choice, split-point choice and which side of a
split is explored first. These are different search controls. In particular,
§3.4, pp. 36–37, surveys impact-based search, activity-based search and
counting-based search. It describes high-impact variable selection combined
with low-impact value selection; it does not establish a universal preference
for either narrower or wider numeric regions.

Section 6.4, pp. 94–96, records useful negative results. Midpoint splitting,
trying a smaller portion first, and a split-point heuristic using positive/
negative occurrences in domain-splitting nogoods did not improve the reported
Talisman/Latin-square experiments. Those experiments do not support treating
small ranges, balanced trees or repeated failure counts as sufficient signals
of an expensive branch.

Section 6.4.1, pp. 96–99, studies **flexible domain-splitting branching**.
The first branch may impose either x ≤ v or x > v, with the second branch
exploring its complement. The thesis randomly chooses the orientation at each
node. In this terminology, a "positive decision" means the first branch;
it does not mean a universally fixed arithmetic direction or SAT sign.
Tables 6.5–6.6 report improvements in the selected finite-domain experiments.
This is randomized branch orientation, not an adaptive detector of bad strips.

Sections 6.1–6.2 distinguish standalone activity from adding it to a useful
base heuristic. Several activity/failure-order variants fail on the tested
instances; `dom+act` instead uses nogood activity to break domain-size ties.
Section 6.6 further combines that information with `dom/wdeg`, with
problem-dependent results. These results motivate measuring combinations,
not replacing an effective policy merely because a new score is available.

## What transfers, and what does not

| Idea | Relation to Primo | Status |
| --- | --- | --- |
| Reverse the order of complementary threshold branches | Choosing the preferred sign of an existing unforced arithmetic atom can change which side is tried first. | Candidate experiment only. No random phase policy added. |
| Domain size, midpoint and domain-volume scores | They require finite domains or a separately justified abstraction. An Int sort can be unbounded; bounds do not count combined EUF/LIA models. | No direct transfer. See the [finite-domain limitations](arithmetic-phase-control.md#csp-connections-and-the-finite-domain-limitation). |
| Propagation activity | Counting bound-tightening events does not itself require a finite numeric cardinality. Such counts need attribution to decisions and a defined history/decay policy. | Possible diagnostic signal, not an implementation of activity-based search. |
| `dom+act`, `dom/wdeg` and conflict-ordering variable choice | The thesis changes which numeric variable is selected. Primo's current phase-only investigation keeps SAT variable selection. | Outside this investigation. |
| Restart-derived domain-splitting nogoods | Their extraction, simplification and schedule belong to the finite-domain search procedure. Primo already delegates CDCL learning and restarts to its SAT backend. | No new nogood extractor or restart schedule adopted. |

## Bounded implication for phase experiments

For an existing threshold atom, complementary sign preferences can be tried
without enumerating the numeric domain, choosing a new split point, or adding
an arithmetic assumption. This remains possible when the expression is
unbounded. It does not transplant the thesis's finite-domain solver or its
termination argument.

The {settingssource}`current arithmetic phase producer <src/smt/solver_engines.cpp>`
evaluates inequalities at the live rational tableau assignment β and publishes
persistent signs through the
{settingssource}`SAT adapter <src/sat/cadical_backend.cpp>`.
It skips equality atoms and does not expose a fresh sign callback for the
variable SAT has just selected. A randomized orientation experiment therefore
needs a precise sampling/delivery contract: resampling on every phase refresh
is not the thesis's once-per-node choice. Actual unforced decisions, override
windows and explicit-assumption exclusions must be measured.

The existing {ref}`narrow-region candidate <narrow-region-phase-candidate>`
keeps β as its ordinary preference. A possible future comparison is sparse,
seeded sign overrides versus overrides conditioned on repeated entry into a
bounded region, with comparable declared override budgets. Actual override
counts and effects must be reported; equal budgets do not imply equal search
trajectories. This would test whether the region signal adds information
beyond perturbing phases.

The follow-up [phase-region simulations](../leads/phase-region-simulation.md)
evaluate an isolated adaptation on the recorded QF_UFLIA losses. They match
the probability per eligible hint context, rather than the total number of
overrides: the history gate permits far fewer flips. This is therefore not
a comparison at matched actual override counts. Its implementation and
measurement status are recorded in that report; the thesis remains motivation,
not the source of Primo's interval/history gate.

The [repair schedule](../implementation/lia.md#repair-schedule-and-phase-advice)
still matters: live rational β and the later private integer-completion model
are different measurements. A forced bound or an active combination assumption
cannot be reversed by a phase preference. Narrowness alone must not classify
a branch as bad: useful propagation and difficult repetitive reasoning can
both restrict the apparent options. The
[earlier phase diagnostic](../leads/adaptive-phase-probe.md) and the follow-up
simulations hold the measured evidence. This reading itself adds no benchmark
result or default proposal.
