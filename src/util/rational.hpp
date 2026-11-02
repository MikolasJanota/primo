#pragma once

#include <gmpxx.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace primo::util {

// Exact rational arithmetic with a machine-word fast path.
//
// Two representations behind one value type:
//
//   * "small": numerator/denominator held inline as std::int64_t, allocating
//     nothing at all;
//   * "big": a heap mpq_class, for values too large for the small form.
//
// This two-tier scheme is the one described by David Monniaux, "On using
// floating-point computations to help an exact linear arithmetic decision
// procedure" (CAV 2009; arXiv:0904.3525), Section 4: "Rational numbers are
// implemented as quotients of two 32-bit numbers; in case of overflow,
// extended precision rationals from the GMP library are used. The reason
// behind a two-tier system is that GMP rationals inherently involve some
// inefficiencies, including dynamic memory allocation, even for small
// numerator and denominator." Only that representation choice is taken from
// the paper -- Primo implements none of the paper's actual contribution (a
// floating-point preprocessing phase feeding a forced-pivoting step).
//
// The same diagnosis was reached independently by profiling Primo: on
// tmp/QF_LRA/DTP-Scheduling/constraints-tempo-width-30.smt2, ~42% of runtime
// was in the allocator and ~12% in GMP copy/init, against ~5% in actual
// bignum arithmetic -- the values are small, the representation was
// expensive. See docs/qf-lra-heuristics.md (item H2) for the full profile.
//
// Deviation from the paper: the small form here is 64-bit rather than
// Monniaux's 32-bit, keeping a wider set of values off the GMP path at the
// cost of __int128 intermediates. kSmallMax below is the single knob, so the
// two widths can be compared empirically rather than argued about.
//
// **Canonical-form invariant**: a Rational is in big form if and only if its
// value does not fit the small form (see fits_small()). The representation is
// therefore a function of the value, never of how the value was reached.
// operator== and RationalHash both rely on this: without it, a value reachable
// both directly and through an overflowing intermediate would have two
// representations, and equal values could hash differently -- which would
// silently break LinearPolynomial/PolynomialKey hash-consing rather than
// produce a visibly wrong answer. Only from_int128() and from_mpq() establish
// the invariant, and nothing else may assign the representation fields.
//
// Kept as Primo's own type (rather than using mpq_class directly at every call
// site) so the GMP dependency stays contained to this file, the same way
// CadicalBackend contains the CaDiCaL dependency.
//
// **Why not let the library do this?** GMP cannot: __mpz_struct holds
// `mp_limb_t *_mp_d`, a pointer to separately allocated limbs, with no inline
// storage, and __mpq_struct is two of those. That layout is frozen public ABI
// -- callers embed mpq_t by value and macros read _mp_size/_mp_d directly --
// so GMP could not add a small-value form without breaking every program
// compiled against it. Even a 1-limb value allocates.
//
// Other libraries *do* provide exactly this scheme, and FLINT's fmpz/fmpq is
// the clearest example: one machine word holding either a small immediate
// value or a reference to a big one. So the real choice was never "hand-roll
// versus use a library" -- it was "keep GMP and add a fast path in front of
// it" versus "adopt a library whose rationals already have one".
//
// Only the first was evaluated. That is an omission, not a conclusion -- no
// measurement says the hand-written path beats fmpq, because none was taken --
// and it is tracked as direction D5 in docs/qf-lra-heuristics.md, ranked there
// as an opportunity rather than a priority. The containment above is what
// keeps it cheap to revisit: every GMP mention is in this file, so swapping
// the backing representation does not touch a call site. The reason to bother
// would be shedding the canonical-form invariant below, which is the kind of
// subtle correctness obligation a library would own instead of us.
class Rational {
 public:
  Rational() = default;
  Rational(std::int64_t value);  // NOLINT(google-explicit-constructor)
  Rational(std::int64_t numerator, std::int64_t denominator);

  Rational(const Rational& other);
  Rational(Rational&& other) noexcept = default;
  Rational& operator=(const Rational& other);
  Rational& operator=(Rational&& other) noexcept = default;
  ~Rational() = default;

  // Parses an SMT-LIB NUMERAL ("3") or DECIMAL ("1.5", "-0.25") token into
  // an exact Rational. Returns std::nullopt if `text` isn't shaped like
  // either (the caller is expected to have already decided `text` is a
  // candidate literal, e.g. by checking its first character).
  static std::optional<Rational> parse(std::string_view text);

  Rational operator+(const Rational& other) const;
  Rational operator-(const Rational& other) const;
  Rational operator*(const Rational& other) const;
  Rational operator/(const Rational& other) const;
  Rational operator-() const;

  Rational& operator+=(const Rational& other);
  Rational& operator-=(const Rational& other);
  Rational& operator*=(const Rational& other);
  Rational& operator/=(const Rational& other);

  bool operator==(const Rational& other) const;
  bool operator!=(const Rational& other) const;
  bool operator<(const Rational& other) const;
  bool operator<=(const Rational& other) const;
  bool operator>(const Rational& other) const;
  bool operator>=(const Rational& other) const;

  bool is_zero() const;
  int sign() const;         // -1, 0, or 1.
  bool is_integer() const;  // true iff the reduced denominator is 1.

  // Largest integer <= this value / smallest integer >= this value (i.e.
  // round toward -infinity / +infinity, not toward zero). For QF_LIA
  // branch-and-bound: the two split points for a fractional integer
  // variable's current value are floor() and floor()+1 (== ceil() when not
  // already an integer).
  Rational floor() const;
  Rational ceil() const;

  // The reduced numerator and the (positive) reduced denominator, as
  // integer-valued Rationals: 6/-4 gives -3 and 2.
  Rational numerator() const;
  Rational denominator() const;

  // Positive exact factor that scales the nonzero input values to coprime
  // integers. Monniaux, CAV 2009, Section 2.1 uses this normalization for
  // linear-arithmetic left-hand sides. Throws when every value is zero.
  static Rational primitive_integer_scale(std::span<const Rational> values);

  // Positive gcd of a set of already-integer-valued Rationals (e.g. a
  // tableau row's coefficients after clearing denominators with
  // primitive_integer_scale -- see the QF_LIA GCD-consistency check in
  // theory/arithmetic/lia_gcd_consistency.cpp). Throws if any value is not
  // integer-valued, or if every value is zero.
  static Rational gcd(std::span<const Rational> values);

  std::string to_string() const;

  // Number of times any Rational left the small form for the big one, in this
  // thread, since the process started. Diagnostic only: it is what makes the
  // small-form width an empirically answerable question rather than a guess
  // (docs/qf-lra-heuristics.md's H2 width ablation), and it is what reveals
  // whether the fast path is actually being taken on a given benchmark.
  static std::uint64_t promotion_count();
  // Operations that ran on the GMP path (either operand already big, or a
  // small-form computation that overflowed).
  static std::uint64_t big_operation_count();

 private:
  // __int128 is a GCC/Clang extension rather than standard C++20, and the
  // repository builds with -Wpedantic -Werror, so the alias is introduced under
  // __extension__ (which is exactly what that keyword is for). Narrowing
  // kSmallMax to INT32_MAX would make every product below fit std::int64_t and
  // remove the need for it -- see the class comment on the width choice.
  __extension__ typedef __int128 Int128;

  static_assert(sizeof(Int128) == 16,
                "Rational's small-form arithmetic needs 128-bit intermediates");
  static_assert(sizeof(unsigned long) == 8,  // NOLINT(google-runtime-int)
                "Rational converts small values to GMP one 64-bit limb at a time");

  // Widest magnitude the small form admits, for both numerator and
  // denominator. INT64_MIN is deliberately excluded so negating a small value
  // can never overflow.
  static constexpr std::int64_t kSmallMax = INT64_MAX;

  explicit Rational(mpq_class value);

  bool is_small() const { return big_ == nullptr; }

  // The two choke points: every operation's result goes through exactly one of
  // these, and each establishes the canonical-form invariant. Nothing else
  // assigns num_/den_/big_. from_int128() is the general entry point (it
  // reduces first); from_int128_reduced() is the hot one, for the arithmetic
  // operators, which reduce in machine words themselves.
  static Rational from_int128_reduced(Int128 numerator, Int128 denominator);
  static Rational from_int128(Int128 numerator, Int128 denominator);
  static Rational from_mpq(mpq_class value);

  // `value`, as an mpq_class, whichever form it is currently in. Used for the
  // mixed small/big operand paths.
  mpq_class as_mpq() const;

  // The single definition of "this value belongs in the small form", shared by
  // from_mpq() and check_invariant() so the two can never drift apart.
  static bool fits_small(const mpq_class& value);

  // Asserts the canonical-form invariant (see the class comment) plus the
  // small form's own canonicity. A no-op under NDEBUG; `make asan-test` runs
  // it across the whole solver rather than only the unit tests, which is the
  // point -- a violation here is silent (lost hash-consing), not a wrong
  // answer, so it needs a check that fires everywhere.
  void check_invariant() const;

  std::int64_t num_ = 0;
  std::int64_t den_ = 1;
  std::unique_ptr<mpq_class> big_;

  friend struct RationalHash;
};

// Hashes the canonical (numerator, denominator) pair so equal Rationals always
// hash equal, for use as a LinearPolynomial coefficient / hash-consing key
// component. Relies on Rational's canonical-form invariant: a value in small
// form is never also representable in big form, so the two branches here can
// never disagree for numerically equal values.
struct RationalHash {
  std::size_t operator()(const Rational& value) const;
};

}  // namespace primo::util
