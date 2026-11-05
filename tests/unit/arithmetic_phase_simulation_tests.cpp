#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <optional>
#include <stdexcept>

#include "smt/arithmetic_phase_simulation.hpp"
#include "theory/arithmetic/lia_integer_values.hpp"
#include "util/rational.hpp"

using Probe = primo::smt::ArithmeticPhaseSimulation;
using primo::util::Rational;

namespace {
Probe::Domain range(int lower, int upper) { return {Rational(lower), Rational(upper)}; }

void revisit(Probe& probe) {
  probe.domain(0, range(0, 17));
  probe.domain(0, range(16, 17));
  probe.domain(0, range(0, 17));
  probe.domain(0, range(16, 17));
  probe.domain(0, range(0, 17));
}

int choose(Probe& probe, int beta = 7, bool unassigned = true, bool integer = true,
           int true_literal = 7) {
  return probe.choose(0, 0, true_literal, beta, unassigned, integer, false, Rational(16),
                      Rational(15));
}

void geometric_policies() {
  Probe least({.mode = Probe::Mode::Least});
  least.domain(0, range(0, 17));
  assert(choose(least) == 7);
  revisit(least);
  assert(choose(least) == -7);
  assert(least.counts().repeated == 1);
  assert(least.counts().flips == 1);
  assert(choose(least) == -7);
  assert(least.counts().flips == 1);
  assert(choose(least, 7, false) == 7);
  assert(choose(least, 7, true, false) == 7);

  Probe most({.mode = Probe::Mode::Most});
  revisit(most);
  assert(choose(most, -7) == 7);

  Probe negative({.mode = Probe::Mode::Least});
  revisit(negative);
  assert(choose(negative, -7, true, true, -7) == 7);

  Probe upper({.mode = Probe::Mode::Most});
  upper.domain(0, range(0, 17));
  upper.domain(0, range(0, 1));
  upper.domain(0, range(0, 17));
  upper.domain(0, range(0, 1));
  upper.domain(0, range(0, 17));
  assert(upper.choose(0, 0, 9, -9, true, true, true, Rational(1), Rational(2)) == 9);
}

void bounds_and_history() {
  Probe probe({.mode = Probe::Mode::Least, .history_limit = 2});
  probe.domain(0, {});
  assert(choose(probe) == 7);
  assert(probe.counts().finite_splits == 0);
  probe.domain(0, range(0, 17));
  const auto entries = probe.counts().entries;
  probe.domain(0, range(0, 17));
  assert(probe.counts().entries == entries);
  probe.domain(0, range(16, 17));
  probe.domain(0, range(2, 14));
  assert(probe.history_size() == 2);
  assert(probe.counts().evictions == 1);
  probe.domain(0, range(16, 17));
  assert(choose(probe) == 7);  // the false side has no remaining integer candidate

  using primo::theory::arithmetic::delta_ceil;
  using primo::theory::arithmetic::delta_floor;
  using primo::theory::arithmetic::DeltaRational;
  const DeltaRational lower{Rational(-3), Rational(1)};
  const DeltaRational upper{Rational(2), Rational(-1)};
  const Probe::Domain strict{delta_ceil(lower), delta_floor(upper)};
  assert(strict == range(-2, 1));
}

void seeded_sampling_and_gating() {
  Probe left({.mode = Probe::Mode::Random, .seed = 20261105, .denominator = 4});
  Probe right({.mode = Probe::Mode::Random, .seed = 20261105, .denominator = 4});
  for (int index = 0; index < 100; ++index) {
    left.domain(0, range(0, index + 2));
    right.domain(0, range(0, index + 2));
    assert(choose(left) == choose(right));
    const auto flips = left.counts().flips;
    static_cast<void>(choose(left));
    assert(left.counts().flips == flips);
  }
  assert(left.counts().flips > 0 && left.counts().flips < 100);

  Probe gated({.mode = Probe::Mode::Gated, .denominator = 1});
  gated.domain(0, range(0, 17));
  assert(choose(gated) == 7);
  revisit(gated);
  assert(choose(gated) == -7);
  assert(choose(gated, -7) == -7);  // beta already selects the wider side

  Probe always({.mode = Probe::Mode::Random, .denominator = 1});
  always.domain(0, {});
  assert(choose(always) == -7);
  assert(choose(always, 7, false) == 7);

  bool rejected = false;
  try {
    const Probe invalid({.denominator = 0});
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  assert(rejected);
}
}  // namespace

int main() {
  geometric_policies();
  bounds_and_history();
  seeded_sampling_and_gating();
}
