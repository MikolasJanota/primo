# Compact rational storage

**Status: Open lead. Owner: unassigned.**

The layout probe is complete. Lifetime design, implementation, and solver
evaluation remain pending.

Investigate overlapping the small numerator and owned big-number pointer in
`util::Rational`. Keep this separate from changing the arithmetic algorithms
or adopting another rational library.

## Current layout and allocation

{source}`Rational's fields <src/util/rational.hpp#L202-L204>` occupy space
simultaneously: a 64-bit numerator, a 64-bit denominator and a
`std::unique_ptr<mpq_class>`. A null pointer selects the small representation;
those values allocate nothing. Big values use the pointed-to GMP rational.

On the local Linux x86-64 GCC 13.3.0/libstdc++ build, the default-deleter
`unique_ptr` occupies eight bytes, exactly like a raw pointer. It has no
reference count or separately allocated ownership control block. Replacing
it with a raw pointer while retaining the same three fields saves no space.

There is an allocation for the big `mpq_class` object itself, made by the
{source}`big-value constructor <src/util/rational.cpp#L186-L188>`, in addition
to GMP's backing storage for large integers. The
{source}`copy operations <src/util/rational.cpp#L227-L239>` deep-copy that
object; they do not share it. A raw pointer to a separately allocated
`mpq_class` would retain this allocation and indirection while transferring
cleanup responsibility to handwritten code.

## Measured candidate layouts

The {download}`C++ layout probe <../examples/rational-layout.cpp>` reports:

| Record | Inline bytes |
| --- | ---: |
| Current `Rational` | 24 |
| Same fields with a raw pointer | 24 |
| Union of small pair / owned pointer, plus separate tag | 24 |
| Union of numerator / owned pointer, tagged by denominator | 16 |
| Union of small pair / inline `mpq_class`, plus separate tag | 40 |
| Current {source}`DeltaRational <src/theory/arithmetic/lra_tableau.hpp#L24-L56>` / candidate using compact rationals | 48 / 32 |
| Current {source}`LraTableau::RowEntry <src/theory/arithmetic/lra_tableau.hpp#L290-L295>` / candidate using compact rational | 40 / 32 |

The separate-tag union has a 16-byte payload plus its tag and alignment
padding, so it saves nothing here. A smaller candidate uses the denominator
as the discriminator: small values already require `denominator > 0`, so
`denominator == 0` can select a pointer in the overlapping payload:

```text
denominator > 0:  [ denominator ][ numerator          ]
denominator = 0:  [      0      ][ unique_ptr to GMP  ]
```

This uses an unused denominator value, not a pointer bit or a narrower small
number range. It can keep `unique_ptr` ownership. The potential saving is
eight inline bytes per rational, including small values. That is one third
of `Rational`'s inline size and one fifth of the illustrated row entry's size;
it is not a measured reduction in solver memory or runtime.

Embedding `mpq_class` directly in the union would remove the allocation and
indirection for the wrapper object, but its 32-byte size enlarges every value
in this simple tagged layout to 40 bytes. GMP's separately allocated integer
storage remains. The tradeoff depends on the small/big value distribution.

Reproduce the layout observation from the repository root:

```bash
c++ --version
c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror \
  -Isrc -isystem contrib/local/include \
  docs/examples/rational-layout.cpp -o /tmp/primo-rational-layout
/tmp/primo-rational-layout
```

Candidate records are used only with `sizeof`; they are not working arithmetic
types. These sizes depend on the ABI and library implementation. The probe
does not run the solver or measure allocations, cache behavior or peak memory.

## Design and evaluation still needed

Before implementation, specify active-union-member construction and destruction,
copy/move operations, a valid moved-from state, self-assignment, and exception
safety during promotion and copying. A union containing `unique_ptr` needs
explicit lifetime management even though the pointer still owns deletion.
Preserve reduced small fractions, automatic demotion when a big result fits
small, and identical equality/hash behavior regardless of the arithmetic path.

Measure the resulting arrays and owners, not only the scalar type: tableau
rows, infinitesimal values, bounds, undo records, core terms and interning keys
all contain rational values. Coordinate this with the separate
[term-storage lead](term-storage.md). The retained
{source}`D5 library-replacement investigation <docs/qf-lra-heuristics.md>`
addresses a different way of reducing ownership obligations.

A prototype needs focused lifetime and promotion/demotion regressions,
sanitizers, the [solver correctness gates](../development/correctness.md),
and comparisons covering both small-value-heavy and big-value-heavy
inputs. Compare allocation counts, peak/retained memory and total runtime.
Keep the current layout if the ownership complexity or runtime regressions
outweigh the memory saving.

Contributors may use any suitable machine. In the maintainer workspace, these
batches run on SLURM; see the {ref}`execution policy <run-the-applicable-local-checks>`.
