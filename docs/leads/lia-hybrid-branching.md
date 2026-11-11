# Hybrid integer branching

**Status: original implementation checklist; the ordinary-split subset is now
qualified as an opt-in policy.** See {ref}`the current representation <lia-hybrid-branching>`
and {ref}`the gains, regressions and verification evidence <lia-hybrid-evidence>`.
Baseline:
`c49585a0`. Preserve private branching as the default for QF_LIA and QF_UFLIA.
The intended outcome is bounded private exploration followed by a SAT-visible
integer-bound choice, with sound explanations and separately validated models.

## Sources and implementation status

Read Alberto Griggio, [*A Practical Approach to Satisfiability Modulo Linear
Integer Arithmetic*](https://doi.org/10.3233/SAT190086), JSAT 8 (2012), §§5–5.2,
printed pp. 14–19. Section 5 supplies proportional internal branching, the
history score below, and the fallback to SAT. Section 5.1's proof-derived
linear-combination splits are deferred. Section 5.2 explicitly discusses the
remaining termination limitation; this milestone adds no termination theorem.

Read Barrett, Nieuwenhuis, Oliveras and Tinelli, [*Splitting on Demand in SAT
Modulo Theories*](https://homepage.divms.uiowa.edu/~tinelli/papers/BarNOT-LPAR-06.pdf),
LPAR 2006, §§2.2–2.3: new atoms may postpone theory consistency checking, but
acceptance still requires a consistent theory model. Their termination argument
requires a finite generated-literal universe, which arbitrary integer thresholds
here do not establish. The driver must preserve resource limits.

These papers are algorithmic sources, not implementation sources. The existing
[integer waterfall](../implementation/lia.md) supplies patching, GCD, optional
cubes, Gomory cuts and private branching. The sections below retain the original
implementation and acceptance requirements; they do not assert qualification.

## Representation and ownership

| Owner and source | Planned state or operation |
| --- | --- |
| {integersource}`branch_and_bound <src/theory/arithmetic/lia_branch_and_bound.cpp>` | Optional history per stable `LraVarId`; explicit DFS stack; exact floor; node and branch-conflict counts. Arithmetic sees numerical variables, never SAT backend handles. |
| {integersource}`complete_integer_assignment <src/theory/arithmetic/lia_completion.hpp>` | Distinct split status and owning numerical request after private-bound restoration and rational feasibility repair. Keep every integer obligation, including integer slacks. |
| {ufliaablationsource}`LiaComponentAdapter <src/smt/solver_engines.cpp>` | Solve-scoped history, original semantic integer coordinates, numerical-to-`TermId` translation. Its private working tableau owns Gomory rows; only stable semantic coordinates may escape. |
| {source}`TheoryCheckResult <src/theory/theory_solver.hpp>` | Distinct integer split request with `TermId` and owned exact threshold, separate from equality decisions and entailments. |
| {source}`ComponentModelCoordinator <src/smt/component_model_coordinator.cpp>` | Forward split requests before model acceptance. Do not require an interface equality or shared term for an ordinary arithmetic split. |
| {source}`solve_component_recipe <src/smt/component_solver.cpp>` | Deduplicated bound-atom registry, fresh-atom budget, managed SAT choices and failed-assumption recovery at the existing detached growth boundary. |
| {hybridbaselinesource}`LiaSettings <src/smt/lia_settings.hpp>` and {hybridbaselinesource}`route policy <src/smt/lia_settings_policy.cpp>` | Private/hybrid mode, private factor and fresh-atom cap; expose matching CLI and strategy parameters without changing route defaults. |

The component's history survives SAT backtracking and atom installation; a new
solving invocation starts empty. The original-coordinate count is frozen before
generated split atoms, and counts distinct semantic integer terms rather than
slacks. Appended ordinary bounds use `PreparedLraTheory::append_atom`'s existing
single-variable path: no additional row or arithmetic variable. Do not change
`LraTableau::add_row`'s construction-only contract. Registration must invalidate
model tokens, assignment snapshots and equality-query state. Snapshot needed
term IDs before any term-creating operation; do not retain arena references.

## Private search and handoff obligations

1. Add `--lia-branching=private|hybrid` (default `private`) and
   `--lia-private-branch-factor` (default `1`). In hybrid mode use
   `min(branch_limit, factor * N)`, with overflow-safe saturation. Factor zero
   skips private splits after the existing patch/GCD/cube/cut stages. The factor,
   zero convention and definition of `N` are Primo engineering choices.
2. For each variable keep separate left/right observation counts and sums of
   fractional integer-variable counts after rationally feasible branch checks.
   Select the fractional variable minimizing the smaller side's mean; an
   unobserved side scores zero. Compare means exactly and break ties by stable
   variable ID. Record immediately infeasible sides as branch conflicts, not as
   feasible-model observations. These observation and tie conventions make the
   paper's underspecified cases explicit. A scan costs linear time in the
   integer obligations; counting fractionality is also linear per observation.
3. Private search may split integer slacks, but external selection considers
   only original semantic integer coordinates. Undo every speculative bound
   before handoff and repair rational feasibility. Rescan all integer obligations:
   return integer success if all are integral; otherwise choose a fractional
   original coordinate with the same score. Use `delta_floor`, including
   negative values and negative infinitesimals. If every original coordinate
   is integral but an integer slack remains fractional, diagnose a violated
   preparation invariant rather than exporting a private slack.
4. A numerical split is a request, not an UNSAT certificate. On budget exit do
   not turn an accumulated explanation from partially explored branches into a
   conflict. Eliminate a private split premise only after both branches close,
   as justified by Griggio §5's two-branch explanation rule. Real SAT split
   literals remain in later arithmetic conflict explanations.
5. Preserve cancellation and existing work limits. Count private nodes, branch
   conflicts, budget exits and split requests; measure private completion time.

## SAT scheduling and proof obligations

For an original Int term `x` and integer `k`, materialize one atom
$p\leftrightarrow(x\le k)$. Its false polarity is exactly $x\ge k+1$.
The disjunction is valid for every integer `x`; neither side is entailed.
For example a fractional value $-3-\varepsilon$ requests $k=-4$ and the two
choices $x\le-4$ and $x\ge-3$.

Deduplicate by semantic `TermId` and exact threshold; include compatible bounds
already present in the input. Prefer the lower branch through the existing
managed SAT-choice mechanism. An unsuccessful assumed choice only justifies a
blocking clause over failed assumptions, after which SAT must explore the
remaining choices. It cannot justify global UNSAT. Register new atoms outside
callbacks and install them in both online/offline combined routes and the
single-component QF_LIA route before resuming SAT.

Do not accept a candidate model with a pending split or incomplete integer
completion. A requested bound already assigned at this threshold must imply
progress or a diagnosed invariant failure. Count unique/reused atoms, SAT
resumes, choice conflicts, fresh-budget exits and peak generated atoms; measure
resumed SAT search separately. Add an optional fresh-atom cap, default zero
(unlimited); exhaustion returns `unknown` and never a conflict.

## Acceptance checklist

- Arithmetic: integer success after handoff repair; zero/exact/overflowing budgets; score updates and deterministic
  ties; negative/infinitesimal floors; feasibility after exhausted private search;
  explanations after one versus both branches close. Confirm ordinary split
  registration changes neither row nor arithmetic-variable count.
- Integration: several external splits, both polarities, bound reuse,
  backjumping, incremental scopes, UF interaction and registry allocation growth.
  Cover online/offline UFLIA, existing equality producers, private/hybrid modes,
  explicit exhaustion and a failed preferred choice in a satisfiable formula.
- Build with `scripts/slurm_prepare_benchmark_workspace.py --build-via-slurm`.
  In SLURM run `make check`, ASan, whole-tree static checks, include-cleaner and
  the prescribed source review. Run lint and `make docs`; inspect rendered docs
  and obtain independent documentation review.
- Run fresh differential gates of at least 1,000 cases for each affected logic,
  with hybrid enabled, plus real-seed mutations. Record commands, source hashes,
  references, seeds, limits, job IDs and all outcome classes in `docs/run-log.json`.
  Wrong answers, invalid models, crashes and untriaged disagreements block timing.
- Compare frozen `c49585a0` with the candidate on the existing 200-case LIA and
  300-case SMT-COMP 2026 UFLIA selections at 120 seconds, plus four Certora cases
  with three rotated paired repetitions. Report solved/PAR2, paired runtimes,
  memory, mechanisms and regressions. Reuse reference rows only with matching
  provenance. Separately validate every successful native SAT timing row with
  Dolmen; keep validation time out of speed measurements.
- Keep the feature opt-in. Retain an implementation only with evidence of
  benefit on the targeted exploration bottleneck and an account of regressions;
  otherwise remove experimental source and commit the rejection evidence.
  Use separate verified commits for the disabled split protocol, integrated
  policy and qualification evidence. Commit all required metadata and docs.
