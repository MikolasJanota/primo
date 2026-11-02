#include "util/rational.hpp"

#include <gmp.h>

#include <cassert>
#include <cctype>
#include <functional>
#include <span>
#include <stdexcept>
#include <utility>

namespace primo::util {

namespace {

__extension__ typedef unsigned __int128 u128;
__extension__ typedef __int128 i128;

// Diagnostic counters (see Rational::promotion_count). thread_local rather
// than atomic: a solve is single-threaded, and these must not cost anything on
// the fast path.
thread_local std::uint64_t g_promotions = 0;
thread_local std::uint64_t g_big_operations = 0;

bool is_all_digits(std::string_view text) {
  if (text.empty()) {
    return false;
  }
  for (char c : text) {
    if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
      return false;
    }
  }
  return true;
}

std::uint64_t magnitude(std::int64_t value) {
  return value < 0 ? static_cast<std::uint64_t>(-(value + 1)) + 1
                   : static_cast<std::uint64_t>(value);
}

u128 magnitude128(i128 value) {
  return value < 0 ? -static_cast<u128>(value) : static_cast<u128>(value);
}

// Binary GCD (Stein's algorithm) on machine words: no division, which matters
// because every small-form add and multiply below runs one or two of these.
//
// Stein's binary GCD for two odd, nonzero operands, in the branchless form
// GMP uses for mpn_gcd_11 (contrib/src/gmp-6.3.0/mpn/generic/gcd_11.c). The
// textbook loop picks the smaller operand with `if (a > b) swap(a, b)`, and on
// operands of unrelated magnitude that branch is essentially a coin flip, so it
// mispredicts about half the time. Computing a mask from the sign of `u - v`
// and folding min/abs arithmetically removes it entirely -- measured at 1.8x on
// random 16-bit pairs and 2.1x on random 63-bit pairs.
//
// Both operands are shifted right by one first, dropping the low bit that is
// known to be set. That is GMP's trick: it makes the high bit of `t = u - v`
// tell us whether v > u, with no separate comparison.
std::uint64_t gcd_odd(std::uint64_t u, std::uint64_t v) {
  u >>= 1;
  v >>= 1;
  while (u != v) {
    const std::uint64_t t = u - v;
    // All ones when v > u (i.e. t borrowed), zero otherwise. Written as a
    // negated logical shift rather than an arithmetic shift of a signed value,
    // so it does not depend on signed-shift semantics.
    const std::uint64_t v_greater = ~(t >> 63) + 1;
    v += v_greater & t;               // v <- min(u, v)
    u = (t ^ v_greater) - v_greater;  // u <- |u - v|
    const int trailing = __builtin_ctzll(t);
    // The shift by one is independent of `trailing`, so it can issue in
    // parallel with the count rather than waiting to form `trailing + 1`.
    u = (u >> 1) >> trailing;
  }
  return (u << 1) + 1;
}

// The two guards in front of the binary loop are the ones GMP's mpn_gcd_1
// applies, for the same reasons; neither changes the result.
//
// A binary GCD makes progress by subtract-and-shift and never divides, so it
// cannot notice that one argument already divides the other. That is fine on
// operands of similar magnitude and bad on lopsided ones -- and this solver's
// distribution is extremely lopsided. Rational::operator* calls this as
// gcd(|numerator|, denominator), and on a scale-canonicalized tableau the
// denominator is very often exactly 1, so without a guard the loop spends
// O(log n) iterations computing the constant 1.
//
// The `b == 1` exit is deliberately placed *before* the trailing-zero counts
// rather than after, as GMP has it: 1 is odd, so gcd(a, 1) is 1 regardless of
// any common power of two, and the early placement measured 0.7 ns against
// 2.4 ns for the same test after the shifts.
//
// The division is conditional, not unconditional. GMP only divides when the
// operands differ by more than a factor of 2^16 ("if u is much bigger than v,
// reduce using a division rather than chipping away at it bit-by-bit") and that
// tuning holds here too: an unconditional `a %= b` costs more than it saves on
// comparable magnitudes, where the branchless loop is already fast.
//
// std::gcd was benchmarked as an alternative to all of this and is not faster
// than the plain textbook loop, so there was no library form to adopt -- only
// GMP's structure to follow.
//
// docs/papers/word-gcd-notes.md surveys the literature behind all of this. The
// b == 1 pathology in particular is documented (Lemire, 2013) rather than
// novel, and the current speed record for word-adjacent GCD (Bernstein & Yang's
// safegcd) deliberately does NOT apply here: it buys constant time, which
// forbids exactly the input-dependent early exits that help us most.
//
// **These guards are tuned to a measured operand distribution and are not
// universally optimal.** On tiny operands where neither is 1 and the magnitudes
// are comparable -- so all three guards are dead weight and the loop is too
// short for the branchless form to repay them -- this is about 8% SLOWER than
// the plain textbook loop. Every other shape measured is 1.0x to 1.9x faster,
// and QF_LRA (which uses Rational just as heavily, with fractional
// coefficients) came out at 1.002x, i.e. unaffected. Making the choice
// switchable at runtime was measured and rejected: this function inlines into
// operator*, and dispatching through a function pointer costs +175% on the hot
// path to hedge that 8%. tests/bench/word_gcd_bench.cpp re-checks all of it,
// and word-gcd-notes.md records when to re-run it.
std::uint64_t gcd_u64(std::uint64_t a, std::uint64_t b) {
  if (a == 0) {
    return b;
  }
  if (b == 0) {
    return a;
  }
  if (a < b) {
    std::swap(a, b);
  }
  if (b == 1) {
    return 1;
  }
  const int a_trailing = __builtin_ctzll(a);
  const int b_trailing = __builtin_ctzll(b);
  const int shift = a_trailing < b_trailing ? a_trailing : b_trailing;
  a >>= a_trailing;
  b >>= b_trailing;
  // Reachable when the original b was a power of two, which the early exit
  // above cannot see.
  if (b == 1) {
    return 1ULL << shift;
  }
  if ((a >> 16) > b) {
    a %= b;
    if (a == 0) {
      return b << shift;
    }
    a >>= __builtin_ctzll(a);
  }
  return gcd_odd(a, b) << shift;
}

mpz_class to_mpz(i128 value) {
  const bool negative = value < 0;
  const u128 bits = magnitude128(value);
  const u128 low_mask = (~static_cast<u128>(0)) >> 64;
  mpz_class result(static_cast<unsigned long>(bits >> 64));  // NOLINT(google-runtime-int)
  result <<= 64;
  result += mpz_class(static_cast<unsigned long>(bits & low_mask));  // NOLINT(google-runtime-int)
  return negative ? mpz_class(-result) : result;
}

}  // namespace

Rational::Rational(std::int64_t value) {
  // Both bounds have to be checked, not just the lower one: at the default
  // kSmallMax == INT64_MAX only INT64_MIN is out of range, so an upper check
  // looks vacuous -- but kSmallMax is deliberately a knob (see the class
  // comment's note on the small form's width), and narrowing it to INT32_MAX
  // makes plenty of ordinary std::int64_t inputs out of range. Checking one
  // side only let those into the small form and broke the canonical-form
  // invariant; the 32-bit ablation build caught it immediately.
  if (value >= -kSmallMax && value <= kSmallMax) {
    num_ = value;
    den_ = 1;
  } else {
    *this = from_int128(static_cast<i128>(value), 1);
  }
}

Rational::Rational(std::int64_t numerator, std::int64_t denominator)
    : Rational(from_int128(static_cast<i128>(numerator), static_cast<i128>(denominator))) {}

Rational::Rational(mpq_class value) : big_(std::make_unique<mpq_class>(std::move(value))) {
  check_invariant();
}

// The single definition of "fits the small form", shared by from_mpq() (which
// decides) and check_invariant() (which verifies). Keeping one predicate is not
// cosmetic: an earlier version had from_mpq() use mpz_fits_slong_p directly,
// which admits INT64_MIN even though the small form deliberately excludes it
// (so that negation can never overflow) -- the two disagreed by exactly that
// one value. The explicit range comparison also keeps this correct if kSmallMax
// is ever narrowed for the width ablation, where mpz_fits_slong_p would be far
// too permissive.
bool Rational::fits_small(const mpq_class& value) {
  const mpz_srcptr numerator = mpq_numref(value.get_mpq_t());
  const mpz_srcptr denominator = mpq_denref(value.get_mpq_t());
  if (mpz_fits_slong_p(numerator) == 0 || mpz_fits_slong_p(denominator) == 0) {
    return false;
  }
  const std::int64_t small_numerator = mpz_get_si(numerator);
  const std::int64_t small_denominator = mpz_get_si(denominator);
  return small_numerator >= -kSmallMax && small_numerator <= kSmallMax && small_denominator >= 1 &&
         small_denominator <= kSmallMax;
}

void Rational::check_invariant() const {
#ifndef NDEBUG
  if (big_ == nullptr) {
    assert(den_ > 0 && "small-form denominator must be positive");
    assert(num_ >= -kSmallMax && num_ <= kSmallMax && "small-form numerator out of range");
    assert(den_ <= kSmallMax && "small-form denominator out of range");
    assert((num_ != 0 || den_ == 1) && "zero must be canonical 0/1");
    assert((num_ == 0 || gcd_u64(magnitude(num_), static_cast<std::uint64_t>(den_)) == 1) &&
           "small form must be in lowest terms");
    return;
  }
  // Big form is only legal for values the small form cannot hold; otherwise
  // the same value would have two representations and hash two ways.
  assert(!fits_small(*big_) && "big form used for a value that fits the small form");
#endif
}

Rational::Rational(const Rational& other)
    : num_(other.num_),
      den_(other.den_),
      big_(other.big_ == nullptr ? nullptr : std::make_unique<mpq_class>(*other.big_)) {}

Rational& Rational::operator=(const Rational& other) {
  if (this != &other) {
    num_ = other.num_;
    den_ = other.den_;
    big_ = other.big_ == nullptr ? nullptr : std::make_unique<mpq_class>(*other.big_);
  }
  return *this;
}

std::uint64_t Rational::promotion_count() { return g_promotions; }
std::uint64_t Rational::big_operation_count() { return g_big_operations; }

Rational Rational::primitive_integer_scale(std::span<const Rational> values) {
  bool saw_nonzero = false;
  bool all_small_integers = true;
  std::uint64_t small_gcd = 0;
  for (const Rational& value : values) {
    if (value.is_zero()) {
      continue;
    }
    saw_nonzero = true;
    if (!value.is_small() || value.den_ != 1) {
      all_small_integers = false;
      continue;
    }
    small_gcd = gcd_u64(small_gcd, magnitude(value.num_));
  }
  if (!saw_nonzero) {
    throw std::invalid_argument("primitive integer scale needs a nonzero coefficient");
  }
  if (all_small_integers) {
    return Rational(1, static_cast<std::int64_t>(small_gcd));
  }

  mpz_class denominator_lcm = 1;
  for (const Rational& value : values) {
    if (value.is_zero()) {
      continue;
    }
    const mpq_class rational = value.as_mpq();
    mpz_lcm(denominator_lcm.get_mpz_t(), denominator_lcm.get_mpz_t(),
            mpq_denref(rational.get_mpq_t()));
  }

  mpz_class integer_gcd = 0;
  for (const Rational& value : values) {
    if (value.is_zero()) {
      continue;
    }
    const mpq_class rational = value.as_mpq();
    mpz_class integer;
    mpz_set(integer.get_mpz_t(), mpq_numref(rational.get_mpq_t()));
    mpz_class denominator;
    mpz_set(denominator.get_mpz_t(), mpq_denref(rational.get_mpq_t()));
    integer *= denominator_lcm / denominator;
    mpz_gcd(integer_gcd.get_mpz_t(), integer_gcd.get_mpz_t(), integer.get_mpz_t());
  }
  mpq_class scale(denominator_lcm, integer_gcd);
  scale.canonicalize();
  return from_mpq(std::move(scale));
}

Rational Rational::gcd(std::span<const Rational> values) {
  bool saw_nonzero = false;
  bool all_small_integers = true;
  std::uint64_t small_gcd = 0;
  for (const Rational& value : values) {
    if (!value.is_integer()) {
      throw std::invalid_argument("Rational::gcd requires integer-valued inputs");
    }
    if (value.is_zero()) {
      continue;
    }
    saw_nonzero = true;
    if (!value.is_small()) {
      all_small_integers = false;
      continue;
    }
    small_gcd = gcd_u64(small_gcd, magnitude(value.num_));
  }
  if (!saw_nonzero) {
    throw std::invalid_argument("Rational::gcd needs a nonzero value");
  }
  if (all_small_integers) {
    return Rational(static_cast<std::int64_t>(small_gcd));
  }

  mpz_class integer_gcd = 0;
  for (const Rational& value : values) {
    if (value.is_zero()) {
      continue;
    }
    mpz_class integer;
    mpz_set(integer.get_mpz_t(), mpq_numref(value.as_mpq().get_mpq_t()));
    mpz_gcd(integer_gcd.get_mpz_t(), integer_gcd.get_mpz_t(), integer.get_mpz_t());
  }
  return from_mpq(mpq_class(std::move(integer_gcd)));
}

// One of the two choke points establishing the canonical-form invariant (see
// the class comment). Takes an *already reduced* pair, normalizes its sign, and
// picks the small form if and only if it fits -- no gcd here, because the
// arithmetic operators below already reduce in machine words and a 128-bit
// division on their hot path would undo much of the point.
Rational Rational::from_int128_reduced(i128 numerator, i128 denominator) {
  assert(denominator != 0 && "Rational denominator must be non-zero");
  if (denominator < 0) {
    numerator = -numerator;
    denominator = -denominator;
  }
  if (numerator == 0) {
    return Rational();
  }

  if (numerator >= -static_cast<i128>(kSmallMax) && numerator <= static_cast<i128>(kSmallMax) &&
      denominator <= static_cast<i128>(kSmallMax)) {
    Rational result;
    result.num_ = static_cast<std::int64_t>(numerator);
    result.den_ = static_cast<std::int64_t>(denominator);
    result.check_invariant();
    return result;
  }

  ++g_promotions;
  mpq_class value(to_mpz(numerator), to_mpz(denominator));
  value.canonicalize();
  return Rational(std::move(value));
}

// The general entry point, for pairs that are not known to be reduced (the
// two-argument constructor). Euclid rather than the binary gcd above: this is
// not a hot path, and 128-bit shifts/trailing-zero counts would need their own
// helper for no gain.
Rational Rational::from_int128(i128 numerator, i128 denominator) {
  // Checked in release too, for the same reason operator/ is: this is the
  // path the two-argument constructor takes, so a caller-supplied zero
  // denominator would otherwise build a canonical-looking value with
  // den_ == 0. from_int128_reduced below keeps only the assert, because its
  // callers are the arithmetic operators, whose denominators are products of
  // positive denominators by construction -- and it is the hot path.
  if (denominator == 0) {
    throw std::domain_error("Rational denominator must be non-zero");
  }
  u128 a = magnitude128(numerator);
  u128 b = magnitude128(denominator);
  while (b != 0) {
    const u128 remainder = a % b;
    a = b;
    b = remainder;
  }
  if (a > 1) {
    numerator /= static_cast<i128>(a);
    denominator /= static_cast<i128>(a);
  }
  return from_int128_reduced(numerator, denominator);
}

// The other choke point: demotes back to the small form whenever the (already
// canonical) mpq fits it. Demotion is what makes the representation a function
// of the value rather than of the path taken to it -- see the class comment on
// why operator== and RationalHash depend on that.
Rational Rational::from_mpq(mpq_class value) {
  if (fits_small(value)) {
    Rational result;
    result.num_ = static_cast<std::int64_t>(mpz_get_si(mpq_numref(value.get_mpq_t())));
    result.den_ = static_cast<std::int64_t>(mpz_get_si(mpq_denref(value.get_mpq_t())));
    result.check_invariant();
    return result;
  }
  return Rational(std::move(value));
}

mpq_class Rational::as_mpq() const {
  if (big_ != nullptr) {
    return *big_;
  }
  mpq_class result;
  mpz_set_si(mpq_numref(result.get_mpq_t()), num_);
  mpz_set_ui(mpq_denref(result.get_mpq_t()),
             static_cast<unsigned long>(den_));  // NOLINT(google-runtime-int)
  return result;  // Already canonical: num_/den_ are coprime with den_ > 0.
}

std::optional<Rational> Rational::parse(std::string_view text) {
  const std::size_t dot = text.find('.');
  if (dot == std::string_view::npos) {
    if (!is_all_digits(text)) {
      return std::nullopt;
    }
    mpz_class numerator(std::string(text), 10);
    return from_mpq(mpq_class(std::move(numerator)));
  }

  const std::string_view integer_part = text.substr(0, dot);
  const std::string_view fractional_part = text.substr(dot + 1);
  if (!is_all_digits(integer_part) || !is_all_digits(fractional_part)) {
    return std::nullopt;
  }

  mpz_class numerator(std::string(integer_part) + std::string(fractional_part), 10);
  mpz_class denominator(1);
  for (std::size_t i = 0; i < fractional_part.size(); ++i) {
    denominator *= 10;
  }
  mpq_class value(std::move(numerator), std::move(denominator));
  value.canonicalize();
  return from_mpq(std::move(value));
}

// Knuth, TAOCP Vol. 2, Sec. 4.5.1's two-gcd rational addition, in machine
// words. The two gcds are not needed for correctness (see the __int128
// headroom argument below) -- they keep the *result* inside the small form far
// more often than the naive an*bd + bn*ad over ad*bd would, which is the whole
// point of the fast path.
//
// Headroom: both operands are canonical with |num| <= kSmallMax and
// den <= kSmallMax, so every product below is < 2^126 and their sum < 2^127.
// No intermediate can overflow __int128; the only range check needed is on the
// reduced result, inside from_int128's small-form test.
//
// Reducedness: when gcd(ad, bd) == 1 the result is already in lowest terms
// (any prime dividing ad divides neither an nor bd, so it cannot divide
// an*bd + bn*ad), and when it isn't, Knuth's second gcd finishes the job.
Rational Rational::operator+(const Rational& other) const {
  if (!is_small() || !other.is_small()) {
    ++g_big_operations;
    return from_mpq(as_mpq() + other.as_mpq());
  }

  const std::uint64_t common =
      gcd_u64(static_cast<std::uint64_t>(den_), static_cast<std::uint64_t>(other.den_));
  if (common == 1) {
    const i128 numerator =
        static_cast<i128>(num_) * other.den_ + static_cast<i128>(other.num_) * den_;
    const i128 denominator = static_cast<i128>(den_) * other.den_;
    return from_int128_reduced(numerator, denominator);
  }

  const std::int64_t den_reduced = den_ / static_cast<std::int64_t>(common);
  const std::int64_t other_den_reduced = other.den_ / static_cast<std::int64_t>(common);
  const i128 numerator =
      static_cast<i128>(num_) * other_den_reduced + static_cast<i128>(other.num_) * den_reduced;
  const std::uint64_t second =
      gcd_u64(static_cast<std::uint64_t>(magnitude128(numerator) % common), common);
  return from_int128_reduced(
      numerator / static_cast<i128>(second),
      static_cast<i128>(den_reduced) * (other.den_ / static_cast<std::int64_t>(second)));
}

Rational Rational::operator-(const Rational& other) const { return *this + (-other); }

// Cross-reduce before multiplying, for the same reason operator+ uses two
// gcds: it keeps the product inside the small form far more often, and it
// leaves the result already in lowest terms.
Rational Rational::operator*(const Rational& other) const {
  if (!is_small() || !other.is_small()) {
    ++g_big_operations;
    return from_mpq(as_mpq() * other.as_mpq());
  }

  // Both gcds have a positive second argument (denominators are positive), so
  // neither can come back zero even when a numerator is.
  const std::int64_t left =
      static_cast<std::int64_t>(gcd_u64(magnitude(num_), static_cast<std::uint64_t>(other.den_)));
  const std::int64_t right =
      static_cast<std::int64_t>(gcd_u64(magnitude(other.num_), static_cast<std::uint64_t>(den_)));
  const i128 numerator = static_cast<i128>(num_ / left) * (other.num_ / right);
  const i128 denominator = static_cast<i128>(den_ / right) * (other.den_ / left);
  return from_int128_reduced(numerator, denominator);
}

Rational Rational::operator/(const Rational& other) const {
  // Checked in release too, not just asserted. mpq_class used to fail loudly
  // here (GMP raises on a zero denominator); the small form would instead
  // sail through and produce a canonical-looking value with den_ == 0, which
  // every later operation would happily propagate. A silently corrupt value
  // in the numeric core is a much worse failure mode than a thrown
  // exception, and check_sat.cpp's top-level handler already turns
  // std::exception into an Unsupported result rather than a crash. Not
  // reachable from the tableau today -- rows never store a zero coefficient
  // (add_row drops them and pivot()'s combine step skips is_zero() sums) --
  // but "unreachable" is exactly the assumption worth not betting the
  // numeric core on.
  if (other.is_zero()) {
    throw std::domain_error("Rational division by zero");
  }
  if (!is_small() || !other.is_small()) {
    ++g_big_operations;
    return from_mpq(as_mpq() / other.as_mpq());
  }

  const std::int64_t left =
      static_cast<std::int64_t>(gcd_u64(magnitude(num_), magnitude(other.num_)));
  const std::int64_t right = static_cast<std::int64_t>(
      gcd_u64(static_cast<std::uint64_t>(den_), static_cast<std::uint64_t>(other.den_)));
  const i128 numerator = static_cast<i128>(num_ / left) * (other.den_ / right);
  const i128 denominator = static_cast<i128>(den_ / right) * (other.num_ / left);
  // The denominator here carries the divisor's sign; from_int128_reduced
  // normalizes it.
  return from_int128_reduced(numerator, denominator);
}

Rational Rational::operator-() const {
  if (is_small()) {
    Rational result;
    result.num_ = -num_;  // Safe: |num_| <= kSmallMax excludes INT64_MIN.
    result.den_ = den_;
    result.check_invariant();
    return result;
  }
  ++g_big_operations;
  return from_mpq(mpq_class(-*big_));
}

Rational& Rational::operator+=(const Rational& other) { return *this = *this + other; }
Rational& Rational::operator-=(const Rational& other) { return *this = *this - other; }
Rational& Rational::operator*=(const Rational& other) { return *this = *this * other; }
Rational& Rational::operator/=(const Rational& other) { return *this = *this / other; }

bool Rational::operator==(const Rational& other) const {
  // The canonical-form invariant does the work here: a value representable in
  // the small form is never stored in the big one, so a form mismatch already
  // proves inequality without touching GMP.
  if (is_small() != other.is_small()) {
    return false;
  }
  if (is_small()) {
    return num_ == other.num_ && den_ == other.den_;
  }
  return *big_ == *other.big_;
}

bool Rational::operator!=(const Rational& other) const { return !(*this == other); }

bool Rational::operator<(const Rational& other) const {
  if (is_small() && other.is_small()) {
    // Both denominators are positive, so cross-multiplying preserves the
    // ordering; both products are < 2^126 and cannot overflow.
    return static_cast<i128>(num_) * other.den_ < static_cast<i128>(other.num_) * den_;
  }
  return as_mpq() < other.as_mpq();
}

bool Rational::operator<=(const Rational& other) const { return !(other < *this); }
bool Rational::operator>(const Rational& other) const { return other < *this; }
bool Rational::operator>=(const Rational& other) const { return !(*this < other); }

bool Rational::is_zero() const { return is_small() ? num_ == 0 : sgn(*big_) == 0; }

int Rational::sign() const {
  if (is_small()) {
    return (num_ > 0) - (num_ < 0);
  }
  return sgn(*big_);
}

bool Rational::is_integer() const {
  if (is_small()) {
    return den_ == 1;
  }
  return mpz_cmp_ui(mpq_denref(big_->get_mpq_t()), 1) == 0;
}

Rational Rational::numerator() const {
  if (is_small()) {
    return Rational(num_);
  }
  return from_mpq(mpq_class(mpz_class(mpq_numref(big_->get_mpq_t()))));
}

Rational Rational::denominator() const {
  if (is_small()) {
    return Rational(den_);
  }
  return from_mpq(mpq_class(mpz_class(mpq_denref(big_->get_mpq_t()))));
}

Rational Rational::floor() const {
  if (is_small()) {
    if (den_ == 1) {
      return *this;
    }
    std::int64_t quotient = num_ / den_;  // Truncates toward zero.
    if (num_ % den_ != 0 && num_ < 0) {
      --quotient;  // Round toward -infinity instead.
    }
    return Rational(quotient);
  }
  mpz_class quotient;
  mpz_fdiv_q(quotient.get_mpz_t(), mpq_numref(big_->get_mpq_t()), mpq_denref(big_->get_mpq_t()));
  return from_mpq(mpq_class(std::move(quotient)));
}

Rational Rational::ceil() const {
  if (is_small()) {
    if (den_ == 1) {
      return *this;
    }
    std::int64_t quotient = num_ / den_;  // Truncates toward zero.
    if (num_ % den_ != 0 && num_ > 0) {
      ++quotient;  // Round toward +infinity instead.
    }
    return Rational(quotient);
  }
  mpz_class quotient;
  mpz_cdiv_q(quotient.get_mpz_t(), mpq_numref(big_->get_mpq_t()), mpq_denref(big_->get_mpq_t()));
  return from_mpq(mpq_class(std::move(quotient)));
}

std::string Rational::to_string() const {
  // Must match mpq_class::get_str()'s shape exactly: bare integer when the
  // denominator is 1, "numerator/denominator" otherwise.
  if (!is_small()) {
    return big_->get_str();
  }
  if (den_ == 1) {
    return std::to_string(num_);
  }
  return std::to_string(num_) + "/" + std::to_string(den_);
}

std::size_t RationalHash::operator()(const Rational& value) const {
  if (!value.is_small()) {
    return std::hash<std::string>{}(value.big_->get_str());
  }
  // Equal values always share a representation (Rational's canonical-form
  // invariant), so hashing the pair directly can never disagree with the
  // big-form branch above for numerically equal values.
  const std::size_t numerator = std::hash<std::int64_t>{}(value.num_);
  const std::size_t denominator = std::hash<std::int64_t>{}(value.den_);
  return numerator ^ (denominator + 0x9e3779b9 + (numerator << 6) + (numerator >> 2));
}

}  // namespace primo::util
