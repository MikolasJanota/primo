# QF_LIA default profile update

Status: implementation and qualification in progress. The owner requested
QF_LIA's defaults become **M124 + uniform cubes + hybrid branching**. Other
logics keep their existing defaults. This changes configuration of existing
algorithms; it introduces no new integer inference or termination claim.

## Implementation checklist

- `default_lia_settings(LiaRoute::QfLia)` remains the single route-policy owner.
  Enable hybrid and uniform cubes; disable explicit integer-slack obligations;
  retain integer-bound rounding, primitive rows and integral patch moves.
- Keep the generic `LiaSettings{}` baseline and QF_UFLIA policy unchanged.
  Budgets, preprocessing, row propagation, equality export and EUF options
  retain their values. Explicit route-supported overrides still win.
- Settings remain copied by value into solver steps/components. SAT-bound
  registration, history, resource accounting and private cut lifetimes retain
  their existing contracts.
- Update CLI defaults and strategy identity tests. Pin old engine settings in
  stage-specific tests, and test actual cube/hybrid execution under new defaults.
- Build through the SLURM preparation helper; run native/ASan checks, fresh
  campaign-grade QF_LIA generation, real-seed mutations, tracked/scoped models
  and controls. Every successful selected SAT row requires Dolmen acceptance.
- Compare the exact new default against old settings and the individual
  cube/hybrid additions on the existing frozen LIA selection. Keep timings
  separate from model validation; retain incomplete outcomes and regressions.
- Update current documentation, distinguish historical evaluations, obtain
  independent documentation/evidence review, build/render/check docs and commit.

## Basis and scope

The existing [cube implementation](../implementation/lia.md#lia-cubes) follows
Bjørner–Nachmanson, CAV 2024 §4.2, with its documented subset and candidate
validation. [Hybrid branching](../implementation/lia.md#lia-hybrid-branching)
implements ordinary-coordinate splitting from the documented partial Griggio
§5 policy. Proof-derived combination splits remain absent. The earlier
[cube](lia-cubes.md), [hybrid](lia-hybrid-20261111.md) and
[combination](arithmetic-combinations.md) studies motivate this configuration;
they do not measure this exact combined default on the current source.

M124 omits redundant integer-slack obligations from patching/completion. Proved
slack integrality can still justify preparation-time threshold rounding. All
actual Int terms must still be integral before accepting a model.
