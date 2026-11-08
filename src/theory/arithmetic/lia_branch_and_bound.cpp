#include "theory/arithmetic/lia_branch_and_bound.hpp"

#include <optional>

#include "sat/lit.hpp"
#include "theory/arithmetic/lia_integer_values.hpp"
#include "util/rational.hpp"

namespace primo::theory::arithmetic {

namespace {

std::optional<LraVarId> find_fractional_variable(const LraTableau& tableau,
                                                 const std::vector<LraVarId>& integer_vars) {
  for (const LraVarId var : integer_vars) {
    if (!is_integer_valued(tableau.assignment(var))) {
      return var;
    }
  }
  return std::nullopt;
}

// Asserts one side of the split (var <= floor_value, or var >=
// floor_value + 1) and checks it. Returns whether that side is feasible;
// on false, whatever the assert/check touched is left in place for the
// caller to pop().
//
// Every conflict that closes a side is folded into `explanation` when one
// is supplied. The split bound itself is asserted with
// kDerivedBoundReason and so drops out of that explanation, which is
// exactly right: `var <= floor_value` or `var >= floor_value + 1` is a
// tautology over the integers, so once branch_and_bound has closed *both*
// sides, the union of the two sides' explanations refutes the parent node
// without needing to mention the split. See lia_explanation.hpp.
bool try_branch(LraTableau& tableau, LraVarId var, const util::Rational& floor_value, bool low_side,
                LiaExplanation* explanation) {
  const std::optional<std::vector<sat::Lit>> assert_conflict =
      low_side
          ? tableau.assert_upper(var, DeltaRational::from_rational(floor_value),
                                 kDerivedBoundReason)
          : tableau.assert_lower(var, DeltaRational::from_rational(floor_value + util::Rational(1)),
                                 kDerivedBoundReason);
  if (assert_conflict.has_value()) {
    if (explanation != nullptr) {
      explanation->add_all(*assert_conflict);
    }
    return false;
  }
  const std::optional<std::vector<sat::Lit>> check_conflict = tableau.check();
  if (check_conflict.has_value()) {
    if (explanation != nullptr) {
      explanation->add_all(*check_conflict);
    }
    return false;
  }
  return true;
}

}  // namespace

LiaBranchAndBoundResult branch_and_bound(LraTableau& tableau,
                                         const std::vector<LraVarId>& integer_vars,
                                         std::size_t node_limit, LiaExplanation* explanation) {
  struct Frame {
    LraVarId var = 0;
    util::Rational floor_value;
    std::size_t checkpoint = 0;
    bool tried_high = false;
  };
  std::vector<Frame> stack;
  const std::size_t entry_checkpoint = tableau.push();
  std::size_t nodes = 0;

  while (true) {
    const std::optional<LraVarId> fractional = find_fractional_variable(tableau, integer_vars);
    if (!fractional.has_value()) {
      return LiaBranchAndBoundResult{LiaBranchAndBoundResult::Status::Sat, nodes};
    }
    if (nodes >= node_limit) {
      tableau.pop(entry_checkpoint);
      return LiaBranchAndBoundResult{LiaBranchAndBoundResult::Status::LimitReached, nodes};
    }
    ++nodes;

    Frame frame;
    frame.var = *fractional;
    frame.floor_value = delta_floor(tableau.assignment(*fractional));
    frame.checkpoint = tableau.push();
    stack.push_back(frame);

    if (try_branch(tableau, frame.var, frame.floor_value, /*low_side=*/true, explanation)) {
      continue;  // Descend: re-scan for the next fractional variable.
    }

    // The low side of the frame just pushed failed. Unwind the stack,
    // trying each frame's untried high side in turn, until one succeeds
    // (resume descending from there) or the stack empties (Unsat).
    bool resumed = false;
    while (!stack.empty()) {
      Frame& top = stack.back();
      if (!top.tried_high) {
        tableau.pop(top.checkpoint);
        top.checkpoint = tableau.push();
        top.tried_high = true;
        if (try_branch(tableau, top.var, top.floor_value, /*low_side=*/false, explanation)) {
          resumed = true;
          break;
        }
      }
      tableau.pop(top.checkpoint);
      stack.pop_back();
    }
    if (!resumed) {
      return LiaBranchAndBoundResult{LiaBranchAndBoundResult::Status::Unsat, nodes};
    }
  }
}

}  // namespace primo::theory::arithmetic
