# Explore Primo and find a contribution

Use this map to connect the solver's parts with explanations and open questions.
Each card links to what exists today and a possible investigation. **Open leads
are proposals, not implemented features or assigned tasks.** Read a lead's scope
and evidence before choosing work.

For the supported input boundary, start with the [capability map](capabilities.md).
For a first run, use the [getting-started guide](guide/getting-started.md).

## Follow a query

This follows the default rebuild route for formulas without quantifiers.
The search repeatedly consults theory solvers; it is not a single forward pass. Quantified input has
its own route, shown below. Retained sessions have additional lifetime and
backtracking requirements.

```{raw} html
<nav class="primo-map" aria-label="Solver architecture and contribution map">
  <div class="primo-map-flow">
    <article class="primo-map-card">
      <p class="primo-map-label">1 · Read and manage input</p>
      <h3><a href="implementation/frontend.html">SMT-LIB and sessions</a></h3>
      <p>Build typed terms and maintain assertions across queries and scopes.</p>
      <p><a href="implementation/sessions.html">Understand session behavior</a></p>
      <p class="primo-map-lead">Open lead · <a href="leads/library-api.html">Make Primo embeddable</a></p>
    </article>
    <article class="primo-map-card">
      <p class="primo-map-label">2 · Transform the goal</p>
      <h3><a href="implementation/preprocessing.html">Preprocessing and tactics</a></h3>
      <p>Simplify assertions while preserving the information needed to recover a model.</p>
      <p><a href="implementation/tactics.html">Understand tactic composition</a></p>
      <p class="primo-map-lead">Open lead · <a href="leads/index.html#solver-selection-after-preprocessing">Select engines from surviving features</a></p>
    </article>
    <article class="primo-map-card">
      <p class="primo-map-label">3 · Search and check</p>
      <h3><a href="implementation/dpllt.html">SAT and theory interaction</a></h3>
      <p>CaDiCaL searches Boolean assignments; theory checks supply conflicts and consequences.</p>
      <p><a href="implementation/sat.html">Understand Boolean abstraction</a></p>
      <p class="primo-map-lead">Open lead · <a href="leads/theory-lemmas.html">Study theory-lemma reuse</a></p>
    </article>
    <article class="primo-map-card">
      <p class="primo-map-label">4 · Interpret the result</p>
      <h3><a href="implementation/models.html">Models and reconstruction</a></h3>
      <p>For SAT, assemble interpretations and recover values removed by preprocessing.</p>
      <p><a href="guide/models.html">Request and read a model</a></p>
      <p class="primo-map-lead">Contribution · <a href="development/validation.html">Check models independently</a></p>
    </article>
  </div>
  <p class="primo-map-connector">Stage 3 consults the relevant theory solvers through their <a href="implementation/theory-interface.html">shared interface and route-specific integration</a>.</p>
  <div class="primo-map-grid">
    <article class="primo-map-card">
      <p class="primo-map-label">Equality</p>
      <h3><a href="implementation/euf.html">Uninterpreted functions</a></h3>
      <p>Congruence, explanations, and rollback.</p>
      <p class="primo-map-lead">Open lead · <a href="leads/shared-euf-structure.html">Share immutable EUF structure</a></p>
    </article>
    <article class="primo-map-card">
      <p class="primo-map-label">Arithmetic</p>
      <h3><a href="implementation/lra.html">Real and integer solving</a></h3>
      <p>Exact linear arithmetic; <a href="implementation/lia.html">integer completion</a> adds patching, cuts, and branching.</p>
      <p class="primo-map-lead">Open leads · <a href="leads/lra-floating-point.html">Guide the Real basis</a> · <a href="leads/lia-patching.html">Improve integer patching</a></p>
    </article>
    <article class="primo-map-card">
      <p class="primo-map-label">Interaction between theories</p>
      <h3><a href="implementation/combination.html">Theory combination</a></h3>
      <p>Reconcile equalities and models across supported theory combinations.</p>
      <p class="primo-map-lead">Open leads · <a href="leads/care-pair-generation.html">Reduce equality suggestions</a> · <a href="leads/real-combination-migration.html">Unify the Real driver</a></p>
    </article>
    <article class="primo-map-card">
      <p class="primo-map-label">Separate quantified-UF route</p>
      <h3><a href="implementation/quantifiers.html">Quantifiers and instances</a></h3>
      <p>Generate instances and witnesses, with restricted SAT completion and no printed quantified models.</p>
      <p class="primo-map-lead">Open lead · <a href="leads/quantifier-instantiation.html">Improve instance selection</a></p>
    </article>
  </div>
  <p class="primo-map-connector">All routes depend on term ownership and resource accounting. Improvements here can affect many stages.</p>
  <div class="primo-map-foundations">
    <article class="primo-map-card">
      <p class="primo-map-label">Shared representation</p>
      <h3><a href="implementation/core.html">Terms, symbols, and memory</a></h3>
      <p>Identity, sharing, and lifetimes of expressions.</p>
      <p class="primo-map-lead">Open leads · <a href="leads/term-storage.html">Compact terms</a> · <a href="leads/incremental-memory.html">Reclaim session memory</a></p>
    </article>
    <article class="primo-map-card">
      <p class="primo-map-label">Measurement and control</p>
      <h3><a href="reference/statistics.html">Statistics and resource limits</a></h3>
      <p>Understand counters before using them to explain performance.</p>
      <p class="primo-map-lead">Open lead · <a href="leads/statistics-instrumentation.html">Separate diagnostics from control state</a></p>
    </article>
  </div>
</nav>
```

## Choose a kind of contribution

A useful first contribution can be a focused test, explanation, or measurement.
A solver optimization needs a justified mechanism and fresh correctness and
performance evidence before acceptance.

| If you enjoy… | A concrete starting point | How to proceed |
| --- | --- | --- |
| Explaining difficult ideas | Work through an example in a chapter and identify a missing derivation or ambiguous term. | [Documentation workflow](development/documentation.md) |
| Finding counterexamples | Exercise a boundary such as backtracking, resource limits, or reconstruction after elimination. | [Fuzzing and reduction](development/fuzzing.md) |
| Experimental work | Investigate one bottleneck or an unresolved provenance/checking task. | [Reproducible experiments](development/experiments.md) and [open evidence work](work/evidence.md) |
| Algorithms and proofs | Choose a lead, read its primary sources, and specify invariants and unsupported cases before implementation. | [Research register](leads/index.md) and [literature map](literature.md) |
| APIs and data structures | Audit ownership or prototype an external library consumer before proposing interface changes. | [Library API lead](leads/library-api.md) and [contribution requirements](development/contributing-changes.md) |

The [correctness requirements](development/correctness.md) apply across these
areas. In the maintainer's workspace, solver batches, fuzzing, and benchmarks
belong in SLURM; contributors may use a suitable machine. The
[experiment workflow](development/experiments.md) explains preparation and
recording results. A card is a starting point, not a claim that its investigation
is small or ready for implementation.

## Find the rest of the manual

- **Use the solver:** [input](guide/input.md), [models](guide/models.md), and [sessions](guide/sessions.md).
- **Look up exact behavior:** [options](reference/options.md), [effective policies](reference/policies.md), and [tactic inventory](reference/tactics.md).
- **Understand the design:** [architecture](implementation/architecture.md) and [primary literature](literature.md).
- **Find remaining questions and project work:** [open leads](leads/index.md), [documentation work](work/documentation.md), and [results storage](work/results-storage.md).
- **Assess claims and history:** [completed investigations](evidence/index.md), [documentation review](review.md), and [preservation map](coverage/index.md).
