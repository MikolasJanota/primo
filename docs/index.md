# Primo

Primo is an open-source SMT solver built with LLM assistance. In our
[published evaluation](papers/primo-qflra-results.md) of quantifier-free linear
real arithmetic (QF_LRA), a tuned Primo configuration outperformed OpenSMT, the
[SMT-COMP 2026 QF_LRA single-query winner](https://smt-comp.github.io/2026/results/qf_lra-single-query/),
with **20 minutes allowed per instance**.

**Correctness comes first:** wrong answers, invalid models and crashes block
acceptance of solver changes. Our goal is to understand every part we build,
with clear documentation of the algorithms, design choices, invariants and
limitations. Contributions that change solver behavior need explanations,
regression tests, fuzzing and reproducible evidence; see
[](development/correctness.md).

Primo is implemented in C++20 from research papers and original design notes.
Its SMT implementation is original; propositional search is delegated to
CaDiCaL through IPASIR-UP. Primo-owned code and documentation use the MIT license.

**[Try Primo](guide/getting-started.md) ·
[Ground-theory comparisons](evidence/ground-comparison.md) ·
[Benchmark details](papers/primo-qflra-results.md) ·
[Read the paper](https://easychair.org/publications/paper/rPZ9) ·
[Source code](https://github.com/primo-solver/primo)**

## Published QF_LRA results

Table 2 of our [2026 paper](https://easychair.org/publications/paper/rPZ9)
reports the following measurements on the **519-instance SMT-COMP 2026 QF_LRA
selection**, with a **1,200-second (20-minute) limit per instance** and
**7 GB memory per solver run**.

| Published configuration | Solved / 519 | PAR2 (seconds; lower is better) |
| --- | ---: | ---: |
| Primo ⋆ (tuned) | **504** | **47,653** |
| OpenSMT (default) | 502 | 54,584 |
| Yices (default) | 495 | 73,077 |

Primo ⋆ solved two more instances than default OpenSMT with approximately
**13% lower PAR2**. PAR2 sums runtimes and charges twice the timeout for each
unsolved instance. These are the paper's measurements; the
[benchmark report](papers/primo-qflra-results.md) describes the measured
configurations, hardware, tuning/test split and comparison with current defaults.

## Explore and contribute

The [clickable documentation map](map.md) follows a query through the solver,
links each area to its explanation and open leads, and suggests starting
points for contributions in documentation, testing, experiments, and code.

## Build and solve a first problem

From a repository checkout, with a C++20 compiler, CMake 3.20+, GNU Make, Git,
Python 3, and a Unix-like shell:

```bash
make build
./build/primo --quiet docs/examples/first-solve.smt2
```

The first build downloads required dependencies into `contrib/local` and needs
network access. Linux is the tested development environment.
The example contains:

```{literalinclude} examples/first-solve.smt2
:language: lisp
```

The answer is `unsat`: equal arguments must give equal results when passed to
the same function, so these assertions contradict each other.

(implemented-now)=
## Implemented

The {source}`command loop <src/main.cpp#L87-L119>` is the executable entry
point; {source}`IpasirUpBackend <src/sat/ipasir_up.hpp#L81-L107>` defines the SAT
boundary.

Primo supports quantifier-free uninterpreted functions, linear Real arithmetic,
linear Int arithmetic, and their supported combinations. Integer search is
bounded and may return `unknown`. The quantified `UF` route supports
enumerative instantiation with limited SAT completion; it does not provide
general quantifier solving or printed models for quantified assertions.
The admitted logic names are checked by
{source}`SmtSession <src/smt/smt_session.cpp#L270-L287>`; quantified assertions
take the {source}`dedicated UF branch <src/smt/solver_session.cpp#L624-L655>`.

The default session engine supports repeated queries and `push`/`pop` by
rebuilding solving state. Opt-in assumptions engines retain more state for
`QF_UF` and `QF_LRA`. Arrays, bit-vectors, nonlinear arithmetic, and proof
production are outside the implemented scope.

The [architecture overview](implementation/architecture.md) explains the
component boundaries; [terms, symbols, and ownership](implementation/core.md)
explains the core representation and its lifetimes.
[Goals, tactics, and strategies](implementation/tactics.md) follows query
composition and engine selection. [Preprocessing](implementation/preprocessing.md)
explains the pass order, profile selection, transformations, and model records.
[Boolean abstraction and SAT integration](implementation/sat.md) follows those
terms into clauses and describes assumptions and theory callbacks. The full
interaction is followed in [DPLL(T)](implementation/dpllt.md), including
explained conflicts, backtracking, and final consistency checks.
The **Theories** section begins with the
[theory solver interface](implementation/theory-interface.md), which explains
the common assignment, explanation, and model contract and maps it to each
solver's current integration path.
[EUF: congruence, explanations, and rollback](implementation/euf.md) follows
the congruence-closure implementation through a conflict and its undo.
[Linear real arithmetic](implementation/lra.md) covers exact numbers, shared
polynomial rows, sparse simplex, strict conflicts and rollback, with links to
the retained arithmetic development and experiments.
[Linear integer arithmetic](implementation/lia.md) follows the real relaxation
through patching, divisibility checks, cuts and branching, including conflict
explanations and resource limits.
[Theory combination](implementation/combination.md) explains purification,
Nelson–Oppen equality exchange, model-guided choices, and the implemented general
component driver used by QF_LIA, QF_UFLIA and quantified UF. QF_UFLRA retains its
dedicated coordinator. [Quantifiers](implementation/quantifiers.md) explains
binding, universal instances, existential witnesses, and the current answer
boundary. [Model construction](implementation/models.md) explains typed values,
function tables, and restoration of expressions removed by preprocessing.

The [capability matrix](capabilities.md) and user guides cover
[building](guide/getting-started.md), [input](guide/input.md),
[models](guide/models.md), and [sessions](guide/sessions.md).
The [reference](reference/options.md) lists options and effective policies.

The [Yices literature survey](papers/yices.md) connects three papers/talks to
the code and distinguishes implemented mechanisms from ideas for investigation.

## Citing Primo

If you use Primo in research, please cite Mikoláš Janota and Jan Jakubův,
*Vibe-Coded and Tuned: A State-of-the-Art SMT Solver for QF-LRA*
{cite:p}`janota2026primo`, *Kalpa Publications in Computing* 23, pages 44–56,
2026. [DOI: 10.29007/rn52](https://doi.org/10.29007/rn52);
[publisher copy](https://easychair.org/publications/paper/rPZ9).
Also report the commit and configuration used in your experiments.

The {download}`BibTeX file <references.bib>` contains the entry
`janota2026primo`. [Reported QF_LRA results](papers/primo-qflra-results.md)
summarizes the paper's benchmark tables. Published configurations and results
are kept separate from current defaults and qualification evidence.

(work-underway)=
## Active work

[Publication preparation](work/publication.md) records the remaining launch
checks and owner decisions. The [closing review](review.md) records the manual's
consistency review; the [preservation audit](coverage/index.md) maps the
historical material. [Evidence work](work/evidence.md) and
[results storage](work/results-storage.md) keep unresolved provenance and the
planned storage migration separate from solver implementation.

## Open leads

The [research register](leads/index.md) links proposed algorithmic and
engineering investigations and records the outcomes of earlier experiments.
It includes arithmetic and combination improvements, quantifier instantiation,
memory reclamation, compact storage, and a public library API. Each lead states
its current implementation boundary and the evidence needed before acceptance.
An open lead is a candidate for investigation, with no implementation commitment.

Build the manual with `make docs`; preview it with `make docs-serve` at
`http://localhost:8000`. The <a href="archive/original/index.html">historical manual</a>
remains available. [Publication preparation](work/publication.md) records launch
checks and outstanding owner decisions.
