# Yices: architecture, dynamic Ackermannization, and lemma reuse

This survey connects three Yices publications to Primo. The 2006 paper
explains conflict-driven Ackermann clauses; the 2014 paper gives the library
architecture and theory combination; the 2016 talk explains representation,
lazy explanations, and selective retention of theory lemmas. These are
descriptions of the Yices designs at their respective publication dates.

Primo has optional, restricted Ackermann propagation and conflict-selected
clause mechanisms. The latter is an existing-atom subset, described in the
[prototype checklist](../leads/conflict-ackermann-prototype.md) and
[bounded pilot](../evidence/conflict-ackermann.md). It does not implement
the whole policy described in 2006.
For lemma caching, the first question is what CaDiCaL already retains: the
presence of a reason callback alone does not answer that question.

## Sources and reading coverage

| Source | Sections used |
| --- | --- |
| *The Yices SMT Solver*, 2006 {cite:p}`dutertre2006yices` | §2: SAT/core/satellite architecture, equality exchange, dynamic Ackermannization (p. 3); §3: contexts and library use. |
| *Yices 2.2*, CAV 2014 {cite:p}`yices2` | §3: terms, contexts, models; §4: Boolean-aware UF and dynamic clauses; §5: preprocessing and model-based combination. |
| *The Nuts and Bolts of Yices*, SMT Workshop 2016 {cite:p}`dutertre2016yices` | Slides 2–6: architecture/representation; 7–14: DPLL(T), explanations, watches, caching; 15–22: congruence closure; 23–27: preprocessing. |

Slide references use the printed number; PDF page 15 is printed slide 14.

## The 2006 paper: exposing useful congruence to SAT

The problem is that forward congruence closure and Boolean search see different
opportunities. Equality of arguments implies equality of results, but a
disequality of results can also constrain the arguments. A clause makes both
directions available to SAT propagation.

For a unary uninterpreted function, let `A` denote `x = y` and `R` denote
`f(x) = f(y)`. The congruence implication is the theory-valid clause

```text
¬A ∨ R
```

If `R` is false, Boolean propagation can force `¬A`. For two arguments the
clause is `¬A₁ ∨ ¬A₂ ∨ R`: a false result equality requires *at least one*
argument disequality. It does not imply both argument disequalities. Knowing
`A₁` additionally lets SAT force `¬A₂`.

In §2, p. 3, Yices generates a congruence clause when that congruence step
participates in explaining a conflict. This selects useful application pairs
during search. It avoids eagerly encoding every pair of applications of each
function. The paper also permits deleting these additional clauses: the UF
decision procedure still checks consistency, so completeness does not depend
on retaining every shortcut.

The required state includes applications and their arguments, congruence
explanations identifying the participating pair, an atom-to-SAT mapping, and
the resulting clause. A Primo implementation must keep explanation production
in `theory/`, atom creation and scheduling in `smt/`, and clause storage and
watch handling behind `sat/`.

The paper gives no complexity bound for the selection heuristic. Our cost
inference is that eagerly considering `m` applications of one arity-`k`
function exposes up to `m(m−1)/2` pairs with `k+1` clause literals each, before
simplification or sharing. Dynamic generation reduces what is materialized;
it does not prove a better worst-case pair bound.

### What Primo currently implements

| Mechanism | Status and code |
| --- | --- |
| All-pairs cost diagnostics | **Implemented, diagnostic only.** `--euf-ackermann-diagnostics` estimates expansion costs; it does not install the reduction. See the {source}`diagnostic tests <tests/unit/boolean_solver_tests.cpp#L2147-L2200>`. |
| SAT-visible candidate discovery | **Implemented, restricted.** {source}`build_dynamic_ackermann_candidate_index <src/smt/solver_engines.cpp#L965-L1048>` starts from an existing equality between same-symbol applications and requires SAT atoms for every argument equality. It skips missing atoms. |
| Dynamic Ackermann shadow mode | **Implemented, diagnostic only.** `--euf-dynamic-ackermann-shadow` measures candidate states without adding the corresponding clauses. See {source}`shadow tests <tests/unit/boolean_solver_tests.cpp#L2202-L2274>`. |
| Watched argument-disequality propagation | **Implemented subset; off by default.** `--euf-diseq-ackermann` queues existing candidates by affected SAT variables and uses the clause as a propagation reason or conflict. {source}`Watch construction <src/smt/solver_engines.cpp#L2090-L2126>` applies width/per-symbol caps; {source}`candidate evaluation <src/smt/solver_engines.cpp#L2378-L2490>` requires the result equality to be false. |
| Selecting congruence steps from conflicts, then retaining their clauses for reuse | **Implemented bounded prototype; off by default.** {conflictacksource}`ConflictAckermannSelector <src/smt/conflict_ackermann.hpp>` constructs congruence clauses over existing atoms from copied online conflict witnesses; `OnlineEufPropagator` owns and queues the clauses. Missing-atom creation and a new deletion policy remain open; delivery counters do not establish backend retention/reuse. |
| Earlier scan-based dynamic clause experiment | **Not adopted; reverted.** `f83828f` introduced `--euf-dynamic-ackermann`; `160ce4e` reverted it. This old option is not the current `--euf-diseq-ackermann` mechanism. |

For the assignment-indexed propagation modes, `DynamicAckermannCandidate`
owns atom indices and a copied `sat::Clause`;
`OnlineEufPropagator` owns the candidate index, per-variable occurrence lists,
queue, and reasons. Repeated negative literals are removed when constructing
a candidate. Backtracking clears queued work and the pending external clause;
the candidate catalogue belongs to that propagator. These occurrence lists
are SMT-side scheduling data, not CaDiCaL's two watched literals.

The {source}`option defaults <src/smt/check_sat.hpp#L232-L242>` keep the
experimental modes off. The {source}`assumptions-mode admission check
<src/smt/smt_session.cpp#L101-L112>` rejects both shadow and disequality
Ackermann modes. Existing tests cover unary propagation, conflict delivery,
skipping a missing argument atom, and the n-ary disjunction invariant:
{source}`Ackermann regressions <tests/unit/boolean_solver_tests.cpp#L2276-L2389>`.

The rejected scan experiment is recorded as
`2026-07-28-qfuf-dynamic-ackermann-real-f83828f-weak-local` in
{source}`the historical run ledger <docs/run-log.json#L17718-L17829>`.
It inspected about 547 million candidates to add 990 unit clauses on a local
weak manifest. That record is explicitly `needs_revalidation`; it is a warning
about repeated scans, not a current performance comparison or a rejection of
every dynamic Ackermann design.

Other mechanisms in §2 include selective interface equality creation, offset
equalities, simplex/difference logic, and quantified/array reasoning. These
are context for the architecture, not an adoption list. Primo's arithmetic
and combination algorithms need their own primary algorithm notes; this tool
paper alone is not an implementation specification for them.

## The 2014 paper: terms, contexts, and models

The central library lesson in §3 is the separation of three lifetimes. A
shared term/type database supports several configurable contexts. Each context
owns assertion processing and solving state. A returned model is an independent
object that remains usable after its originating context changes or is deleted.
Hash-consing and compact term descriptors reduce duplication in the shared
database. The paper does not establish a particular reference-counting or
garbage-collection algorithm for Primo to adopt.

**Public library design: open lead.** Primo
has {source}`TermManager <src/core/term_manager.hpp>` and
{source}`Model <src/core/model.hpp>`, but its
{source}`SmtSession <src/smt/smt_session.hpp>` is a command-oriented facade.
`Model` owns copied sort/symbol/value data; `ModelEvaluator` also borrows a live
term manager. A public API for shared terms and multiple contexts still needs
explicit ownership rules, covered by the library and incremental-memory leads.

Section 4 explains treating equality itself as a Boolean-valued term in UF.
This permits congruence to transport a false equality through equivalent
endpoints. It also cites dynamic Ackermannization, but provides no replacement
for the 2006 conflict-selection explanation. Section 5 describes symmetry
breaking and model-based theory combination: try to reconcile theory models
before introducing an interface split. Primo's separate design notes
cover {source}`Boolean/equality propagation <docs/qfuf-disequality-propagation.md>`,
{source}`symmetry breaking <docs/papers/smt-symmetry-breaking.md>`, and
{source}`model-based combination <docs/papers/model-based-theory-combination.md>`.

The article gives an architectural overview. Detailed algorithms require the
primary sources cited in those design notes. Arrays, bit-vectors, and the
richer Yices type system are outside this survey's adoption scope.

## The 2016 talk: what lemma caching means

Consider a theory explanation such as

```text
a: x = y       b: y = z       c: x = z
a ∧ b ⇒ c      becomes       ¬a ∨ ¬b ∨ c
```

A theory solver can supply this clause when SAT asks why `c` was propagated.
Conflict analysis may use it as one step in resolution and ultimately learn a
different clause. Using the explanation once does not, by itself, require
storing that exact explanation as a watched SAT clause.

Printed slide 14 proposes retaining selected explanation/conflict clauses in
the SAT clause database. If a later branch sets `a` and `b` true again, the
stored clause can propagate `c` through ordinary Boolean propagation. That can
avoid repeating theory work. This is reuse *inside a solve or surviving solver
context*, not a cache of whole SMT input/output results on disk.

There are three distinct mechanisms:

| Mechanism | What survives and why |
| --- | --- |
| Lazy explanation, slide 11 | A compact reason handle preserves enough information to construct a clause only if it is requested. This saves work on unused explanations. |
| Callback reason storage | The propagator keeps the data needed to answer a pending reason request correctly. Storing a C++ vector does not make it a SAT clause. |
| SAT lemma retention, slide 14 | The Boolean engine keeps a selected theory clause with its normal propagation machinery, allowing reuse on later assignments. |

### Why cache only some lemmas?

Every retained clause costs storage and propagation work. Yices' slide gives
two filters: a configurable upper bound on clause length, and the ability to
install two suitable watches without backtracking just to insert the cached
lemma. The latter concerns optional reuse; it does not license ignoring a
theory conflict that must be reported.

Slides 12–13 explain why insertion during search needs care: the clause's
literals may already be assigned at different decision levels. The stated
watch preference ranks true before unassigned before false literals, preferring
lower levels among true literals and higher levels among false ones. Two
selected non-false literals provide the straightforward watchable case;
unit/conflicting cases may require propagation or backtracking. The slides
are an engineering sketch, not a complete insertion algorithm covering every
case. In Primo, CaDiCaL owns that algorithm.

The talk supplies neither a numerical size threshold nor a cache eviction,
deduplication, or lifetime specification. It gives no asymptotic speedup claim.
Our expected tradeoff is less repeated theory reasoning versus more clause
memory and Boolean work; it needs measurement on Primo.

Forgetting requires a separate progress argument. Being able to regenerate a
deleted theory lemma does not prevent repeatedly encountering the same conflict
after restarts. The [clause-forgetting survey](clause-forgetting.md) gives
papers on logical redundancy, reason lifetime, and sufficient termination
conditions, including a worked distinction between the original conflict
explanation and the clause learned from it.

### What Primo and its backend already do

**Implemented interface/storage; selective Yices policy not established.**
{source}`OnlineEufPropagator::store_reason
<src/smt/solver_engines.cpp#L2192-L2238>` validates a clause against the current
assignment and retains its antecedents. Its {source}`reason callback
<src/smt/solver_engines.cpp#L1863-L1890>` streams the propagated literal and
antecedents when requested. The {source}`LRA reason callback
<src/smt/solver_engines.cpp#L7147-L7179>` similarly streams stored reasons.
Much of this explanation data is already constructed before the callback;
having a callback is not proof of the fully deferred construction on slide 11.

The {source}`CaDiCaL adapter <src/sat/cadical_backend.cpp#L35-L85>` forwards
these callbacks. The installed CaDiCaL 3.0.0 public API,
`contrib/local/include/cadical.hpp`, documents learning external reason
clauses; see its upstream
[reason and external-clause contracts](https://github.com/arminbiere/cadical/blob/rel-3.0.0/src/cadical.hpp#L1248-L1367).
Primo's adapter does not set `are_reasons_forgettable`. The inspected header
initializes that field to false and describes non-forgettable reasons near
the reason callback, but later contains a conflicting statement about the
default and a `reason_forgettable` parameter. Measure the effective backend
configuration before claiming exactly when reasons can be deleted.

For explicit external conflicts, the {source}`EUF callback
<src/smt/solver_engines.cpp#L1892-L1916>` sets `is_forgettable = false` in the
API variant that carries this flag. Establish what the backend already keeps
before designing additional caching. The open questions concern selection,
retention cost, and deletion safety.

## Other ideas retained from the talk

| Slides | Idea and data/invariant | Primo disposition |
| --- | --- | --- |
| 2–4 | Shared term tables and separately allocated descriptors, addressed through integer IDs. | **Layout redesign: open lead.** Primo interns terms, but {source}`Term <src/core/term.hpp#L77-L85>` stores children and several payload fields in every record. Separate descriptors are a compact-storage candidate. |
| 5 | A polarity bit represents negation without allocating an explicit negation node. | **Not adopted at the core-term layer.** Primo has `TermKind::Not`; signed {source}`SAT literals <src/sat/lit.hpp>` are a different layer. |
| 6 | Small exact rationals fall back to GMP when needed. | **Related implemented idea, different representation.** {source}`Rational <src/util/rational.hpp>` uses an inline 64-bit path; the slide uses a 32-bit pair. |
| 10–11 | Phase restrictions on theory callbacks; deferred explanation expansion. | **Partial overlap.** Primo follows its IPASIR-UP contract, not Yices' callback API. Deferred reason streaming and deferred reason construction require separate accounting. |
| 15–22 | Boolean-aware classes, congruence representatives, and parent occurrence vectors. Per-argument hooks locate removable occurrences without searching an entire vector. | **Representation comparison: open lead.** Compare the signed circular class lists and hooked descriptors with {source}`FlatCongruenceClosure <src/theory/equality/flat_congruence_closure.hpp>` and its own paper basis. A change would need rollback invariants and occurrence-maintenance counters. |
| 23–27 | Preserve useful ITE structure: auxiliary terms, finite domains from constant leaves, and flatter path-guard encodings can avoid expansion or unnecessary intermediate terms. | **Further mechanism audit deferred.** Compare the existing {source}`preprocessing <src/smt/preprocessing.cpp>` passes with each transformation before declaring a gap. Constant values must be provably distinct; uninterpreted constants do not automatically have distinct interpretations. Sharing, model reconstruction, and clause growth need explicit checks. |

## Open investigations and acceptance conditions

**Dynamic Ackermannization:** evaluate the next extension beyond the bounded
existing-atom conflict selector, separately from the assignment-indexed
propagator. Specify how absent equality atoms are created and registered,
how growth interacts with existing deduplication/budgets, and whether clauses can be forgotten while
the complete EUF checker remains responsible for consistency. Preserve the
n-ary disjunction and stable term/atom identities across allocation and scope
changes. Do not restart the rejected repeated-scan design without addressing
its cost.

**Lemma retention:** first count explanations built, requested, retained, and
reused, with clause sizes and memory over long incremental sessions. Audit
the effective CaDiCaL reason policy and distinguish explanation vectors from
backend clauses. Only then propose size/reuse-based selection or forgettability
controls supported by the adapter. A theory-valid clause includes its required
antecedents; consequences depending on scoped assertions must preserve those
dependencies or guards. A cache must never turn a branch-local fact into an
unconditional unit.

Before implementing either lead, complete an operation-by-operation design note
covering data owners, callback phases, lifetime/rollback rules, complexity expectations,
and proof obligations. Tests must check missing atoms, n-ary reasoning,
backtracking, repeated checks, scope retirement, and any deletion/rederivation
policy. Counters must distinguish useful reuse from candidate scans, duplicate
clauses, and retained memory. Any solver change needs the applicable fresh
fuzz/model gates and a reproducible baseline comparison in the run ledger.
