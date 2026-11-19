# Composing UF symmetry restrictions

**Status: completed correctness repair and completed paired evaluation.**
The guard is implemented and qualified in `59d81fee`; the
[full SMT-COMP comparison](../evidence/euf-performance-fixes.md#completed-120-second-comparison)
checks 8,424 timing rows and all 4,020 selected SAT models. This page preserves
the repair's original proof and implementation checklist. The comparison is
not a whole-branch comparison against `main`; current integration readiness is
tracked in [the merge checklist](../work/merge-readiness.md).

The follow-up exposed `SEQ/SEQ004_size7.smt2`: both frozen `13230b6d` and prior
candidate `6e53a710` returned UNSAT on this SAT input. The repair and a separately
corrected comparator replace those known-wrong experimental identities.

## Paper basis and implemented subset

Déharbe, Fontaine, Merz and Woltzenlogel Paleo, *Exploiting Symmetry in SMT
Problems*, CADE 2011, §3 Theorem 5 and Lemma 6, §4 Algorithm 1 and Theorem 7
{cite:p}`deharbe2011symmetry`, is the primary basis. The relevant sections were
read before preparing this repair. Algorithm 1 changes its working formula as
restrictions are added; the invariance check for each later constant set must
therefore account for the earlier restrictions.

Primo retains its existing finite-domain candidate recognizer, syntactic
cycle/swap invariance test, fixed terms, and value-precedence or weaker exclusion
restrictions. This repair does not introduce graph-automorphism detection,
moving-term restrictions or a new completeness claim.

## Failure and proof obligation

Let $F$ denote the input conjunction, and let $S$ be the conjunction of
restrictions already generated. An eligible constant permutation must preserve
both $F$ and $S$. Checking only $F$ can justify each group separately while their
combined restrictions remove every model. Overlapping sets make this visible,
but mere disjointness of the value sets would not suffice when fixed terms
mention constants from another set.

For a concrete SAT example, take three distinct values $a,b,c$ and a function
$f$ whose values on these constants form a permutation with no fixed points.
The cycle $f(a)=b$, $f(b)=c$, $f(c)=a$ is a model. After forced disequalities
simplify the domains, the candidates use the overlapping value sets
$\{b,c\}$, $\{a,c\}$ and $\{a,b\}$. Independently fixing the first value of each
set can impose incompatible two-cycle or repeated-image requirements.

The repair checks the original assertions as before, then checks the already
generated restriction vector against the same cycle/swap permutations. If both
conjunctions are invariant, so is $F\land S$. For the weaker prefix exclusions, Theorem 5 preserves satisfiability.

The default value-precedence clauses need a separate canonical-renaming argument.
In any model of $F\land S$, scan the fixed terms in their `TermId` order and
label the distinct group values by their first occurrence in that sequence.
Ignore term values outside the group and assign unused labels arbitrarily at
the end. The named group values are distinct, so this is a permutation. Changing
only those constant interpretations leaves every fixed term's value unchanged;
invariance preserves $F\land S$. The first occurrence of label $c_i$ has at
least $i$ earlier distinct occurrences, so it cannot occur before position $i$.
For $i>0$, label $c_{i-1}$ occurs earlier. Consequently the renamed model satisfies
both the prefix exclusions and every guarded precedence clause. This proves
satisfiability preservation for the default mode as well.

Induction over accepted groups therefore establishes that the final conjunction
has a model exactly when the input does, in either mode. The separate
checks are a conservative implementation of Algorithm 1's evolving-formula
precondition; they avoid rescanning the original formula merely to append the
small restriction prefix to it.

## Implementation checklist

- [x] [UfSymmetryBreaker::break_symmetries](https://github.com/primo-solver/primo/blob/59d81fee55ae861ea7fe022b5602450013a57cd0/src/smt/preprocessing.cpp#L5202) owns the
  growing [UfSymmetryBreakingResult::constraints](https://github.com/primo-solver/primo/blob/59d81fee55ae861ea7fe022b5602450013a57cd0/src/smt/preprocessing.hpp#L452) vector of stable `TermId`s.
  Each new group must preserve the conjunction represented by that vector
  before any restriction is appended.
- [x] The invariance helper reads the term manager without mutation. No borrowed
  term node or child range survives the later term-constructing emission calls.
- [x] Record a separate rejected-prior-constraints count, published through a
  [typed statistics key](https://github.com/primo-solver/primo/blob/59d81fee55ae861ea7fe022b5602450013a57cd0/src/util/statistics.hpp#L77),
  [published by the tactic](https://github.com/primo-solver/primo/blob/59d81fee55ae861ea7fe022b5602450013a57cd0/src/smt/preprocessing_tactics.cpp#L426).
  Existing rejection totals retain their broad meaning.
- [x] Exercise both value-precedence and weaker exclusions, plus the default
  preprocessing route that exposes reduced overlapping domains.
- [x] Keep a newly written three-value regression and replay the original
  147,946-byte competition input; validate all successful SAT models separately.
- [x] Build, native/ASan tests, fresh generated and real-seed UF gates, lint,
  static review and documentation qualify the exact repaired source before any
  resumed broad benchmark. The known-wrong old baseline cannot qualify as a
  performance comparator without its own correction or replacement.

The additional guard makes at most one extra invariance check per candidate
that passes the original-formula check, over the restrictions accumulated so
far. This retains the existing canonical-key implementation's allocation and
bit-cost limitations. Reduced symmetry coverage is permitted; removing a SAT
orbit is not.


## Qualification and comparison boundary

The {download}`candidate qualification <../../benchmarks/euf-smtcomp2026-20261118/symmetry-candidate-qualification.json>`
binds the helper-built native binary, source, exact option profiles, audit
receipts and model coverage. Native tests pass all 49 groups and 391 Python
tests; ASan passes all 49 native groups. There are 6,978 passes and 22 timeouts
among 7,000 fresh generated cases across the five ground logics, with UF also
checked under strong preprocessing and the weaker symmetry rule. The 129
regression measurements pass. Real-seed mutation checks pass 2,132 of 2,142
inputs; ten native timeouts remain unsolved. Both previously failing inputs
are included among the UF mutation seeds.

A separate targeted generator checks 3,000 cases across the three UF profiles.
All pass against Z3; the new guard activates on all 1,500 SAT cases. Dolmen
accepts all of those models, all eight focused SAT replays, and all 6,901 SAT
models/queries selected by the broader gates: 8,409 total. No wrong answer,
crash, invalid model or unexplained disagreement was found in this scope.
Whole-tree clang-tidy and include-cleaner each cover 141 translation units;
880 and 407 distinct advisories remain, respectively, with no new normalized
diagnostic signatures relative to `6e53a710` and no unused-include gate failure.
These are completed scans with findings, not a warning-free claim.

The old comparison baseline is known wrong and cannot be reused unchanged.
The {download}`correctness-only comparator patch <../../benchmarks/euf-smtcomp2026-20261118/baseline-correctness.patch>`
applies to `13230b6d`. It adds this guard and its tests/counter, plus the same
mixed-component complete-fact filtering repair already present in the current
candidate. It excludes the EUF mechanical optimizations being measured.
The {download}`comparator qualification <../../benchmarks/euf-smtcomp2026-20261118/baseline-qualification.json>`
records its separate helper build, 49 native groups, 369 Python tests and 49
ASan groups. Fresh broader gates yield 6,979 generated passes, 20 timeouts and
one confirmed integer branch-limit unknown; 129 regression measurements pass.
The 2,151 mutants yield 2,149 passes and two native timeouts. Its three targeted
UF profiles pass all 3,000 cases, and its separate model stage accepts 8,558
SAT models/queries including the eight focused replays. No wrong answer, crash,
invalid model or unexplained disagreement was found in this scope.

The {download}`restarted protocol <../../benchmarks/euf-smtcomp2026-20261118/protocol-repaired.json>`
freezes both corrected native identities for the full selection. This compares
the subsequent EUF changes with the corrected `13230b6d` baseline; it does not
measure the entire branch against `main`. Qualification of this repair does not
certify the full branch for merging.
