#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>

#include "sat/phase_probe.hpp"

using primo::sat::PhaseProbe;

namespace {
void cancel(PhaseProbe& probe, int literal) {
  probe.new_level();
  probe.assignment(literal, true);
  probe.backtrack(0);
}

void count_only_context_matching_decisions() {
  PhaseProbe probe({.mode = PhaseProbe::Mode::Release, .threshold = 2, .window = 2});
  probe.hint(1);
  probe.new_level();
  probe.assignment(1, false);
  assert(probe.backtrack(0).empty());
  assert(probe.counts().aligned_removed == 0);
  probe.assume(1);
  cancel(probe, 1);
  assert(probe.counts().assumptions == 1 && probe.counts().activations == 0);
  probe.finish_solve();
  cancel(probe, -1);
  assert(probe.counts().aligned_removed == 0);
  probe.new_level();
  probe.assignment(1, true);
  probe.hint(-1);
  probe.hint(1);
  assert(probe.backtrack(0).empty());
  assert(probe.counts().aligned_removed == 0);
  cancel(probe, 1);
  assert(probe.counts().activations == 0);
  probe.new_level();
  probe.assignment(1, true);
  const auto release = probe.backtrack(0);
  assert(release.size() == 1 && release[0].release && release[0].literal == 1);
  assert(probe.counts().activations == 1);
  probe.new_level();
  assert(probe.assignment(1, true).empty());
  assert(probe.backtrack(0).empty());  // An overridden decision does not retrigger.
  probe.new_level();
  const auto restored = probe.assignment(2, true);
  assert(restored.size() == 1 && !restored[0].release && restored[0].literal == 1);
  assert(probe.counts().restores == 1);
}

void opposite_sign_and_changed_hint() {
  PhaseProbe probe({.mode = PhaseProbe::Mode::Invert, .threshold = 1});
  probe.hint(-2);
  probe.new_level();
  probe.assignment(-2, true);
  const auto opposite = probe.backtrack(0);
  assert(opposite.size() == 1 && opposite[0].literal == 2 && !opposite[0].release);
  const auto changed = probe.hint(2);
  assert(changed.literal == 2 && !changed.release);
  probe.new_level();
  probe.assignment(-2, true);
  assert(probe.backtrack(0).empty());
}

void static_controls_and_solve_reset() {
  PhaseProbe probe({.mode = PhaseProbe::Mode::Release, .threshold = 0});
  assert(probe.hint(3).release);
  assert(probe.finish_solve().size() == 1);
  const auto next = probe.begin_solve();
  assert(next.size() == 1 && next[0].release);
  PhaseProbe counted({.mode = PhaseProbe::Mode::Release, .threshold = 2});
  counted.hint(3);
  cancel(counted, 3);
  counted.finish_solve();
  cancel(counted, 3);
  assert(counted.counts().activations == 0);
  PhaseProbe observe({.mode = PhaseProbe::Mode::Observe, .threshold = 1});
  assert(!observe.hint(1).release);
  cancel(observe, 1);
  cancel(observe, 1);
  assert(observe.counts().activations == 0 && observe.counts().aligned_removed == 2);
}
}  // namespace

int main() {
  count_only_context_matching_decisions();
  opposite_sign_and_changed_hint();
  static_controls_and_solve_reset();
}
