# Composing UF symmetry restrictions

Status: correctness repair implemented, pending qualification after the full SMT-COMP follow-up stopped on
`SEQ/SEQ004_size7.smt2`. Both frozen `13230b6d` and current `6e53a710` return
UNSAT on this SAT input. Disabling symmetry gives independently validated SAT
models. The older `7dd8874c` binary returns SAT. Performance work is suspended.

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
conjunctions are invariant, so is $F\land S$. The existing fixed-term rule then
preserves satisfiability by Theorem 5. Induction over accepted groups establishes
that the final conjunction has a model exactly when the input does. The separate
checks are a conservative implementation of Algorithm 1's evolving-formula
precondition; they avoid rescanning the original formula merely to append the
small restriction prefix to it.

## Implementation checklist

- [x] `UfSymmetryBreaker::break_symmetries` in `smt/preprocessing.cpp` owns the
  growing `UfSymmetryBreakingResult::constraints` vector of stable `TermId`s.
  Each new group must preserve that vector before any restriction is appended.
- [x] The invariance helper reads the term manager without mutation. No borrowed
  term node or child range survives the later term-constructing emission calls.
- [x] Record a separate rejected-prior-constraints count, published through a
  typed statistics key. Existing rejection totals retain their broad meaning.
- [x] Exercise both value-precedence and weaker exclusions, plus the default
  preprocessing route that exposes reduced overlapping domains.
- [ ] Keep a newly written three-value regression and replay the original
  147,946-byte competition input; validate all successful SAT models separately.
- [ ] Build, native/ASan tests, fresh generated and real-seed UF gates, lint,
  static review and documentation qualify the exact repaired source before any
  resumed broad benchmark. The known-wrong old baseline cannot qualify as a
  performance comparator without its own correction or replacement.

The additional guard makes at most one extra invariance check per candidate
that passes the original-formula check, over the restrictions accumulated so
far. This retains the existing canonical-key implementation's allocation and
bit-cost limitations. Reduced symmetry coverage is permitted; removing a SAT
orbit is not.
