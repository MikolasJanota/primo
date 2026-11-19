# Linear arithmetic

Primo's linear real arithmetic (LRA) solver maintains a tableau: linear equations,
an assignment and the currently active variable bounds. Linear integer arithmetic
(LIA) uses the same tableau representation as a rational relaxation, temporarily
ignoring integrality. Integer completion then seeks a satisfying assignment in
which the required variables have integer values. The
[capability summary](../../capabilities.md#implemented-mechanisms-and-defaults)
and [effective policies](../../reference/policies.md) distinguish available
mechanisms from route defaults.

## Current implementation

Read the LRA state and feasibility chapters before the integer stages. Integer
completion reuses the exact tableau, bounds and explanations; it does not define
another rational solver.

| Question | Chapter |
| --- | --- |
| What state is prepared, and how do models and rollback work? | [LRA overview](../lra.md) → [tableau state, models and rollback](lra/tableau.md). |
| How does arithmetic establish rational feasibility or explain failure? | [Rational feasibility and conflicts](lra/feasibility.md). |
| Which consequences are propagated during solving? | [Shared arithmetic propagation](lra/propagation.md), including bound rules for all four supported ground arithmetic logics and route-specific equality discovery. |
| Which pivot and search alternatives exist? | [Pivot policies and optional methods](lra/search.md), including sum of infeasibilities and phase hints. |
| How are integer obligations represented? | [LIA overview](../lia.md) → [integer input and integrality](lia/preparation.md). |
| When does completion run, and how does patching repair values? | [Completion scheduling and patching](lia/repair.md), including interval reuse. |
| What do cubes, divisibility checks and cuts establish? | [Cube models, GCD checks and cuts](lia/cubes-and-cuts.md), with their distinct model/refutation roles. |
| How does integer search cooperate with SAT, and when is it incomplete? | [Branching, explanations and limits](lia/branching.md). |

The LRA and LIA overview pages retain their source/test maps. Each child chapter
links back to those maps and keeps the derivations, examples, ownership and
limitations of its subject. The existing overview paths and section fragments
remain entry points; internal links point directly to the detailed destination.

## Adjacent layers

Some arithmetic work belongs to another architectural layer. These pages own
those contracts rather than duplicating them inside LRA and LIA:

| Boundary | Owner |
| --- | --- |
| Before Boolean search | [Preprocessing order and optional unconditional bounds](../preprocessing.md#optional-bound-tightening); [arithmetic normalization](../preprocessing.md#arithmetic-atom-normalization-and-equality-elimination). |
| SAT integration and explanations | [DPLL(T)](../dpllt.md) and [the theory interface](../theory-interface.md). |
| Sharing equalities and reconciling UF/arithmetic models | [Theory combination](../combination.md); [QF_UFLIA equality integration](../uflia-equality-propagation.md). |
| Exact controls and observability | [Options](../../reference/options.md), [policies](../../reference/policies.md) and [statistics](../../reference/statistics.md). |

The pure and mixed routes share arithmetic mechanisms but keep their documented
integration paths and defaults. In particular, QF_LIA's hybrid/cube defaults do
not change the QF_UFLIA defaults.

## Research and completed evidence

The [arithmetic research index](../../leads/index.md#arithmetic) names residual
questions, including equality exploitation, patching and arithmetic efficiency.
It is separate from the implemented mechanisms above.

The evidence index groups [integer comparisons](../../evidence/index.md#integer-arithmetic),
[bounds and propagation studies](../../evidence/index.md#bounds-and-propagation)
and [mixed-theory investigations](../../evidence/index.md#euf-and-theory-combination).
The [Real-arithmetic record](../../evidence/arithmetic.md) retains its own
measurements and rejected alternatives. Original implementation checklists are
records under those evidence groups, rather than additional current manuals.

Primary-source mappings are in the [LRA source table](../lra.md#sources-and-implementation-status)
and [LIA source table](../lia.md#sources-and-implementation-status). The
[Bromberger reading guide](../../papers/bromberger-linear-arithmetic.md),
[proof-derived cuts note](../../papers/cuts-from-proofs.md),
[primal-heuristic survey](../../papers/lia-primal-heuristics.md) and
[integrality checklist](../../papers/mip-presolve-integrality.md) distinguish
implemented subsets from further paper mechanisms.
