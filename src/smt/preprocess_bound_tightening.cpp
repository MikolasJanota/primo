#include "smt/preprocess_bound_tightening.hpp"

#include <chrono>
#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "core/term.hpp"
#include "core/term_manager.hpp"
#include "smt/inconclusive_reason.hpp"
#include "smt/strategy.hpp"
#include "smt/tactic.hpp"
#include "util/rational.hpp"
#include "util/statistics.hpp"

namespace primo::smt {
namespace {

using core::TermId;
using util::Rational;
struct Endpoint {
  Rational value;
  bool strict = false;
};
struct Interval {
  std::optional<Endpoint> lower;
  std::optional<Endpoint> upper;
};
struct Polynomial {
  Rational constant{0};
  std::map<TermId, Rational> coefficients;
};
struct Constraint {
  std::vector<std::pair<TermId, Rational>> entries;
  Rational rhs;
  bool strict;
  bool equality;
};

bool stronger(const Endpoint& value, const std::optional<Endpoint>& old, bool upper) {
  return !old || (upper ? value.value < old->value : old->value < value.value) ||
         (value.value == old->value && value.strict && !old->strict);
}

class Analysis {
 public:
  Analysis(const core::TermManager& terms, std::size_t limit,
           const std::function<bool()>& cancelled)
      : terms_(terms), limit_(limit), cancelled_(cancelled) {}

  PreprocessBoundResult run(std::span<const TermId> assertions) {
    collect(assertions);
    const auto initial = intervals_;
    if (!stopped() && !result_.contradiction) propagate();
    if (result_.contradiction || result_.cancelled) return std::move(result_);
    // Ordered map gives stable emission, lower before upper. Derived premises
    // need not themselves be emitted: every endpoint follows from the input.
    for (const auto& [term, interval] : intervals_) {
      if (cancelled_ && cancelled_()) {
        result_.cancelled = true;
        result_.bounds.clear();
        result_.newly_finite = 0;
        return std::move(result_);
      }
      const auto found = initial.find(term);
      for (const bool upper : {false, true}) {
        const auto& value = upper ? interval.upper : interval.lower;
        const auto old = found == initial.end()
                             ? std::optional<Endpoint>{}
                             : (upper ? found->second.upper : found->second.lower);
        if (!value || !stronger(*value, old, upper)) continue;
        if (result_.bounds.size() == 4096) {
          ++result_.emission_truncated;
          continue;
        }
        result_.bounds.push_back({term, value->value, upper, value->strict});
        if (!old) ++result_.newly_finite;
      }
    }
    return std::move(result_);
  }

 private:
  bool stopped() {
    if (cancelled_ && cancelled_()) result_.cancelled = true;
    return result_.cancelled || result_.exhausted;
  }
  bool charge() {
    if (stopped()) return false;
    if (result_.work == limit_) {
      result_.exhausted = true;
      return false;
    }
    ++result_.work;
    return true;
  }
  void add(Polynomial& into, TermId term, const Rational& coefficient) {
    if (coefficient.is_zero()) return;
    into.coefficients[term] += coefficient;
    if (into.coefficients.at(term).is_zero()) into.coefficients.erase(term);
  }

  // Iterative, memoized DAG normalization. No manager mutation. Cache entries
  // are published only when their charged calculation is complete.
  const Polynomial* normalize(TermId root) {
    std::vector<std::pair<TermId, bool>> stack{{root, false}};
    while (!stack.empty()) {
      const auto [id, expanded] = stack.back();
      stack.pop_back();
      if (polynomials_.contains(id)) continue;
      if (!charge()) return nullptr;
      const auto& node = terms_.get(id);
      if (!expanded && (node.kind == core::TermKind::Plus || node.kind == core::TermKind::Times)) {
        stack.emplace_back(id, true);
        for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
          if (!charge()) return nullptr;
          stack.emplace_back(*it, false);
        }
        continue;
      }
      Polynomial polynomial;
      if (node.kind == core::TermKind::Rational) {
        polynomial.constant = node.rational_value;
      } else if (node.kind == core::TermKind::Constant ||
                 node.kind == core::TermKind::Application) {
        polynomial.coefficients.emplace(id, Rational(1));
      } else if (node.kind == core::TermKind::Plus || node.kind == core::TermKind::Times) {
        const Rational factor =
            node.kind == core::TermKind::Times ? node.rational_value : Rational(1);
        bool supported = true;
        for (const auto child : node.children) {
          if (!charge()) return nullptr;
          const auto& normalized = polynomials_.at(child);
          if (!normalized) {
            supported = false;
            break;
          }
          polynomial.constant += normalized->constant * factor;
          for (const auto& [term, coefficient] : normalized->coefficients) {
            if (!charge()) return nullptr;
            add(polynomial, term, coefficient * factor);
          }
        }
        if (!supported) {
          polynomials_.emplace(id, std::nullopt);
          continue;
        }
      } else {
        polynomials_.emplace(id, std::nullopt);
        continue;
      }
      polynomials_.emplace(id, std::move(polynomial));
    }
    const auto& value = polynomials_.at(root);
    return value ? &*value : nullptr;
  }

  Endpoint rounded(TermId term, Endpoint value, bool upper) const {
    if (terms_.get(term).sort != terms_.symbols().int_sort()) return value;
    Rational result = upper ? value.value.floor() : value.value.ceil();
    if (value.strict && value.value.is_integer()) result += Rational(upper ? -1 : 1);
    return Endpoint{std::move(result), false};
  }
  bool update(TermId term, Endpoint value, bool upper) {
    value = rounded(term, std::move(value), upper);
    auto& interval = intervals_[term];
    auto& old = upper ? interval.upper : interval.lower;
    if (!stronger(value, old, upper)) return false;
    old = std::move(value);
    ++result_.endpoint_updates;
    if (interval.lower && interval.upper &&
        (interval.lower->value > interval.upper->value ||
         (interval.lower->value == interval.upper->value &&
          (interval.lower->strict || interval.upper->strict))))
      result_.contradiction = true;
    return true;
  }

  void collect(std::span<const TermId> assertions) {
    std::vector<std::pair<TermId, bool>> pending;
    for (auto it = assertions.rbegin(); it != assertions.rend(); ++it)
      pending.emplace_back(*it, false);
    std::set<std::pair<TermId, bool>> seen;
    while (!pending.empty() && !result_.contradiction) {
      if (!charge()) return;
      const auto item = pending.back();
      pending.pop_back();
      if (!seen.insert(item).second) continue;
      const auto [id, negative] = item;
      const auto& node = terms_.get(id);
      if (node.kind == core::TermKind::Not) {
        pending.emplace_back(node.children.at(0), !negative);
        continue;
      }
      if (node.kind == core::TermKind::And && !negative) {
        for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
          if (!charge()) return;
          pending.emplace_back(*it, false);
        }
        continue;
      }
      if (node.kind == core::TermKind::BoolValue) {
        if (node.bool_value == negative) result_.contradiction = true;
        continue;
      }
      const bool equality = node.kind == core::TermKind::Equality;
      if ((!equality && node.kind != core::TermKind::Le && node.kind != core::TermKind::Lt) ||
          (equality && negative))
        continue;
      const auto sort = terms_.get(node.children.at(0)).sort;
      if (sort != terms_.symbols().int_sort() && sort != terms_.symbols().real_sort()) continue;
      const auto* lhs = normalize(node.children.at(0));
      if (!lhs) continue;
      const auto* rhs = normalize(node.children.at(1));
      if (!rhs) continue;
      // std::map keeps lhs/rhs cache borrows valid during insertion by normalize.
      Polynomial difference;
      difference.constant = lhs->constant;
      for (const auto& [term, coefficient] : lhs->coefficients) {
        if (!charge()) return;
        add(difference, term, coefficient);
      }
      difference.constant -= rhs->constant;
      for (const auto& [term, coefficient] : rhs->coefficients) {
        if (!charge()) return;
        add(difference, term, -coefficient);
      }
      const Rational sign(negative ? -1 : 1);
      Constraint row{
          {}, -difference.constant * sign, (node.kind == core::TermKind::Lt) != negative, equality};
      for (const auto& [term, coefficient] : difference.coefficients) {
        if (!charge()) return;
        row.entries.emplace_back(term, coefficient * sign);
      }
      ++result_.normalized_constraints;
      if (row.entries.empty()) {
        if (row.rhs.sign() < 0 || (row.rhs.is_zero() && row.strict) ||
            (equality && !row.rhs.is_zero()))
          result_.contradiction = true;
      } else if (row.entries.size() == 1) {
        const auto& [term, coefficient] = row.entries.front();
        const bool upper = coefficient.sign() > 0;
        update(term, Endpoint{row.rhs / coefficient, row.strict}, upper);
        if (equality) update(term, Endpoint{row.rhs / coefficient, false}, !upper);
      } else {
        rows_.push_back(std::move(row));
      }
    }
  }

  void propagate() {
    std::map<TermId, std::vector<std::size_t>> occurrences;
    std::deque<std::size_t> queue;
    std::vector<bool> queued(rows_.size(), true);
    for (std::size_t i = 0; i < rows_.size(); ++i) {
      for (const auto& entry : rows_[i].entries) {
        if (!charge()) return;
        occurrences[entry.first].push_back(i);
      }
      queue.push_back(i);
    }
    while (!queue.empty() && !result_.contradiction) {
      if (!charge()) return;
      const auto index = queue.front();
      queue.pop_front();
      queued[index] = false;
      ++result_.row_visits;
      const auto& row = rows_[index];
      std::vector<PreprocessBoundEndpoint> candidates;
      for (int direction = 0; direction < (row.equality ? 2 : 1); ++direction) {
        const Rational sign(direction == 0 ? 1 : -1);
        Rational sum(0);
        std::size_t missing = 0, strict = 0;
        std::vector<std::optional<Endpoint>> contributions;
        for (const auto& [term, coefficient] : row.entries) {
          if (!charge()) return;
          const auto found = intervals_.find(term);
          const auto value = found == intervals_.end()
                                 ? std::optional<Endpoint>{}
                                 : (coefficient.sign() * sign.sign() > 0 ? found->second.lower
                                                                         : found->second.upper);
          if (value) {
            contributions.push_back(Endpoint{value->value * coefficient * sign, value->strict});
            // The immediately preceding insertion constructed an engaged optional.
            sum += contributions.back()->value;
            if (value->strict) ++strict;
          } else {
            contributions.push_back(std::nullopt);
            ++missing;
          }
        }
        for (std::size_t i = 0; i < row.entries.size(); ++i) {
          if (!charge()) return;
          const auto& own = contributions[i];
          if (missing > static_cast<std::size_t>(!own.has_value())) continue;
          const auto& [term, coefficient] = row.entries[i];
          const auto signed_coefficient = coefficient * sign;
          const auto other = sum - (own ? own->value : Rational(0));
          const auto other_strict = strict - static_cast<std::size_t>(own && own->strict);
          candidates.push_back({term, (row.rhs * sign - other) / signed_coefficient,
                                signed_coefficient.sign() > 0, row.strict || other_strict != 0});
        }
      }
      // CAV2006 §4.5 interval rule. Finish the entire row snapshot before
      // updating endpoints, so excluded contributions always share a state.
      for (const auto& candidate : candidates) {
        if (!update(candidate.term, Endpoint{candidate.value, candidate.strict}, candidate.upper))
          continue;
        if (result_.contradiction) return;
        for (const auto affected : occurrences.at(candidate.term)) {
          if (!charge()) return;
          if (!queued[affected]) {
            queued[affected] = true;
            queue.push_back(affected);
          }
        }
      }
    }
  }

  const core::TermManager& terms_;
  std::size_t limit_;
  const std::function<bool()>& cancelled_;
  PreprocessBoundResult result_;
  std::map<TermId, std::optional<Polynomial>> polynomials_;
  std::map<TermId, Interval> intervals_;
  std::vector<Constraint> rows_;
};
}  // namespace

PreprocessBoundResult analyze_preprocessing_bounds(const core::TermManager& terms,
                                                   std::span<const TermId> assertions,
                                                   std::size_t work_limit,
                                                   const std::function<bool()>& cancelled) {
  return Analysis(terms, work_limit, cancelled).run(assertions);
}

std::vector<TermId> materialize_preprocessing_bounds(core::TermManager& terms,
                                                     const PreprocessBoundResult& result) {
  if (result.cancelled) return {};
  if (result.contradiction) return {terms.mk_bool(false)};
  std::vector<TermId> assertions;
  for (const auto& bound : result.bounds) {
    const auto sort = terms.get(bound.term).sort;
    const auto numeral = terms.mk_rational(bound.value, sort);
    const auto lhs = bound.upper ? bound.term : numeral;
    const auto rhs = bound.upper ? numeral : bound.term;
    assertions.push_back(bound.strict ? terms.mk_lt(lhs, rhs) : terms.mk_le(lhs, rhs));
  }
  return assertions;
}

std::vector<StrategyParameter> PreprocessBoundOptions::strategy_parameters() const {
  return {{"work-limit", std::to_string(work_limit)}};
}

Tactic make_preprocess_bound_tightening(PreprocessBoundOptions options) {
  return make_goal_tactic("preprocess-bound-tightening", [options](TacticContext& context,
                                                                   const Goal& goal) {
    const auto start = std::chrono::steady_clock::now();
    const auto result =
        analyze_preprocessing_bounds(context.terms(), goal.assertions(), options.work_limit,
                                     [&context] { return context.interrupted(); });
    auto& statistics = context.statistics();
    const auto set = [&statistics](const char* key, std::size_t value) {
      statistics.set_dynamic(std::string(":preprocessing.bounds.") + key, value);
    };
    set("work", result.work);
    set("constraints", result.normalized_constraints);
    set("endpoint-updates", result.endpoint_updates);
    set("row-visits", result.row_visits);
    set("newly-finite", result.newly_finite);
    set("emitted", result.cancelled || result.contradiction ? 0 : result.bounds.size());
    set("contradictions", result.contradiction ? 1 : 0);
    set("work-exhaustion", result.exhausted ? 1 : 0);
    set("emission-truncated", result.emission_truncated);
    set("cancelled", result.cancelled ? 1 : 0);
    if (result.cancelled) {
      const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::steady_clock::now() - start)
                               .count();
      set("time-us", static_cast<std::size_t>(elapsed));
      return AnyOf::single(goal.with_diagnostic(
          context.cancelled() ? InconclusiveReason::Cancelled : InconclusiveReason::ResourceLimit,
          context.cancelled() ? "cancelled" : "deadline reached"));
    }
    auto assertions = goal.assertions();
    const auto extra = materialize_preprocessing_bounds(context.terms(), result);
    assertions.insert(assertions.end(), extra.begin(), extra.end());
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();
    set("time-us", static_cast<std::size_t>(elapsed));
    return AnyOf::single(goal.with_assertions(std::move(assertions)));
  });
}
}  // namespace primo::smt
