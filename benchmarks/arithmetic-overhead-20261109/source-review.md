# Source and efficiency review

Baseline: `75e10877`. Cache source: `cab1ebc4`; CITE experimental source:
`98a5fd8b`. Its test-only capacity reservation was separately verified at
`c8148611`. CITE is rejected after runtime measurement; final production sources
and C++ tests retain the accepted cache version. The CITE analysis below
describes the rejected snapshot, retained in history for reproduction.

No blocking source-review finding remains. Runtime acceptance is a separate
measurement decision recorded in acceptance artifacts when qualification finishes.

The whole tracked non-generated C++ baseline received the 135-unit
clang-analyzer/include-cleaner scan, plus whole-tree lint/check/docs validation.
The focused manual review followed changed owners, callers and statistic
export paths. This is not a claim of manual line-by-line review of the entire
repository or a warning-free static baseline.

- Soundness and invariants: a freedom interval is immutable while the tableau's
  bounds, basis and assignment are unchanged. The owning map is cleared at each
  attempt and immediately after every accepted move. No map iteration changes
  candidate order; feasibility, rejection, scoring and tie-breaking retain
  their original positions. Frozen baseline-linked fixtures compare assignments,
  move prefixes and old counters, including coupled-variable invalidation.
- CITE exactness: sorted endpoint truth agrees for all leaves precisely when
  a monotone inequality is uniform. Equality of a nonsingleton set is uniformly
  false only for an absent constant; membership stays mixed. Empty, absent and
  singleton handling is preserved. Negative/zero scaling is already reflected
  in the sorted complete sets. No leaf representation or merging is changed.
- Borrow/mutation/use: the interval map owns its values; no iterator escapes a
  query. CITE leaf iterators and references are consumed before any term creation.
  Reconstruction snapshots IDs before allocating terms; existing lifetime
  regressions run under ASan. No new borrowed arena storage crosses allocation.
- Layering and API: no SMT-LIB/SAT/theory shortcut or additional public solver
  option is introduced. New fields expose work through existing result/statistic
  types and preserve meanings of existing counters. Includes, helpers, comments,
  formatting and all statistic mappings were inspected.
- Efficiency: the cache bounds computations by distinct requested variables per
  attempt, with retained bucket capacity documented. Balanced CITE probes require
  two uniform-inequality comparisons and logarithmic equality membership work.
  Sorting, complete sets, mixed reconstruction, cache-key comparisons, flattening
  thresholds and search heuristics remain outside these two changes.
- Hygiene: cache adds no static warning identities. CITE's sole new advisory was
  in an added test buffer; reserving capacity removes it. Focused Release/ASan
  tests and normalized warning comparison pass without replacing measured solver
  binaries. Baseline indirect-include and other advisories are retained explicitly;
  paired source/header self-includes are not blindly removed.

Validation job IDs, outcomes and generated/mutated/model coverage are in
`docs/run-log.json`, the gate/static JSON files and immutable per-row summaries.
Local metadata tests exercise missing identities, process errors and model
coverage guards without invoking any solver. The efficiency snapshot helper was
used as an index; its unstaged-only file listing is not treated as review coverage
of staged implementation changes.
