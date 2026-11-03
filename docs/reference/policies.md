# Effective policies and option dependencies

The [complete CLI reference](options.md#complete-cli-inventory) lists values
and defaults. This page explains how the requested options become an effective
configuration. Inspect the matching [statistics](statistics.md), because a
setting can be inapplicable to the selected route.

## EUF scheduling and propagation budgets

{source}`resolve_euf_propagation_mode <src/smt/solver_engines.cpp#L484-L505>`
resolves `--euf-prop=auto` as follows:

1. At most 5,000 theory atoms: `online`.
2. Otherwise, at least 500 recognized finite domains and no more pure-negative
   theory atoms than domains: `online`.
3. Otherwise: `conflict`.

`conflict` keeps assignment/conflict callbacks but does not run ordinary
positive-equality propagation scans. `off` omits those online callbacks;
`model` performs propagation checks after a candidate SAT model. Final theory
consistency checking remains necessary in every route. Explicit choices bypass
this automatic selector. Restrictions of retained sessions are in
[sessions](../implementation/sessions.md).

For $n$ theory atoms, {source}`budget resolution
<src/smt/solver_engines.cpp#L793-L821>` gives an automatic candidate budget
$b=n$ and an automatic delivery budget
$\max(1,\min(n,\lfloor b/2\rfloor+1))$. Candidate limits bound inspections;
delivery limits bound produced consequences. `unlimited` removes the respective
cap. Numeric delivery zero also means unlimited; candidate zero is a zero
inspection budget. If candidates are unlimited, automatic delivery still uses
$n$ to compute its bound. These are work limits for a scan, not global limits
on the total number of theory lemmas.

For example, 100 atoms with both budgets automatic select online mode and
resolve to 100 candidate inspections and at most 51 deliveries per applicable
scan. Assignment thresholds, hit-rate cutoffs, and propagation backoff can end
a scan earlier. The corresponding `:euf.propagation.*` counters distinguish
attempts, inspected candidates, deliveries, and cutoffs.

## Initial phase hints

`--theory-phase` defaults to `negative`. `positive` prefers equalities, `none`
leaves backend phases unchanged, and `polarity` sets a hint only for atoms with
one syntactic polarity. These are hints before SAT search, not theory-selected
decision variables.

The explicit `auto` heuristic chooses polarity only when finite-domain or UF
symmetry structure exists, there are **1–25** pure-negative atoms, and at least
100 pure-positive atoms. Otherwise it chooses negative. The nonzero lower bound
matters; zero pure-negative atoms do not select polarity. See
{source}`resolve_theory_phase_mode <src/smt/solver_engines.cpp#L555-L587>`.

Separately, automatic basic preprocessing can select polarity hints for its
multi-assertion profile or its narrower single-assertion predicate profile.
An explicitly supplied `--theory-phase` disables these overrides;
`--no-auto-basic-multi-polarity` also disables the multi-assertion override.
The `:sat.phase.auto-*` counters report which policy actually ran.

## Structural preprocessing selection

`--auto-preprocess-selector=legacy` remains the default. Structural selection
analyzes the implicit conjunction, counting shared `And` nodes with saturating
occurrence weights. It does not distribute arbitrary formulas or create terms
during analysis. The recognizers and policy are owned by
{source}`preprocessing strategy <src/smt/preprocessing_strategy.cpp>` and
{source}`preprocessing analysis <src/smt/preprocessing.cpp>`.

| Option suffix (after `--auto-`) | Default | Meaning |
| --- | ---: | --- |
| `preprocess-large-conjunct-limit` | 10000 | Upper bound before selecting the large-conjunction skip profile |
| `basic-compact-term-limit` | 20000 | Upper term bound for compact clause-like input |
| `basic-compact-min-conjuncts` | 2 | Minimum conjunct count for that profile |
| `basic-single-term-limit` | 15000 | Upper term bound for the finite-domain profile |
| `basic-single-min-domains` | 200 | Minimum recognized domains |
| `basic-single-max-symmetry-terms` | 6 | Upper symmetry-term bound |

Zero means unlimited only for upper bounds; minimum counts must remain positive.
The priority is large-skip, finite-domain-basic, compact-clause-basic, then
strong. Defaults live in {source}`AutoPreprocessTuning <src/smt/check_sat.hpp#L35-L44>`.
The [preprocessing chapter](../implementation/preprocessing.md) explains the
selected transformations and the distinct integer policies.

For $V$ unique reachable conjunction nodes, $E$ conjunction edges, and $R$ roots,
conjunction traversal and occurrence propagation take expected $O(V+E+R)$ time
with hash-based lookup. The analysis also recognizes clauses at terminal
conjuncts: with $L$ literal inspections, total expected time is
$O(V+E+R+L)$. A single large disjunction can make $L$ large even when $V=0$.
Auxiliary storage is $O(V+C+R)$ for $C$ distinct terminal conjuncts, including
root-sized reservations. See the
{source}`analysis and recognition calls <src/smt/preprocessing.cpp#L3100-L3158>`.
Finite-domain recognition additionally counts pairs within
observed value sets. It does not predict all later equality-definition,
congruence, or repeated-round growth. Structural selection is an empirical
policy; the {source}`rejected promotion <docs/structural-auto-preprocessing.md#L106>`
remains historical evidence.

(automatic-lra-heuristics)=
## Automatic QF_LRA heuristics

The unspecified `--lra-model-phase` and `--monotone-elimination` policies resolve
to enabled only when the declared logic is `QF_LRA`, the session mode is
`rebuild`, and preprocessing is `auto`. Other routes and explicit preprocessing
profiles resolve them to disabled. Use `--no-lra-model-phase` and
`--no-monotone-elimination` to disable either policy explicitly; the positive
flags explicitly enable them. Explicitly enabling monotone elimination remains
an error in assumptions mode, whose assertions can later be popped.

The {lrapolicysource}`query policy resolution
<src/smt/preprocessing_strategy.cpp#L554-L566>` runs on the query-owned
`CheckSatOptions` copy before normalization. `std::optional<bool>` distinguishes
an unspecified policy from an explicit false value. Resolved settings do not
modify the session's requested policies or leak across `reset` or logic changes.
Low-level engines without declared-logic policy resolution treat an unspecified
setting as disabled. The {lrapolicysource}`session policy regressions
<tests/unit/smt_session_tests.cpp>` and
{lrapolicysource}`CLI regressions <tests/cmake/check_lra_heuristic_cli.cmake>`
cover these boundaries.

These defaults were qualified on the separate `f0fbe67` implementation; its
measurements remain recorded under `2026-10-09-lra-automatic-default-committed-qualification`
in the run ledger. The migration's frozen ablations use their original explicit
profiles; they do not become measurements of the combined implementation after
this merge. Equality discovery remains the separate default-off experiment.

## Arithmetic and search controls

The configured {source}`arithmetic options <src/smt/check_sat.hpp#L250-L310>`
form dependent groups:

- Row propagation requires LRA propagation; reverse row propagation also
  requires row propagation. The forward row-size/fanout defaults are 64/64,
  and reverse defaults are 32/32. A limit is useful only when its producer runs.
- `--lra-equality-propagation` is a default-off pure-QF_LRA experiment requiring
  IPASIR-UP. It proves variable equalities with the model-unit recognizer and
  propagates existing difference comparisons. It is independent of the bound
  propagation switches and the mixed-route equality-producer choice. See
  {ref}`its scope and explanation rules <experimental-pure-arithmetic-equality-propagation>`.
- Sparse pricing and sparse/least-violated leaving choices apply to the sparse
  pivot rule. SOI minimization belongs to the SOI rule. They are not interchangeable
  dimensions of one unconditional tuning space.
- Fixed-basic elimination and model phase hints have separate guards and route
  defaults. See [arithmetic](../implementation/lra.md).
- Integer branch limits, the patching move budget (`--lia-patch-limit`), cut controls, quantifier tuple order,
  and instance budgets are listed in [CLI help](options.md#complete-cli-inventory)
  and explained in [LIA](../implementation/lia.md) and
  [quantifiers](../implementation/quantifiers.md).
- The `--lia-*` and `--no-lia-*` flags are overrides. Each integer route
  (QF_LIA and QF_UFLIA) takes its LIA defaults from `default_lia_settings`,
  as described in {ref}`settings and who sets them <lia-settings>`.

Before tuning a parameter, confirm its call path and compare mechanism counters
at two admissible values. A flag's presence in help does not establish that it
can affect the chosen input/profile. The old
{source}`parameter investigation <docs/tuning-parameters.md>` retains examples
of formerly inert limits and inconclusive seed perturbations; those measurements
must not be presented as current measurements.

## SAT backend configuration

See [backend operations](backend.md) for overrides, factoring, separate version
builds, and the meaning of SAT search counters. Backend switches do not bypass
Primo's admission checks or qualify an experimental profile.
