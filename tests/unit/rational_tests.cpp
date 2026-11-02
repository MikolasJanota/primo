#ifdef NDEBUG
#undef NDEBUG
#endif

#include <gmpxx.h>

#include <cassert>
#include <cstdint>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "util/rational.hpp"

using primo::util::Rational;
using primo::util::RationalHash;

namespace {

void construction_and_equality() {
  assert(Rational(3) == Rational(3));
  assert(Rational(3) != Rational(4));
  assert(Rational(6, 4) == Rational(3, 2));
  assert(Rational(-6, 4) == Rational(-3, 2));
  assert(Rational(6, -4) == Rational(-3, 2));
}

void arithmetic() {
  assert(Rational(1, 2) + Rational(1, 3) == Rational(5, 6));
  assert(Rational(1, 2) - Rational(1, 3) == Rational(1, 6));
  assert(Rational(2, 3) * Rational(3, 4) == Rational(1, 2));
  assert(Rational(2, 3) / Rational(4, 9) == Rational(3, 2));
  assert(-Rational(2, 3) == Rational(-2, 3));

  Rational accumulator(0);
  accumulator += Rational(1, 2);
  accumulator += Rational(1, 3);
  accumulator += Rational(1, 6);
  assert(accumulator == Rational(1));
}

void ordering() {
  assert(Rational(1, 3) < Rational(1, 2));
  assert(Rational(1, 2) <= Rational(1, 2));
  assert(Rational(2, 3) > Rational(1, 2));
  assert(Rational(1, 2) >= Rational(1, 2));
  assert(!(Rational(1, 2) < Rational(1, 2)));
}

void floor_ceil_is_integer() {
  assert(Rational(3).is_integer());
  assert(!Rational(3, 2).is_integer());
  assert(Rational(3).floor() == Rational(3));
  assert(Rational(3).ceil() == Rational(3));

  assert(Rational(3, 2).floor() == Rational(1));
  assert(Rational(3, 2).ceil() == Rational(2));
  assert(Rational(-3, 2).floor() == Rational(-2));
  assert(Rational(-3, 2).ceil() == Rational(-1));

  assert(Rational(-4, 2).is_integer());
  assert(Rational(-4, 2).floor() == Rational(-2));
  assert(Rational(-4, 2).ceil() == Rational(-2));

  // Forces the big (mpq_class) path -- see representation_transitions()'s
  // use of the same overflow trick.
  const Rational big_integer = Rational(INT64_MAX) * Rational(INT64_MAX);
  assert(big_integer.is_integer());
  assert(big_integer.floor() == big_integer);
  assert(big_integer.ceil() == big_integer);

  const Rational big_fraction = big_integer + Rational(1, 2);
  assert(!big_fraction.is_integer());
  assert(big_fraction.floor() == big_integer);
  assert(big_fraction.ceil() == big_integer + Rational(1));

  const Rational negative_big_fraction = -big_fraction;
  assert(negative_big_fraction.floor() == -(big_integer + Rational(1)));
  assert(negative_big_fraction.ceil() == -big_integer);
}

void gcd_of_integer_valued_rationals() {
  const std::vector<Rational> two_and_neg_two = {Rational(2), Rational(-2)};
  assert(Rational::gcd(two_and_neg_two) == Rational(2));

  const std::vector<Rational> coprime = {Rational(2), Rational(3)};
  assert(Rational::gcd(coprime) == Rational(1));

  const std::vector<Rational> with_zero = {Rational(0), Rational(6), Rational(-9)};
  assert(Rational::gcd(with_zero) == Rational(3));

  const std::vector<Rational> single = {Rational(-7)};
  assert(Rational::gcd(single) == Rational(7));

  // Forces the big (mpq_class) path.
  const Rational big_multiple_of_six = (Rational(INT64_MAX) * Rational(INT64_MAX)) * Rational(6);
  const std::vector<Rational> big_values = {big_multiple_of_six, Rational(6)};
  assert(Rational::gcd(big_values) == Rational(6));

  bool threw = false;
  try {
    const std::vector<Rational> all_zero = {Rational(0), Rational(0)};
    (void)Rational::gcd(all_zero);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  assert(threw);

  threw = false;
  try {
    const std::vector<Rational> has_fraction = {Rational(2), Rational(1, 2)};
    (void)Rational::gcd(has_fraction);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  assert(threw);
}

void zero_and_sign() {
  assert(Rational(0).is_zero());
  assert(Rational(0, 5).is_zero());
  assert(Rational(3).sign() == 1);
  assert(Rational(-3).sign() == -1);
  assert(Rational(0).sign() == 0);
}

void parse_numerals() {
  const auto zero = Rational::parse("0");
  assert(zero.has_value());
  assert(*zero == Rational(0));

  const auto three = Rational::parse("3");
  assert(three.has_value());
  assert(*three == Rational(3));

  const auto big = Rational::parse("123456789012345678901234567890");
  assert(big.has_value());
  // Exceeds int64_t range; this would fail with a fixed-width backing type.
  assert(big->to_string() == "123456789012345678901234567890");
}

void parse_decimals() {
  const auto half = Rational::parse("0.5");
  assert(half.has_value());
  assert(*half == Rational(1, 2));

  const auto one_five = Rational::parse("1.5");
  assert(one_five.has_value());
  assert(*one_five == Rational(3, 2));

  const auto trailing_zeros = Rational::parse("2.500");
  assert(trailing_zeros.has_value());
  assert(*trailing_zeros == Rational(5, 2));
}

void parse_rejects_malformed_input() {
  assert(!Rational::parse("").has_value());
  assert(!Rational::parse(".").has_value());
  assert(!Rational::parse(".5").has_value());
  assert(!Rational::parse("3.").has_value());
  assert(!Rational::parse("-3").has_value());  // Sign handling belongs to the caller.
  assert(!Rational::parse("3.5.5").has_value());
  assert(!Rational::parse("abc").has_value());
}

void to_string_round_trips() {
  assert(Rational(3).to_string() == "3");
  assert(Rational(-3).to_string() == "-3");
  assert(Rational(1, 2).to_string() == "1/2");
  assert(Rational(-1, 2).to_string() == "-1/2");
}

// -- Two-tier representation (docs/qf-lra-heuristics.md, H2) --
//
// Rational stores small values inline as int64/int64 and only falls back to a
// heap mpq_class when a value does not fit, with the invariant that the big
// form is used *if and only if* the small form cannot hold the value. The
// tests below exercise that switch directly, because getting it wrong is
// mostly silent: a missed demotion computes the right number but hashes it
// differently, which would quietly break polynomial hash-consing rather than
// produce a visibly wrong answer.

// A Rational paired with an independently built GMP reference for the same
// value, so every operation can be differentially checked.
struct Checked {
  Rational value;
  mpq_class reference;
};

mpq_class reference_of(std::int64_t numerator, std::int64_t denominator) {
  mpq_class result(mpz_class(static_cast<long>(numerator)),     // NOLINT(google-runtime-int)
                   mpz_class(static_cast<long>(denominator)));  // NOLINT(google-runtime-int)
  result.canonicalize();
  return result;
}

Checked checked(std::int64_t numerator, std::int64_t denominator) {
  return Checked{Rational(numerator, denominator), reference_of(numerator, denominator)};
}

// Builds a value from decimal digit strings, so the reference side never goes
// through the code under test. Used for the values too large for the small
// form.
Checked checked_big(const std::string& numerator, const std::string& denominator, bool negative) {
  const auto parsed_numerator = Rational::parse(numerator);
  const auto parsed_denominator = Rational::parse(denominator);
  assert(parsed_numerator.has_value() && parsed_denominator.has_value());
  Rational value = *parsed_numerator / *parsed_denominator;
  if (negative) {
    value = -value;
  }
  mpq_class reference(mpz_class(numerator, 10), mpz_class(denominator, 10));
  reference.canonicalize();
  if (negative) {
    reference = -reference;
  }
  return Checked{value, reference};
}

void check_matches(const Rational& value, const mpq_class& reference, const char* what) {
  (void)what;
  assert(value.to_string() == reference.get_str());
  assert(value.sign() == sgn(reference));
  assert(value.is_zero() == (sgn(reference) == 0));
}

std::vector<Checked> sample_values() {
  constexpr std::int64_t kMax = INT64_MAX;
  std::vector<Checked> values;

  // Small, boundary, and near-overflow inputs.
  const std::int64_t interesting[] = {0,          1,    -1,    2,        -2,         3,
                                      7,          -7,   1000,  -1000,    2147483647, -2147483647,
                                      4294967296, kMax, -kMax, kMax - 1, 3037000499};
  for (const std::int64_t numerator : interesting) {
    values.push_back(checked(numerator, 1));
    values.push_back(checked(numerator, 3));
    values.push_back(checked(numerator, kMax));
  }

  // Deterministic random pairs across magnitude buckets.
  std::mt19937_64 rng(20260728);
  const std::int64_t buckets[] = {1000, 1LL << 31, 1LL << 62, kMax};
  for (const std::int64_t bound : buckets) {
    for (int i = 0; i < 12; ++i) {
      const auto pick = [&]() {
        const std::int64_t magnitude =
            static_cast<std::int64_t>(rng() % static_cast<std::uint64_t>(bound)) + 1;
        return (rng() % 2) == 0 ? magnitude : -magnitude;
      };
      const std::int64_t denominator = pick();
      values.push_back(checked(pick(), denominator == 0 ? 1 : denominator));
    }
  }

  // Values that only the big form can hold, so the mixed and big/big paths are
  // covered too.
  values.push_back(checked_big("123456789012345678901234567890", "1", false));
  values.push_back(checked_big("123456789012345678901234567890", "1", true));
  values.push_back(checked_big("1", "123456789012345678901234567890", false));
  values.push_back(checked_big("99999999999999999999999999999999999999", "7", false));
  values.push_back(checked_big("340282366920938463463374607431768211456", "3", true));

  return values;
}

// numerator()/denominator() checked against GMP and against their algebraic
// contract, over every sample value and every sum, difference, product and
// quotient of two samples. That covers small values, values at the small-form
// boundary, results that promote to the big form and results that demote back.
void check_parts(const Rational& value, const mpq_class& reference) {
  const Rational numerator = value.numerator();
  const Rational denominator = value.denominator();

  // Exact agreement with an independent GMP computation.
  assert(numerator.to_string() == mpz_class(reference.get_num()).get_str());
  assert(denominator.to_string() == mpz_class(reference.get_den()).get_str());

  // Contract: integer parts, positive denominator, value recovered, reduced.
  assert(numerator.is_integer() && denominator.is_integer());
  assert(denominator > Rational(0));
  assert(numerator / denominator == value);
  assert(numerator.sign() == value.sign());
  assert(value.is_integer() == (denominator == Rational(1)));
  if (!numerator.is_zero()) {
    const std::vector<Rational> parts{numerator, denominator};
    assert(Rational::gcd(parts) == Rational(1));
  } else {
    assert(denominator == Rational(1));
  }
  const Rational remainder = numerator - denominator * (numerator / denominator).floor();
  assert(value - value.floor() == remainder / denominator);

  // Canonical form: equal to, and hashing like, the same integer built
  // independently, so the results are safe to hash-cons.
  const auto rebuilt_numerator = Rational::parse(mpz_class(reference.get_num()).get_str());
  const auto rebuilt_denominator = Rational::parse(mpz_class(reference.get_den()).get_str());
  if (rebuilt_numerator.has_value() && sgn(reference) >= 0) {
    assert(numerator == *rebuilt_numerator);
    assert(RationalHash{}(numerator) == RationalHash{}(*rebuilt_numerator));
  }
  assert(rebuilt_denominator.has_value());
  assert(denominator == *rebuilt_denominator);
  assert(RationalHash{}(denominator) == RationalHash{}(*rebuilt_denominator));
  assert(RationalHash{}(-numerator) == RationalHash{}(Rational(0) - numerator));

  // The results are independent values: changing them leaves the source alone.
  const std::string before = value.to_string();
  Rational mutated_numerator = value.numerator();
  Rational mutated_denominator = value.denominator();
  mutated_numerator += Rational(1);
  mutated_denominator *= Rational(2);
  assert(value.to_string() == before);
  assert(value.numerator() == numerator && value.denominator() == denominator);
}

void numerator_and_denominator_match_gmp() {
  const std::vector<Checked> values = sample_values();
  for (const Checked& left : values) {
    check_parts(left.value, left.reference);
    check_parts(-left.value, mpq_class(-left.reference));
    for (const Checked& right : values) {
      check_parts(left.value + right.value, mpq_class(left.reference + right.reference));
      check_parts(left.value - right.value, mpq_class(left.reference - right.reference));
      check_parts(left.value * right.value, mpq_class(left.reference * right.reference));
      if (sgn(right.reference) != 0) {
        check_parts(left.value / right.value, mpq_class(left.reference / right.reference));
      }
    }
  }

  // Hand-picked cases, including both small-form limits and temporaries.
  assert(Rational(6, -4).numerator() == Rational(-3));
  assert(Rational(6, -4).denominator() == Rational(2));
  assert(Rational(0).numerator().is_zero() && Rational(0).denominator() == Rational(1));
  assert(Rational(INT64_MAX).numerator() == Rational(INT64_MAX));
  assert(Rational(1, INT64_MAX).denominator() == Rational(INT64_MAX));
  assert(Rational(-INT64_MAX, INT64_MAX).numerator() == Rational(-1));
  const Rational square = Rational(INT64_MAX) * Rational(INT64_MAX);
  assert((Rational(1) / square).denominator() == square);
  assert(((square + Rational(1)) / Rational(3)).denominator() == Rational(3));
  // A big quotient that demotes: its parts must be small-form values that
  // compare and hash equal to directly built ones.
  const Rational demoted = (square * Rational(5)) / (square * Rational(7));
  assert(demoted.numerator() == Rational(5) && demoted.denominator() == Rational(7));
  assert(RationalHash{}(demoted.numerator()) == RationalHash{}(Rational(5)));
}

// Differential test against mpq_class over every ordered pair of the sample
// above: small/small (including results that must promote), small/big, and
// big/big (including results that must demote back).
void differential_against_gmp() {
  const std::vector<Checked> values = sample_values();
  for (const Checked& left : values) {
    check_matches(left.value, left.reference, "construction");
    check_matches(-left.value, mpq_class(-left.reference), "negation");

    for (const Checked& right : values) {
      check_matches(left.value + right.value, mpq_class(left.reference + right.reference), "add");
      check_matches(left.value - right.value, mpq_class(left.reference - right.reference), "sub");
      check_matches(left.value * right.value, mpq_class(left.reference * right.reference), "mul");
      if (sgn(right.reference) != 0) {
        check_matches(left.value / right.value, mpq_class(left.reference / right.reference), "div");
      }

      // Compound assignment must agree with the binary form. They delegate
      // today, but that is an implementation detail worth pinning: += is on
      // pivot()'s hot combine path, and *= and /= had no coverage at all
      // before this.
      {
        Rational accumulated = left.value;
        accumulated += right.value;
        check_matches(accumulated, mpq_class(left.reference + right.reference), "add-assign");
        accumulated = left.value;
        accumulated -= right.value;
        check_matches(accumulated, mpq_class(left.reference - right.reference), "sub-assign");
        accumulated = left.value;
        accumulated *= right.value;
        check_matches(accumulated, mpq_class(left.reference * right.reference), "mul-assign");
        if (sgn(right.reference) != 0) {
          accumulated = left.value;
          accumulated /= right.value;
          check_matches(accumulated, mpq_class(left.reference / right.reference), "div-assign");
        }
      }

      assert((left.value < right.value) == (left.reference < right.reference));
      assert((left.value <= right.value) == (left.reference <= right.reference));
      assert((left.value > right.value) == (left.reference > right.reference));
      assert((left.value >= right.value) == (left.reference >= right.reference));
      assert((left.value == right.value) == (left.reference == right.reference));
      assert((left.value != right.value) == (left.reference != right.reference));
    }
  }
}

// One case per transition between the two representations.
void representation_transitions() {
  const Rational max(INT64_MAX);
  const Rational one(1);

  // Small * small that overflows into the big form.
  const Rational promoted = max * max;
  assert(promoted.to_string() == mpq_class(mpz_class(std::to_string(INT64_MAX), 10) *
                                           mpz_class(std::to_string(INT64_MAX), 10))
                                     .get_str());

  // ...and straight back down again: the result fits, so it must be small once
  // more, which is observable as equality plus hash equality with a value that
  // never left the small form.
  const Rational demoted = promoted / max;
  assert(demoted == max);
  assert(RationalHash{}(demoted) == RationalHash{}(max));

  // A denominator that overflows.
  const Rational tiny = Rational(1, INT64_MAX) * Rational(1, INT64_MAX);
  assert(tiny * (max * max) == one);

  // Mixed small/big operands, in both orders.
  const auto big = Rational::parse("123456789012345678901234567890");
  assert(big.has_value());
  assert((*big + Rational(0)) == *big);
  assert((Rational(0) + *big) == *big);
  assert((*big - *big).is_zero());
  assert((*big * Rational(0)).is_zero());

  // INT64_MIN does not fit the small form (its magnitude is one past the
  // largest admissible numerator), so it exercises the promoting path on
  // construction; the value must still be exact.
  const Rational int64_min(INT64_MIN);
  assert(int64_min.to_string() == "-9223372036854775808");
  assert(int64_min + one == Rational(INT64_MIN + 1));
  assert((int64_min - int64_min).is_zero());
  assert(int64_min < Rational(-INT64_MAX));
}

// Equal values must be == and hash equal no matter which path produced them --
// this is what the canonical-form invariant exists for, and what silently
// breaks polynomial hash-consing if demotion is ever dropped.
void canonical_form_is_path_independent() {
  const Rational direct(6, 4);
  const Rational via_arithmetic = Rational(1, 2) + Rational(1);
  const Rational via_big =
      (Rational(3) * Rational(INT64_MAX)) / (Rational(2) * Rational(INT64_MAX));
  assert(direct == via_arithmetic);
  assert(direct == via_big);
  assert(RationalHash{}(direct) == RationalHash{}(via_arithmetic));
  assert(RationalHash{}(direct) == RationalHash{}(via_big));

  const auto parsed = Rational::parse("1.5");
  assert(parsed.has_value());
  assert(*parsed == direct);
  assert(RationalHash{}(*parsed) == RationalHash{}(direct));

  // Same, for a value that genuinely needs the big form: reached by parsing and
  // by arithmetic.
  const auto huge = Rational::parse("123456789012345678901234567890");
  assert(huge.has_value());
  const Rational huge_by_arithmetic =
      *Rational::parse("61728394506172839450617283945") * Rational(2);
  assert(*huge == huge_by_arithmetic);
  assert(RationalHash{}(*huge) == RationalHash{}(huge_by_arithmetic));
}

void promotion_counter_reports_the_fast_path() {
  // The counter is what makes the small form's width an empirically answerable
  // question (docs/qf-lra-heuristics.md's H2 width ablation), so check it
  // actually moves -- and only when it should.
  const std::uint64_t before = Rational::promotion_count();
  for (int i = 0; i < 100; ++i) {
    const Rational sum = Rational(i, 7) + Rational(i, 3);
    assert(!sum.to_string().empty());
  }
  assert(Rational::promotion_count() == before);

  const Rational overflowing = Rational(INT64_MAX) * Rational(INT64_MAX);
  assert(!overflowing.is_zero());
  assert(Rational::promotion_count() > before);
}

// Division by zero and a zero denominator must fail loudly. mpq_class used to
// do that for free (GMP raises); the small form would instead produce a
// canonical-looking value with den_ == 0 and propagate it silently through
// every later operation, which is a far worse failure mode in the numeric
// core. Not reachable from the tableau today -- rows never hold a zero
// coefficient -- but not a thing to leave resting on that.
void division_by_zero_throws() {
  bool threw = false;
  try {
    const Rational quotient = Rational(3) / Rational(0);
    (void)quotient;
  } catch (const std::domain_error&) {
    threw = true;
  }
  assert(threw);

  threw = false;
  try {
    const Rational bad(1, 0);
    (void)bad;
  } catch (const std::domain_error&) {
    threw = true;
  }
  assert(threw);

  // The big path must agree with the small one.
  const auto huge = Rational::parse("123456789012345678901234567890");
  assert(huge.has_value());
  threw = false;
  try {
    const Rational quotient = *huge / Rational(0);
    (void)quotient;
  } catch (const std::domain_error&) {
    threw = true;
  }
  assert(threw);
}

// The small/big switch is the most sensitive thing in this type, so assert
// *which representation path* each operator takes for each combination of
// operand forms -- not only that the value is right. The differential sweep
// above checks values across every operand pairing, and the ASAN build's
// check_invariant() validates the representation of every value it
// constructs; what neither does is pin the transition itself, so a change
// that quietly stopped promoting (or stopped demoting) on one operator would
// still have to be caught by its downstream effects.
//
// Path is observed through the public counters rather than by exposing
// internals: big_operation_count() increments exactly when an operation runs
// on the GMP path (either operand already big), promotion_count() exactly
// when a machine-word computation overflows into the big form.
void representation_paths_are_exercised_for_every_operator() {
  const auto big_a = Rational::parse("123456789012345678901234567890");
  const auto big_b = Rational::parse("987654321098765432109876543210");
  assert(big_a.has_value() && big_b.has_value());
  const Rational small_a(3, 4);
  const Rational small_b(5, 7);

  struct Counters {
    std::uint64_t promotions;
    std::uint64_t big_operations;
  };
  const auto snapshot = []() {
    return Counters{Rational::promotion_count(), Rational::big_operation_count()};
  };
  // `expect_big_path`/`expect_promotion` are tri-state: -1 means "don't care".
  const auto check_path = [&](const Counters& before, int expect_big_path, int expect_promotion) {
    const Counters after = snapshot();
    if (expect_big_path >= 0) {
      assert((after.big_operations > before.big_operations) == (expect_big_path == 1));
    }
    if (expect_promotion >= 0) {
      assert((after.promotions > before.promotions) == (expect_promotion == 1));
    }
  };

  // -- small OP small, staying small: neither counter moves --
  for (int op = 0; op < 4; ++op) {
    const Counters before = snapshot();
    Rational result;
    switch (op) {
      case 0:
        result = small_a + small_b;
        break;
      case 1:
        result = small_a - small_b;
        break;
      case 2:
        result = small_a * small_b;
        break;
      default:
        result = small_a / small_b;
        break;
    }
    assert(!result.to_string().empty());
    check_path(before, /*expect_big_path=*/0, /*expect_promotion=*/0);
  }

  // -- small OP small that overflows: promotes, without taking the GMP
  //    operand path (both operands were small) --
  {
    const Rational max_value(INT64_MAX);
    const Counters before = snapshot();
    const Rational product = max_value * max_value;
    check_path(before, /*expect_big_path=*/0, /*expect_promotion=*/1);
    assert(product > max_value);

    // Addition overflows through the denominator rather than the numerator.
    const Counters before_sum = snapshot();
    const Rational sum = Rational(1, INT64_MAX) + Rational(1, INT64_MAX - 2);
    check_path(before_sum, /*expect_big_path=*/0, /*expect_promotion=*/1);
    assert(sum.sign() == 1);
  }

  // -- mixed and big OP big: the GMP path, in both operand orders --
  for (int op = 0; op < 4; ++op) {
    for (int pairing = 0; pairing < 3; ++pairing) {
      const Rational& left = pairing == 0 ? small_a : *big_a;
      const Rational& right = pairing == 1 ? small_b : *big_b;
      const Counters before = snapshot();
      Rational result;
      switch (op) {
        case 0:
          result = left + right;
          break;
        case 1:
          result = left - right;
          break;
        case 2:
          result = left * right;
          break;
        default:
          result = left / right;
          break;
      }
      assert(!result.to_string().empty());
      check_path(before, /*expect_big_path=*/1, /*expect_promotion=*/-1);
    }
  }

  // -- big OP big collapsing back to small: the demotion path, per operator --
  {
    const Rational difference = *big_a - *big_a;
    assert(difference.is_zero());
    assert(difference == Rational(0));
    assert(RationalHash{}(difference) == RationalHash{}(Rational(0)));

    const Rational quotient = *big_a / *big_a;
    assert(quotient == Rational(1));
    assert(RationalHash{}(quotient) == RationalHash{}(Rational(1)));

    const Rational scaled_back = (*big_a * Rational(2)) / (*big_a * Rational(4));
    assert(scaled_back == Rational(1, 2));
    assert(RationalHash{}(scaled_back) == RationalHash{}(Rational(1, 2)));

    // Addition reaching a small result: big + (-big + small).
    const Rational sum = *big_a + (Rational(0) - *big_a + Rational(7, 3));
    assert(sum == Rational(7, 3));
    assert(RationalHash{}(sum) == RationalHash{}(Rational(7, 3)));
  }

  // -- comparisons across forms must never allocate or change anything --
  {
    const Counters before = snapshot();
    assert(small_a < *big_a);
    assert(*big_a > small_a);
    assert(*big_a != small_a);
    assert(*big_a == *big_a);
    assert(!(*big_a < *big_a));
    assert(small_a <= small_a);
    // Comparison takes neither the promotion nor the big-operation path.
    check_path(before, /*expect_big_path=*/0, /*expect_promotion=*/0);
  }

  // -- unary negation, both forms --
  {
    assert((-small_a) == Rational(-3, 4));
    assert((-(-*big_a)) == *big_a);
    assert((-*big_a).sign() == -1);
  }
}

// parse() and to_string() are the two places a value crosses the boundary
// between text and the two-form representation, and parse() in particular is
// how every coefficient in every benchmark file enters the solver. The
// differential sweep checks to_string() on all its sample values, but nothing
// pinned the round trip per form, or a decimal literal landing in the big
// form -- which is the shape SMT-LIB files actually contain.
void parse_and_to_string_round_trip_across_forms() {
  const char* const cases[] = {
      "0",
      "7",
      "1.5",
      "0.000000000000000000001",              // small numerator, big denominator
      "9223372036854775807",                  // exactly INT64_MAX: still small
      "9223372036854775808",                  // one past: must be big
      "123456789012345678901234567890",       // comfortably big
      "12345678901234567890.12345678901234",  // big through a decimal literal
  };
  for (const char* text : cases) {
    const auto parsed = Rational::parse(text);
    assert(parsed.has_value());

    // Value must match an independently built GMP reference.
    const std::string digits(text);
    const std::size_t dot = digits.find('.');
    mpq_class reference;
    if (dot == std::string::npos) {
      reference = mpq_class(mpz_class(digits, 10));
    } else {
      mpz_class numerator(digits.substr(0, dot) + digits.substr(dot + 1), 10);
      mpz_class denominator(1);
      for (std::size_t i = dot + 1; i < digits.size(); ++i) {
        denominator *= 10;
      }
      reference = mpq_class(numerator, denominator);
      reference.canonicalize();
    }
    assert(parsed->to_string() == reference.get_str());

    // Round trip: to_string() of an integral value must parse back to the
    // same value, in the same form (equality plus hash equality is what
    // "same form" means under the canonical-form invariant).
    if (reference.get_den() == 1) {
      const auto reparsed = Rational::parse(parsed->to_string());
      assert(reparsed.has_value());
      assert(*reparsed == *parsed);
      assert(RationalHash{}(*reparsed) == RationalHash{}(*parsed));
    }
  }

  // A value that fits the small form must parse into it however it is
  // written, so the leading zeros do not smuggle it onto the big path.
  const auto plain = Rational::parse("42");
  const auto padded = Rational::parse("0000000000000000000000000042");
  assert(plain.has_value() && padded.has_value());
  assert(*plain == *padded);
  assert(RationalHash{}(*plain) == RationalHash{}(*padded));
  assert(padded->to_string() == "42");
}

}  // namespace

int main() {
  construction_and_equality();
  arithmetic();
  ordering();
  floor_ceil_is_integer();
  numerator_and_denominator_match_gmp();
  gcd_of_integer_valued_rationals();
  zero_and_sign();
  parse_numerals();
  parse_decimals();
  parse_rejects_malformed_input();
  to_string_round_trips();
  differential_against_gmp();
  representation_transitions();
  canonical_form_is_path_independent();
  promotion_counter_reports_the_fast_path();
  division_by_zero_throws();
  representation_paths_are_exercised_for_every_operator();
  parse_and_to_string_round_trip_across_forms();
  return 0;
}
