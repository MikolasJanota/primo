# Integer-model repair and MIP primal heuristics

**Status: Literature review and proposed comparisons.** Primo implements
tableau patching; the MIP methods below are research candidates. Their
published results are not performance evidence for Primo.

Finding an integer model after solving the real relaxation is a **primal
heuristic** problem in integer programming. “Primal” here refers to finding
a feasible assignment. An SMT solver needs any model of the current Boolean
candidate; it has no application objective to optimize. This makes methods
for finding a first feasible point more directly relevant than methods that
improve an existing integer solution's objective value.

## How SMT arithmetic relates to ILP and MIP

For a conjunction of linear integer constraints, the feasibility problem is
shared. LP relaxations, GCD reasoning, Gomory cuts, branch-and-bound and
primal heuristics belong to a common body of integer-programming techniques.
Griggio's 2009 thesis, §2.6, explicitly combines a rational solver,
Omega-style equation handling and branching inside SMT; see the retained
{source}`thesis note <docs/papers/griggio-mathsat-thesis.md>`.

The surrounding solver imposes different demands. A Boolean SMT search
repeatedly changes which arithmetic bounds are active. Arithmetic must
produce explanations for conflicts and propagations, and shared values must
agree with other theories. MIP work often concentrates on improving an
objective and proving optimality through relaxation bounds and an incumbent
solution. These are typical emphases, not exclusive capabilities: MIP can
solve feasibility problems, and optimization modulo theories adds objectives
to SMT. Sebastiani and Tomasi's [2015 OMT paper](https://arxiv.org/abs/1410.6039)
is an explicit bridge; its abstract motivates this connection, rather than
an additional algorithm reviewed here.

Bjørner and Nachmanson §2 directly discusses the engineering tradeoff:
exact arithmetic, backtracking and eager CDCL case splits lead to different
choices from the MIP workloads they compare against. Thus a useful MIP
heuristic may still need changes to its arithmetic, state restoration,
explanations and invocation budget. There is clear exchange of ideas; these
sources do not establish how much either research community overlooks the
other. For Primo, the practical rule is to search both literatures and cite
the underlying method separately from its SMT adaptation.

## Primo's baseline and the research question

{source}`patch_integer_variables <src/theory/arithmetic/lia_patching.cpp>`
uses freedom intervals to preserve rational feasibility. It accepts a move
only when the total number of fractional integer variables decreases.
`integer_targets` uses the hard-coded `kTargetWindow = 4`: eight candidates
around the target's floor. `--lia-patch-limit` instead limits accepted moves.
Neither the window nor the strict count score is a claim from the MIP papers.

The incidence lists and cached counts in `PatchingScoringIndex` score a move
by inspecting affected integer rows. They remain valid because patching does
not pivot. A wider search must account for candidate generation, freedom
intervals and affected-row work; the accepted-move limit alone does not bound
that work. A failed sweep supplies no infeasibility proof.

The question is therefore broader than choosing a larger constant: which
candidates, progress measure and search budget find a model cheaply enough
to reduce total solving time?

## Sources and what was read

| Source | Reading scope and relevance |
| --- | --- |
| *Rounding and Propagation Heuristics for Mixed Integer Programming*, ZIB Report 11-29 {cite:p}`achterberg2011rounding` | §§2–3: rounding, ZI Round, Shifting and Shift-and-Propagate. These distinguish feasibility-preserving moves from temporary infeasibility and repair. |
| *The Feasibility Pump* {cite:p}`fischetti2005pump` | §1 and Figure 1: alternating rounding and LP distance minimization, with perturbation when progress stalls. The detailed algorithm and experiments focus on binary integer variables. |
| *Heuristic algorithms in global MINLP solvers*, PhD thesis {cite:p}`berthold2014thesis` | §§7.1–7.3, Definition 7.1, Lemma 7.2 and Figure 7.2: RENS and its bounded use within a solver. The chapter became *RENS: the optimal rounding* {cite:p}`berthold2014rens`. |
| *Arithmetic Solving in Z3*, CAV 2024 {cite:p}`BjornerN24` | §2: SMT/MIP design tradeoffs. §4.1, including Example 3: Primo's existing freedom-interval patching basis. The retained {source}`paper note <docs/papers/arithmetic-solving-in-z3.md>` records the broader arithmetic stages. |

### Rounding, shifting and propagation

Achterberg, Berthold and Hendel define directional **locks** by coefficient
signs in $Ax\le b$: positive coefficients restrict upward rounding, negative
ones restrict downward rounding. Their Simple Rounding preserves feasibility;
Rounding can violate a constraint and then try to repair it. ZI Round uses
available row slack to move toward integrality without violating constraints,
even when a full rounding is impossible. Shifting can move already-integral
or continuous variables to repair violated rows. It limits non-improving
steps. Shift-and-Propagate alternates fixings with domain propagation and can
postpone a fixing that causes infeasibility. These methods maintain row
activities, directional information or propagated domains, according to the
chosen method; they avoid solving an LP at each step.

**Inference for Primo.** Feasible partial moves offer a useful comparison
with the current all-or-nothing decrease in fractional-variable count.
However, changing one nonbasic value also changes dependent basic values.
A new score must inspect all affected integer variables. Treating each
tableau equation as two locked inequalities would miss these coordinated
changes. The existing freedom interval is the appropriate feasibility guard.
Strict decrease of a rational fractionality score alone does not imply finite
termination; retain an explicit work limit.

### Feasibility pump

Fischetti, Glover and Lodi keep a rationally feasible point and a rounded
integer point that may violate constraints. They alternate rounding with an
LP minimizing the sum of absolute distances from the integer coordinates.
The LP-feasible point is a model candidate when it becomes integral.
Repeated roundings can stall or cycle, so the binary implementation perturbs
the rounded point and has a time limit. General integer distance objectives
can require auxiliary variables and constraints for absolute values.

**Inference for Primo.** This requires an LP objective and its optimization
procedure, plus separate candidate assignments and perturbation state.
`LraTableau::check` currently supplies a feasibility check, not this distance
minimization. Merely calling it again is not a feasibility pump. Keep this
as a larger experiment after cheaper patching alternatives; an exact SMT
adaptation also needs strict-bound semantics and final model validation.
Repeated LP solves, rather than eight target scores, dominate its work model.

### RENS: search combinations of nearby roundings

Berthold's **relaxation enforced neighborhood search** fixes integer variables
that are already integral and restricts each fractional integer variable to
its floor or ceiling. It solves the resulting subproblem, without requiring
an existing integer solution. With $k$ fractional variables there can be
$2^k$ rounding choices; this is still a hard search problem. Section 7.3
uses fixing-rate checks and node limits to control cost inside a solver.
Lemma 7.2 states that infeasibility of the subproblem rules out roundings
of that point, not all solutions of the original problem.

**Inference for Primo.** A private bounded branch search could explore joint
roundings that a single-move sweep misses. It must discard a failed
neighborhood without reporting a theory conflict: its extra fixings were
heuristic assumptions. It is also not a replacement for farther targets.
In the existing $y=x/2,z=x/3$ example, starting at $(3,3/2,1)$, fixing the
already-integral $x=3$ and $z=1$ makes a RENS neighborhood infeasible, whereas
Primo can find $(6,3,2)$ by moving farther.

## Representation, status and implementation prerequisites

| Candidate | Status and expected Primo ownership | Checklist still needed |
| --- | --- | --- |
| Configurable window; interval-aware or adaptive candidates | **Planned investigation**, within `lia_patching.cpp` and its options/callers. Existing `LraVarId` incidence and exact bounds can be reused while the basis stays fixed. | Specify candidate completeness claims, deterministic order, caps and option semantics. Measure rejected candidates and affected-row work. |
| Fractionality-guided feasible moves | **Planned comparison**, owned by the patching call on its scratch `LraTableau`. | Define a score over all affected integer variables, infinitesimal handling and a finite work budget. Read the chosen method's full algorithm before implementing it. |
| Temporary infeasibility and repair | **Deferred**; needs a separate candidate state and row-violation bookkeeping. | Define restoration and acceptance rules; prevent an infeasible heuristic assignment from entering ordinary completion as a feasible tableau. |
| Feasibility pump | **Deferred**; needs an objective-capable LP interface as well as heuristic state. | Design exact distance minimization, auxiliary lifetimes, perturbation and strict-bound handling. |
| RENS-style restricted search | **Planned comparison**, in private integer-completion state using a bounded search. | Separate neighborhood failure from theory infeasibility; constrain total nodes and preserve the unrestricted fallback. |

The shared stage owner is
{integersource}`complete_integer_assignment <src/theory/arithmetic/lia_completion.cpp#L13-L35>`,
called through the [standalone and combined LIA component routes](../implementation/lia.md#when-integer-reasoning-runs).
A new LIA setting goes into `LiaSettings`, its override, the route policy,
its CLI flag (`main.cpp`) and its strategy parameter
(`lia_strategy_parameters`); see {ref}`lia-settings`. A switch whose
default differs by route takes its `--help` default from
`lia_route_default_text`. SAT continues to receive theory-valid explanations,
not heuristic restrictions. Model acceptance remains with the pure or combined
completion caller.

Algorithm-specific tests should distinguish a missed distant target, a move
that harms another integer row, a fractional-count plateau, joint-rounding
success, and an infeasible neighborhood of a globally satisfiable instance.
Include negative values, fractional bounds and infinitesimals. Compare model
hits, candidates/row visits, LP work or nodes as appropriate, and total solver
time on both SAT and UNSAT inputs. No MIP speedup transfers automatically to
Primo. The replacement lead records the proposed parameter and evaluation
order; implementation, fuzzing and corpus evaluation remain open.


The [patching lead](../leads/lia-patching.md) records the proposed configuration
and comparisons. The {ref}`implementation chapter <lia-patching>` explains
the current feasible-move rule and its worked example.
