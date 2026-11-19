# Column-to-row coefficient access

**Status: Open lead. Owner: unassigned.**

Investigate how a walk over a tableau column should obtain the coefficients
it needs. Measurement, design selection, implementation, and evaluation
remain open. This continues the occurrence-index work of
{source}`original lead 3b <docs/open-leads.md#L770-L827>`, which implemented
only the row-to-column half of a cross-linked matrix.

## Current representation

The [LRA chapter](../implementation/arithmetic/lra/tableau.md#sparse-rows-and-reverse-occurrences)
describes the layout. A row is a vector of
{source}`RowEntry {var, coeff, occ_slot} <src/theory/arithmetic/lra_tableau.hpp#L290-L295>`
sorted by variable ID. The column `occurs_in_[j]` is an unordered vector of
basic variables. It records neither the coefficient $a_{ij}$ nor the
position of variable $j$ inside row $i$. The links are therefore asymmetric:

```text
row -> column:  occurs_in_[e.var][e.occ_slot] == b   for each entry e of rows_[b]
column -> row:  none; a_ij must be found inside rows_[i]
```

A column walk yields the rows containing $j$. Any consumer that also needs
$a_{ij}$ must then search row $i$ for it. These consumers do:

| Consumer | Needs $a_{ij}$ for | Lookup in row $i$ |
| --- | --- | --- |
| {source}`update <src/theory/arithmetic/lra_tableau.cpp#L120-L140>` | $\beta(x_i) \mathrel{+}= a_{ij}\,\Delta$ whenever a nonbasic assignment changes, including after each `check` pivot, after a fixed-basic elimination pivot, on an SOI step without a pivot, when a new bound excludes the current value, and from `set_nonbasic_value` | linear scan, stops at $j$ |
| `soi_select`, {source}`breakpoint collection <src/theory/arithmetic/lra_tableau.cpp#L557-L569>` | one breakpoint per row containing $j$ | linear scan, stops at $j$ |
| `soi_select`, {source}`breakpoint crossing <src/theory/arithmetic/lra_tableau.cpp#L645-L656>` | the same coefficient again, for each crossed breakpoint | linear scan, stops at $j$ |
| {source}`freedom_interval_impl <src/theory/arithmetic/lra_tableau.cpp#L1507-L1520>` | the bound on $x_j$ implied by each $x_i$'s bounds | linear `find_if`, stops at $j$ |
| `diversify_shared_model`, {source}`apply lambda <src/theory/arithmetic/lra_tableau.cpp#L1618-L1631>` | scratch copy of `update` | linear scan of the whole row, no early exit |
| {source}`pivot <src/theory/arithmetic/lra_tableau.cpp#L249-L252>` | the entering variable's coefficient in each affected row | `std::lower_bound` |
| {source}`unlink_occurrence <src/theory/arithmetic/lra_tableau.cpp#L2004-L2022>` | the row entry of the swapped column element, to repair its `occ_slot` | `std::lower_bound` |

Four of the five linear lookups cost the position of $j$ in row $i$; the
`apply` lambda costs the whole row. Sorted rows make an $O(\log |row_i|)$
binary search possible, but only `pivot` and `unlink_occurrence` use one. Lead
7's {source}`closing note <docs/open-leads.md#L1212-L1220>` describes
coefficient lookup as `lower_bound`, following Griggio §2.5.2; that holds only
for those two sites. `unlink_occurrence` has three callers: `pivot`'s two
removal sites and `set_soi_direction`, which relinks the SOI objective row.

The other column walks need row membership only. They are the row-propagation
scans in the
{source}`online propagator <src/smt/solver_engines.cpp#L7630-L7640>` and its
{source}`component counterpart <src/smt/solver_engines.cpp#L9408-L9418>`, and
the
{source}`online model-phase dirty marking <src/smt/solver_engines.cpp#L7333-L7336>`
and its
{source}`component counterpart <src/smt/solver_engines.cpp#L9112-L9115>`. A further linear
lookup in `check`'s SOI path, which reads the leaving row's
{source}`coefficient for the entering variable <src/theory/arithmetic/lra_tableau.cpp#L1771-L1777>`,
is not a column walk. It is in scope for design 1 below, but not for designs 2
and 3.

## Relation to the earlier cross-link proposal

Lead 3b's
{source}`regression analysis <docs/open-leads.md#L1065-L1133>` named two
candidate causes for the long-row losses of the `occ_slot` change. One is the
`lower_bound` repair in `unlink_occurrence`. The other is the growth of
`RowEntry` from 32 to 40 bytes, which taxes every row walk in the merge. The
separating experiment, narrowing `LraVarId` to 32 bits, has not been run. The
analysis proposed finishing the cross-link if the repair is the cause: store
the row slot next to the basic variable in `occurs_in_`, making the repair
constant-time. It did not estimate the cost of keeping that slot current.

Positions *in a column* are stable. Swap-and-pop moves one element, and
`pivot`'s merge copies `occ_slot` across unchanged for a surviving term.
Positions *in a row* are not. `pivot` rebuilds every affected row by merging
two sorted rows, and an insertion or cancellation shifts every later entry.
Lead 3b originally rejected the `occ_slot` back-index because it expected
one scattered write per surviving term per rebuild, then withdrew that
objection because the back-index points into the column. For a column-to-row
slot, the objection applies again. Every surviving entry after the first
position at which the merged row differs from the old one needs a write
`occurs_in_[var][occ_slot].row_slot = new_position`. Every merged row has
such a position, because the entering variable always leaves it. `occ_slot`
makes each write constant-time, but the writes are scattered. The fraction of
entries after the first difference has not been measured.

## Candidate designs

The candidates below share one obligation: the search must be unchanged. Each
changes how the coefficient is located, not which coefficient is used or the
order of `occurs_in_`. So the search counters `:lra.pivots`,
`:lra.pivot-term-ops`, `:sat.decisions` and `:sat.conflicts` should be
identical to the baseline. As in lead 3b, that makes every timing difference a
constant factor rather than a different search trajectory.

1. **Binary search at the linear sites.** Replace the linear lookups with
   `std::lower_bound` over the sorted row. In `soi_select`, carry the
   coefficient in the breakpoint record instead of looking it up twice. This
   needs no representation change, no extra memory, and no maintenance. On
   short rows, or when $j$ sits near the front of the row, an early-exit scan
   may beat a binary search; step 3 below measures that. This does not help
   `unlink_occurrence`, which already searches by bisection.
2. **Coefficient in the column.** Store `(basic, coeff)` in `occurs_in_`.
   Lookup becomes constant-time. During the merge, `occ_slot` locates the
   column entry of a changed or new term in constant time. Unchanged terms
   need no write. Costs: a column entry grows from 8 bytes to 8 bytes plus an
   inline `Rational` (24 bytes on the measured build, see
   [compact rational storage](rational-storage.md)); a large value deep-copies
   its GMP rational; and the merge performs an extra coefficient copy per
   changed term. Rational copies were 1.1% of lead 7's tropical profile, but the
   {source}`pivot source comment <src/theory/arithmetic/lra_tableau.cpp#L237-L244>`
   records 465M copy constructions on `scrambled228965`, the largest single
   source being per-row copies that `pivot` no longer makes. The
   coefficient is duplicated, so every producer of a row must keep both copies
   equal, including `pop`, `set_soi_direction` and
   {source}`rebuild_occurs_in_index <src/theory/arithmetic/lra_tableau.cpp#L2024-L2041>`.
   This does not help `unlink_occurrence`.
3. **Row slot in the column.** Store `(basic, row_slot)`. Lookup and the
   `unlink_occurrence` repair both become constant-time. The maintenance cost
   is the scattered refresh described above, paid inside `pivot`'s hottest
   loop. Without a measurement this is the least promising option.
4. **King's doubly linked matrix** (King §2.2.7). Lead 3b did not adopt it:
   it gives up sorted contiguous rows and the two-way merge, and needs a dense
   scratch map for row addition. It was not prototyped.

## Evidence so far

No measurement isolates the cost of the coefficient lookups. Existing profiles
bound it only loosely:

- Lead 7's {source}`gprof profile <docs/open-leads.md#L1222-L1233>` puts
  `LraTableau::update` at 2.1% self time, against 74.1% for `pivot`. The
  register places that profile on tropical
  {source}`constraint-199552 <docs/open-leads.md#L705-L710>`, a long-column
  instance. The `Rational` operators are out-of-line and attributed
  separately, so the 2.1% excludes the rational arithmetic itself. It does not
  bound the cost on long-row instances.
- A {source}`QF_LIA profile <docs/qf-lia-profile.md#L50-L58>` of ten
  medium-hard instances (2026-08-21, after `occ_slot` landed) puts
  `LraTableau::unlink_occurrence` at 5.0% self time, against 5.7% for
  `pivot`. That figure includes the swap-pop and is not split by caller, but
  it is the mechanism design 3 targets. The
  {source}`same profile <docs/qf-lia-profile.md#L72-L80>` attributes 8.0% of
  all `Rational::operator*` calls to `update`. Those calls are the
  $a_{ij}\Delta$ products, not the lookup itself.

The volume of lookup work is roughly the column entries walked times the
position of $j$ in each row. Long rows, as in the latendresse/ecoli family
(691 terms on `ecoliFBAyicesTest-500`, three of the seven regressions in lead
3b's >0.2 s band), raise the second factor. Long columns raise the first:
the register reports tropical rows of about 102 terms during search and
columns averaging 679. Which family carries more lookup cost is a hypothesis
for step 1, not a conclusion.

## Investigation and rejection criteria

1. Add counters for column-walk lookups: lookups performed and row entries
   inspected, separately for `update`, both `soi_select` sites, freedom
   intervals and model mutation. Count `unlink_occurrence` repairs and their
   probes by caller. Profile `ecoliFBAyicesTest-500`, `p6-zenonumeric_s5`,
   tropical `constraint-199552`, and the
   {source}`QF_LRA stratified sample <benchmarks/qf-lra-stratified-sample.txt>`
   on SLURM.
2. The lookups are inline loops, so a profile cannot attribute their time
   directly. Use the combined self time of the consuming functions (`update`,
   `soi_select`, `freedom_interval_impl`, `diversify_shared_model`) as an
   upper bound; check that all four appear as their own symbols, since an
   inlined consumer's time would be charged to its caller. Close this lead
   and record the measurement here if that bound stays below a threshold
   fixed before the run (for example 2%) on each of the three named
   instances and on the stratified sample in aggregate. Otherwise continue.
3. Implement design 1 first. Confirm that the search counters are
   bit-identical on solved files, and evaluate with paired per-file time
   ratios against the unchanged binary, as in lead 3b's cost analysis, not
   with a PAR2 difference.
4. Consider design 2 only if lookups remain above the threshold after
   design 1. Extend
   {source}`occurs_in_index_matches_full_rebuild <src/theory/arithmetic/lra_tableau.cpp#L1967-L2002>`
   to check that each column coefficient equals its row coefficient. Add a
   pop-then-pivot regression and measure memory alongside time. Consider
   design 3 only after lead 3b's `LraVarId` narrowing experiment shows that
   the `unlink_occurrence` repair, not `RowEntry` size, is the remaining cost.

Any representation change (designs 2 and 3) touches the tableau invariant.
It requires `make asan-test` and the QF_LRA model fuzz merge gate on SLURM.
Design 1 also changes solver code, so it requires the merge gate as well,
even though its search is expected to be unchanged. Reject any variant that
changes a search counter or an answer, or produces an invalid model. Also
reject one that regresses the >1 s band of commonly solved files without a
corresponding gain on the instances where step 1 found the lookup cost.

## Sources

| Source | Use here |
| --- | --- |
| Tim King, PhD thesis, NYU, 2014 {cite:p}`king2014`, §2.2.7 | Doubly linked sparse matrix with constant-time unlink; the structure whose column-to-row half Primo lacks. Not adopted. |
| Alberto Griggio, PhD thesis, 2009 {cite:p}`griggio2009`, §2.5.2 | Sorted contiguous coefficient arrays, whose layout Primo's rows follow. |

No paper mechanism is implemented by this lead. Designs 1–3 are
implementation choices within Primo's existing sorted-row representation.
