#include "smt/tactic.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "core/term.hpp"
#include "core/term_manager.hpp"
#include "smt/inconclusive_reason.hpp"
#include "smt/model_reconstruction.hpp"
#include "util/statistics.hpp"

namespace primo::smt {
namespace {

using TacticClock = std::chrono::steady_clock;

std::uint64_t elapsed_microseconds(TacticClock::time_point start, TacticClock::time_point end) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
}

std::string stat_name(std::string_view tactic, std::string_view suffix) {
  return ":tactic." + std::string(tactic) + "." + std::string(suffix);
}

std::string time_stat_name(std::string_view tactic) {
  return ":time.tactic." + std::string(tactic) + "-us";
}

std::shared_ptr<const std::vector<core::TermId>> empty_assertions() {
  static const auto storage = std::make_shared<const std::vector<core::TermId>>();
  return storage;
}

std::shared_ptr<const ModelReconstruction> empty_reconstruction() {
  static const auto storage = std::make_shared<const ModelReconstruction>();
  return storage;
}

}  // namespace

struct Goal::Data {
  std::shared_ptr<const std::vector<core::TermId>> assertions = empty_assertions();
  std::shared_ptr<const ModelReconstruction> reconstruction = empty_reconstruction();
  InconclusiveReason reason = InconclusiveReason::None;
  std::string diagnostic;
  std::shared_ptr<const core::Model> typed_model = {};
};

Goal::Goal() : data_(std::make_shared<const Data>()) {}

Goal::Goal(std::vector<core::TermId> assertions)
    : data_(std::make_shared<const Data>(
          Data{.assertions = assertions.empty() ? empty_assertions()
                                                : std::make_shared<const std::vector<core::TermId>>(
                                                      std::move(assertions)),
               .reconstruction = empty_reconstruction(),
               .reason = InconclusiveReason::None,
               .diagnostic = {}})) {}

Goal::Goal(std::shared_ptr<const Data> data) : data_(std::move(data)) {}

const std::vector<core::TermId>& Goal::assertions() const { return *data_->assertions; }

const std::shared_ptr<const core::Model>& Goal::typed_model() const { return data_->typed_model; }

Goal Goal::with_typed_model(std::shared_ptr<const core::Model> model) const {
  if (model == data_->typed_model) return *this;
  Data updated = *data_;
  updated.typed_model = std::move(model);
  return Goal(std::make_shared<const Data>(std::move(updated)));
}

const ModelReconstruction& Goal::reconstruction() const { return *data_->reconstruction; }

InconclusiveReason Goal::reason() const { return data_->reason; }

const std::string& Goal::diagnostic() const { return data_->diagnostic; }

Goal Goal::with_assertions(std::vector<core::TermId> assertions) const {
  if (assertions == *data_->assertions) {
    return *this;
  }
  Data updated = *data_;
  updated.assertions =
      assertions.empty() ? empty_assertions()
                         : std::make_shared<const std::vector<core::TermId>>(std::move(assertions));
  return Goal(std::make_shared<const Data>(std::move(updated)));
}

Goal Goal::with_reconstruction(ModelReconstruction reconstruction) const {
  if (reconstruction == *data_->reconstruction) {
    return *this;
  }
  Data updated = *data_;
  updated.reconstruction =
      reconstruction == *empty_reconstruction()
          ? empty_reconstruction()
          : std::make_shared<const ModelReconstruction>(std::move(reconstruction));
  return Goal(std::make_shared<const Data>(std::move(updated)));
}

Goal Goal::with_diagnostic(InconclusiveReason reason, std::string diagnostic) const {
  if (reason == data_->reason && diagnostic == data_->diagnostic) {
    return *this;
  }
  Data updated = *data_;
  updated.reason = reason;
  updated.diagnostic = std::move(diagnostic);
  return Goal(std::make_shared<const Data>(std::move(updated)));
}

Goal Goal::with_diagnostic(std::string diagnostic) const {
  return with_diagnostic(
      diagnostic.empty() ? InconclusiveReason::None : InconclusiveReason::Unknown,
      std::move(diagnostic));
}

bool Goal::operator==(const Goal& other) const {
  return data_ == other.data_ ||
         (data_->typed_model == other.data_->typed_model && data_->reason == other.data_->reason &&
          data_->diagnostic == other.data_->diagnostic &&
          (data_->assertions == other.data_->assertions ||
           *data_->assertions == *other.data_->assertions) &&
          (data_->reconstruction == other.data_->reconstruction ||
           *data_->reconstruction == *other.data_->reconstruction));
}

AnyOf::AnyOf(std::vector<Goal> alternatives)
    : alternatives_(std::make_shared<const std::vector<Goal>>(std::move(alternatives))) {
  if (alternatives_->empty()) {
    throw std::invalid_argument("AnyOf requires at least one goal");
  }
}

AnyOf AnyOf::single(Goal goal) {
  std::vector<Goal> alternatives;
  alternatives.push_back(std::move(goal));
  return AnyOf(std::move(alternatives));
}

bool AnyOf::operator==(const AnyOf& other) const {
  return alternatives_ == other.alternatives_ || *alternatives_ == *other.alternatives_;
}

TacticContext::TacticContext(core::TermManager& terms, util::Statistics& statistics,
                             std::function<bool()> cancelled, bool produce_model,
                             FreshConstantFactory fresh_constant)
    : terms_(terms),
      statistics_(statistics),
      cancelled_(std::move(cancelled)),
      produce_model_(produce_model),
      fresh_constant_(std::move(fresh_constant)) {}

core::TermId TacticContext::fresh_constant(core::SortId sort, std::string_view prefix) const {
  if (!fresh_constant_) {
    throw TacticError("this tactic needs fresh constants, which the context does not supply");
  }
  return fresh_constant_(sort, prefix);
}

bool TacticContext::cancelled() const { return cancelled_ && cancelled_(); }

bool TacticContext::deadline_reached() const {
  return deadline_.has_value() && Clock::now() >= *deadline_;
}

bool TacticContext::interrupted() const { return cancelled() || deadline_reached(); }

TacticContext::DeadlineScope::DeadlineScope(TacticContext& context, Clock::duration budget)
    : context_(context), previous_(context.deadline_) {
  const Clock::time_point candidate = Clock::now() + budget;
  if (!context_.deadline_.has_value() || candidate < *context_.deadline_) {
    context_.deadline_ = candidate;
  }
}

TacticContext::DeadlineScope::~DeadlineScope() { context_.deadline_ = previous_; }

GoalTruth truth(const Goal& goal, const TacticContext& context) {
  if (goal.assertions().empty()) {
    return GoalTruth::True;
  }
  for (const core::TermId assertion : goal.assertions()) {
    const core::Term& term = context.terms().get(assertion);
    if (term.sort != context.terms().symbols().bool_sort()) {
      throw TacticError("goal assertion is not Bool-sorted");
    }
    if (term.kind == core::TermKind::BoolValue && !term.bool_value) {
      return GoalTruth::False;
    }
  }
  return GoalTruth::Inconclusive;
}

GoalTruth truth(const AnyOf& goals, const TacticContext& context) {
  bool all_false = true;
  for (const Goal& goal : goals.alternatives()) {
    const GoalTruth goal_truth = truth(goal, context);
    if (goal_truth == GoalTruth::True) {
      return GoalTruth::True;
    }
    if (goal_truth != GoalTruth::False) {
      all_false = false;
    }
  }
  return all_false ? GoalTruth::False : GoalTruth::Inconclusive;
}

struct Tactic::Implementation {
  const std::string name;
  const Function function;
  const IncrementalSafety safety;
  const std::string invocation_stat;
  const std::string time_stat;

  Implementation(std::string tactic_name, Function tactic_function, IncrementalSafety tactic_safety)
      : name(std::move(tactic_name)),
        function(std::move(tactic_function)),
        safety(tactic_safety),
        invocation_stat(stat_name(name, "invocations")),
        time_stat(time_stat_name(name)) {}
};

Tactic::Tactic(std::string name, Function function, IncrementalSafety safety)
    : implementation_(
          std::make_shared<const Implementation>(std::move(name), std::move(function), safety)) {
  if (implementation_->name.empty()) {
    throw std::invalid_argument("a tactic requires a non-empty name");
  }
  if (!implementation_->function) {
    throw std::invalid_argument("a tactic requires an implementation");
  }
}

AnyOf Tactic::operator()(TacticContext& context, const AnyOf& input) const {
  if (context.incremental() && implementation_->safety == IncrementalSafety::Unclassified)
    throw TacticError("tactic is not classified for incremental execution: " + name());
  const bool cancelled = context.cancelled();
  if (cancelled || context.deadline_reached()) {
    const InconclusiveReason reason =
        cancelled ? InconclusiveReason::Cancelled : InconclusiveReason::ResourceLimit;
    const char* diagnostic =
        reason == InconclusiveReason::Cancelled ? "cancelled" : "deadline reached";
    std::vector<Goal> interrupted;
    interrupted.reserve(input.size());
    for (const Goal& goal : input.alternatives()) {
      interrupted.push_back(goal.with_diagnostic(reason, diagnostic));
    }
    return AnyOf(std::move(interrupted));
  }

  const TacticClock::time_point start = TacticClock::now();
  context.statistics().increment_dynamic(implementation_->invocation_stat);
  AnyOf output = implementation_->function(context, input);
  context.statistics().increment_dynamic(implementation_->time_stat,
                                         elapsed_microseconds(start, TacticClock::now()));
  return output;
}

const std::string& Tactic::name() const { return implementation_->name; }

Tactic make_goal_tactic(std::string name, GoalTactic tactic, IncrementalSafety safety) {
  return Tactic(
      std::move(name),
      [tactic = std::move(tactic)](TacticContext& context, const AnyOf& input) {
        std::vector<Goal> output;
        for (const Goal& goal : input.alternatives()) {
          if (truth(goal, context) != GoalTruth::Inconclusive) {
            output.push_back(goal);
            continue;
          }
          AnyOf alternatives = tactic(context, goal);
          for (const Goal& alternative : alternatives.alternatives()) {
            output.push_back(alternative);
          }
        }
        return AnyOf(std::move(output));
      },
      safety);
}

Tactic skip() {
  return Tactic("skip", [](TacticContext&, const AnyOf& input) { return input; });
}

Tactic sequence(std::string name, std::vector<Tactic> tactics) {
  return Tactic(std::move(name),
                [tactics = std::move(tactics)](TacticContext& context, const AnyOf& input) {
                  AnyOf current = input;
                  for (const Tactic& tactic : tactics) {
                    current = tactic(context, current);
                    if (truth(current, context) != GoalTruth::Inconclusive) {
                      break;
                    }
                  }
                  return current;
                });
}

Tactic conditional(std::string name, GoalPredicate predicate, Tactic if_true, Tactic if_false) {
  return make_goal_tactic(
      std::move(name), [predicate = std::move(predicate), if_true = std::move(if_true),
                        if_false = std::move(if_false)](TacticContext& context, const Goal& goal) {
        AnyOf input = AnyOf::single(goal);
        const Goal& probe_goal = input.alternatives().front();
        return predicate(context, probe_goal) ? if_true(context, input) : if_false(context, input);
      });
}

Tactic repeat(std::string name, Tactic tactic, std::size_t max_iterations,
              RepeatPredicate continue_after_change, RepeatEquivalence equivalent,
              RepeatObserver observer) {
  return Tactic(std::move(name), [tactic = std::move(tactic), max_iterations,
                                  continue_after_change = std::move(continue_after_change),
                                  equivalent = std::move(equivalent),
                                  observer = std::move(observer)](TacticContext& context,
                                                                  const AnyOf& input) {
    AnyOf current = input;
    for (std::size_t iteration = 0; iteration < max_iterations; ++iteration) {
      AnyOf after = tactic(context, current);
      if (observer) {
        observer(context, current, after, iteration + 1);
      }
      const bool reached_fixed_point =
          equivalent ? equivalent(context, current, after) : after == current;
      if (reached_fixed_point) {
        return after;
      }
      const AnyOf before = current;
      current = std::move(after);
      const bool continue_requested =
          !continue_after_change || continue_after_change(context, before, current, iteration + 1);
      if (truth(current, context) != GoalTruth::Inconclusive) {
        break;
      }
      if (!continue_requested) {
        break;
      }
    }
    return current;
  });
}

AnyOf SequentialTacticScheduler::first_terminal(TacticContext& context, const AnyOf& input,
                                                const std::vector<Tactic>& tactics) const {
  AnyOf last = input;
  for (const Tactic& tactic : tactics) {
    last = tactic(context, input);
    if (truth(last, context) != GoalTruth::Inconclusive) {
      return last;
    }
    for (const Goal& goal : last.alternatives()) {
      if (goal.reason() == InconclusiveReason::Cancelled) {
        return last;
      }
    }
  }
  return last;
}

Tactic first_terminal(std::string name, std::shared_ptr<const TacticScheduler> scheduler,
                      std::vector<Tactic> tactics) {
  if (!scheduler) {
    throw std::invalid_argument("first-terminal requires a scheduler");
  }
  return Tactic(std::move(name), [scheduler = std::move(scheduler), tactics = std::move(tactics)](
                                     TacticContext& context, const AnyOf& input) {
    return scheduler->first_terminal(context, input, tactics);
  });
}

}  // namespace primo::smt
