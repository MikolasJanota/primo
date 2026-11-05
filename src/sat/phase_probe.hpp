#pragma once

#include <charconv>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace primo::sat {

// Isolated diagnostic, original design: docs/leads/adaptive-phase-probe.md.
// Phase advice is never a proof reason. The controller returns actions so no
// reference into its storage survives an operation on the borrowed backend.
class PhaseProbe {
 public:
  enum class Mode { Observe, Release, Invert };
  struct Config {
    Mode mode = Mode::Observe;
    std::size_t threshold = 8;
    std::size_t window = 64;
    std::size_t trace_limit = 0;
    bool report = false;
  };
  struct Action {
    int literal;
    bool release;
  };
  struct Counts {
    std::size_t hints = 0, changes = 0, decisions = 0, hinted = 0, aligned = 0;
    std::size_t overridden = 0, propagated = 0, assumptions = 0, removed = 0;
    std::size_t aligned_removed = 0, activations = 0, releases = 0, inversions = 0;
    std::size_t restores = 0;
  };

  explicit PhaseProbe(Config config) : config_(config) {
    if (config.window == 0) throw std::invalid_argument("phase probe window must be positive");
  }
  ~PhaseProbe() {
    if (!config_.report) return;
    std::fprintf(stderr,
                 "PHASE_PROBE_SUMMARY {\"hints\":%zu,\"changes\":%zu,\"decisions\":%zu,"
                 "\"hinted\":%zu,\"aligned\":%zu,\"overridden\":%zu,\"propagated\":%zu,"
                 "\"assumptions\":%zu,\"removed\":%zu,\"aligned_removed\":%zu,"
                 "\"activations\":%zu,\"releases\":%zu,\"inversions\":%zu,\"restores\":%zu}\n",
                 counts_.hints, counts_.changes, counts_.decisions, counts_.hinted, counts_.aligned,
                 counts_.overridden, counts_.propagated, counts_.assumptions, counts_.removed,
                 counts_.aligned_removed, counts_.activations, counts_.releases, counts_.inversions,
                 counts_.restores);
  }
  PhaseProbe(const PhaseProbe&) = delete;
  PhaseProbe& operator=(const PhaseProbe&) = delete;

  static std::optional<Config> environment() {
    const char* text = std::getenv("PRIMO_PHASE_PROBE");
    if (text == nullptr) return std::nullopt;
    Config config;
    const std::string_view mode(text);
    if (mode == "release")
      config.mode = Mode::Release;
    else if (mode == "invert")
      config.mode = Mode::Invert;
    else if (mode != "observe")
      throw std::invalid_argument("invalid PRIMO_PHASE_PROBE");
    config.threshold = number("PRIMO_PHASE_PROBE_K", 8);
    config.window = number("PRIMO_PHASE_PROBE_WINDOW", 64);
    config.trace_limit = number("PRIMO_PHASE_PROBE_TRACE", 0);
    config.report = true;
    return config;
  }

  const Counts& counts() const { return counts_; }
  void assume(int literal) { assumptions_.insert(variable(literal)); }
  void connect() {
    live_.clear();
    level_ = 0;
  }
  void new_level() { ++level_; }

  Action hint(int literal) {
    ++counts_.hints;
    auto& state = hints_[variable(literal)];
    if (state.literal != literal) {
      ++counts_.changes;
      const auto context = state.context + 1;
      state = Hint{literal, 0, 0, context, false};
      active_.erase(variable(literal));
    }
    if (config_.mode != Mode::Observe && config_.threshold == 0 && !state.active) {
      activate(variable(literal), state);
    }
    return action(state);
  }

  std::vector<Action> assignment(int literal, bool decision) {
    std::vector<Action> result;
    if (!decision) {
      ++counts_.propagated;
      return result;
    }
    if (assumptions_.contains(variable(literal))) {
      ++counts_.assumptions;
      return result;
    }
    if (level_ == 0) return result;  // Re-observation is not a fresh decision.
    ++clock_;
    ++counts_.decisions;
    const auto found = hints_.find(variable(literal));
    if (found != hints_.end()) {
      const Hint snapshot = found->second;
      ++counts_.hinted;
      if (snapshot.active) ++counts_.overridden;
      if (literal == snapshot.literal) ++counts_.aligned;
      live_.push_back(
          Decision{literal, snapshot.literal, level_, snapshot.context, snapshot.active});
      trace("decision", literal, snapshot.literal);
    }
    for (auto it = active_.begin(); it != active_.end();) {
      auto& state = hints_.at(*it);
      if (config_.threshold != 0 && clock_ >= state.until) {
        state.active = false;
        state.removals = 0;
        result.push_back(Action{state.literal, false});
        ++counts_.restores;
        it = active_.erase(it);
      } else
        ++it;
    }
    return result;
  }

  std::vector<Action> backtrack(std::size_t level) {
    std::vector<Action> result;
    while (!live_.empty() && live_.back().level > level) {
      const Decision decision = live_.back();
      live_.pop_back();
      ++counts_.removed;
      auto& state = hints_.at(variable(decision.literal));
      if (!decision.overridden && !state.active && decision.literal == decision.hint &&
          state.literal == decision.hint && state.context == decision.context) {
        ++counts_.aligned_removed;
        if (state.removals < config_.threshold) ++state.removals;
        if (config_.mode != Mode::Observe && config_.threshold != 0 &&
            state.removals >= config_.threshold) {
          activate(variable(decision.literal), state);
          result.push_back(action(state));
        }
      }
    }
    level_ = level;
    return result;
  }

  std::vector<Action> begin_solve() {
    std::vector<Action> result;
    if (config_.mode != Mode::Observe && config_.threshold == 0) {
      for (auto& [var, state] : hints_) {
        if (!state.active) activate(var, state);
        result.push_back(action(state));
      }
    }
    return result;
  }

  std::vector<Action> finish_solve() {
    std::vector<Action> result;
    for (const int var : active_) {
      auto& state = hints_.at(var);
      state.active = false;
      result.push_back(Action{state.literal, false});
      ++counts_.restores;
    }
    active_.clear();
    for (auto& [var, state] : hints_) {
      (void)var;
      state.removals = 0;
    }
    assumptions_.clear();
    connect();
    return result;
  }

 private:
  struct Hint {
    int literal;
    std::size_t removals, until, context;
    bool active;
  };
  struct Decision {
    int literal, hint;
    std::size_t level, context;
    bool overridden;
  };
  static int variable(int literal) { return literal < 0 ? -literal : literal; }
  static std::size_t number(const char* name, std::size_t fallback) {
    const char* text = std::getenv(name);
    if (text == nullptr) return fallback;
    const std::string_view input(text);
    std::size_t value = 0;
    const auto parsed = std::from_chars(input.data(), input.data() + input.size(), value);
    if (input.empty() || parsed.ec != std::errc{} || parsed.ptr != input.data() + input.size() ||
        value > 1000000000)
      throw std::invalid_argument(name);
    return value;
  }
  void activate(int var, Hint& state) {
    state.active = true;
    state.removals = 0;
    state.until = clock_ + config_.window;
    active_.insert(var);
    ++counts_.activations;
    trace("activate", var, state.literal);
  }
  Action action(const Hint& state) {
    if (!state.active) return Action{state.literal, false};
    if (config_.mode == Mode::Release) {
      ++counts_.releases;
      return Action{state.literal, true};
    }
    ++counts_.inversions;
    return Action{-state.literal, false};
  }
  void trace(const char* event, int literal, int hint_literal) {
    if (trace_count_++ >= config_.trace_limit) return;
    std::fprintf(stderr,
                 "PHASE_PROBE_EVENT {\"event\":\"%s\",\"literal\":%d,\"hint\":%d,"
                 "\"level\":%zu,\"clock\":%zu}\n",
                 event, literal, hint_literal, level_, clock_);
  }
  Config config_;
  Counts counts_;
  std::unordered_map<int, Hint> hints_;
  std::unordered_set<int> active_, assumptions_;
  std::vector<Decision> live_;
  std::size_t level_ = 0, clock_ = 0, trace_count_ = 0;
};

}  // namespace primo::sat
