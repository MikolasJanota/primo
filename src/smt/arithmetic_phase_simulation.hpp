#pragma once

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>

#include "util/rational.hpp"

namespace primo::smt {

// Original isolated diagnostic: docs/leads/phase-region-simulation.md.
// All results are preferences; neither interval counts nor history prove a lemma.
class ArithmeticPhaseSimulation {
 public:
  enum class Mode : std::uint8_t { Observe, Random, Gated, Least, Most };
  struct Config {
    Mode mode = Mode::Observe;
    std::uint32_t seed = 1;
    unsigned denominator = 16;
    std::size_t visits = 2;
    std::size_t history_limit = 4096;
    bool report = false;
  };
  struct Domain {
    std::optional<util::Rational> lower;
    std::optional<util::Rational> upper;
    bool operator==(const Domain&) const = default;
  };
  struct Counts {
    std::size_t refreshes = 0, entries = 0, finite_entries = 0, revisits = 0;
    std::size_t candidates = 0, contexts = 0, assigned = 0, finite_splits = 0;
    std::size_t narrow = 0, repeated = 0, flips = 0, evictions = 0;
  };

  explicit ArithmeticPhaseSimulation(Config config) : config_(config), random_(config.seed) {
    if (config.denominator == 0 || config.visits == 0 || config.history_limit == 0)
      throw std::invalid_argument("invalid arithmetic phase simulation limits");
  }
  ~ArithmeticPhaseSimulation() {
    if (!config_.report) return;
    std::fprintf(stderr,
                 "ARITH_PHASE_SUMMARY {\"refreshes\":%zu,\"entries\":%zu,\"finite_entries\":%zu,"
                 "\"revisits\":%zu,\"candidates\":%zu,\"contexts\":%zu,\"assigned\":%zu,"
                 "\"finite_splits\":%zu,\"narrow\":%zu,\"repeated\":%zu,\"flips\":%zu,"
                 "\"evictions\":%zu}\n",
                 counts_.refreshes, counts_.entries, counts_.finite_entries, counts_.revisits,
                 counts_.candidates, counts_.contexts, counts_.assigned, counts_.finite_splits,
                 counts_.narrow, counts_.repeated, counts_.flips, counts_.evictions);
  }
  ArithmeticPhaseSimulation(const ArithmeticPhaseSimulation&) = delete;
  ArithmeticPhaseSimulation& operator=(const ArithmeticPhaseSimulation&) = delete;

  static std::unique_ptr<ArithmeticPhaseSimulation> environment() {
    const char* text = std::getenv("PRIMO_REGION_PHASE");
    if (text == nullptr) return nullptr;
    Config config;
    const std::string_view mode(text);
    if (mode == "random")
      config.mode = Mode::Random;
    else if (mode == "gated")
      config.mode = Mode::Gated;
    else if (mode == "least")
      config.mode = Mode::Least;
    else if (mode == "most")
      config.mode = Mode::Most;
    else if (mode != "observe")
      throw std::invalid_argument("invalid PRIMO_REGION_PHASE");
    config.seed = number("PRIMO_REGION_SEED", 1);
    config.denominator = number("PRIMO_REGION_RATE", 16);
    config.report = true;
    return std::make_unique<ArithmeticPhaseSimulation>(config);
  }

  void begin_refresh() { ++counts_.refreshes; }
  const Counts& counts() const { return counts_; }
  std::size_t history_size() const { return history_.size(); }

  void domain(std::size_t variable, Domain value) {
    auto& state = domains_[variable];
    if (state.valid && state.value == value) return;
    state.value = std::move(value);
    state.valid = true;
    ++state.generation;
    ++counts_.entries;
    if (!size(state.value).has_value()) return;
    ++counts_.finite_entries;
    const auto name = key(variable, state.value);
    if (const auto found = history_.find(name); found != history_.end()) {
      if (found->second < config_.visits) ++found->second;
      ++counts_.revisits;
      return;
    }
    if (history_.size() == config_.history_limit) {
      history_.erase(history_order_.front());
      history_order_.pop_front();
      ++counts_.evictions;
    }
    history_.emplace(name, 1);
    history_order_.push_back(name);
  }

  // true_upper/false_endpoint are CLOSED integer endpoints, computed with
  // existing delta-aware rounding by the SMT integration, never guessed +/-1.
  int choose(std::size_t atom, std::size_t variable, int true_literal, int beta_literal,
             bool unassigned, bool integer, bool true_upper, const util::Rational& true_endpoint,
             const util::Rational& false_endpoint) {
    ++counts_.candidates;
    if (!unassigned) {
      ++counts_.assigned;
      atoms_.erase(atom);
      return beta_literal;
    }
    const auto& region = domains_.at(variable);
    auto& state = atoms_[atom];
    if (state.valid && state.generation == region.generation && state.beta == beta_literal)
      return state.chosen;
    ++counts_.contexts;
    state = AtomState{region.generation, beta_literal, beta_literal, true};
    const bool beta_true = beta_literal == true_literal;
    bool geometric_gate = false;
    bool narrow_true = false;
    if (integer) {
      Domain yes = intersect(region.value, true_upper, true_endpoint);
      Domain no = intersect(region.value, !true_upper, false_endpoint);
      const auto yes_size = size(yes);
      const auto no_size = size(no);
      if (yes_size && no_size && *yes_size > util::Rational(0) && *no_size > util::Rational(0)) {
        ++counts_.finite_splits;
        narrow_true = *yes_size < *no_size;
        const auto& small = narrow_true ? *yes_size : *no_size;
        const auto& large = narrow_true ? *no_size : *yes_size;
        if (small <= util::Rational(4) && large >= small * util::Rational(4)) {
          ++counts_.narrow;
          const Domain& child = narrow_true ? yes : no;
          const auto found = history_.find(key(variable, child));
          geometric_gate = found != history_.end() && found->second >= config_.visits;
          if (geometric_gate) ++counts_.repeated;
        }
      }
    }
    if (config_.mode == Mode::Random ||
        (config_.mode == Mode::Gated && geometric_gate && beta_true == narrow_true)) {
      if (std::uniform_int_distribution<unsigned>(0, config_.denominator - 1)(random_) == 0)
        state.chosen = -beta_literal;
    } else if (geometric_gate && config_.mode == Mode::Least) {
      state.chosen = narrow_true ? -true_literal : true_literal;
    } else if (geometric_gate && config_.mode == Mode::Most) {
      state.chosen = narrow_true ? true_literal : -true_literal;
    }
    if (state.chosen != beta_literal) ++counts_.flips;
    return state.chosen;
  }

 private:
  struct DomainState {
    Domain value;
    std::size_t generation = 0;
    bool valid = false;
  };
  struct AtomState {
    std::size_t generation = 0;
    int beta = 0;
    int chosen = 0;
    bool valid = false;
  };
  static unsigned number(const char* name, unsigned fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr) return fallback;
    const std::string_view text(value);
    unsigned result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
      throw std::invalid_argument(std::string("invalid ") + name);
    return result;
  }
  static std::optional<util::Rational> size(const Domain& domain) {
    if (!domain.lower || !domain.upper) return std::nullopt;
    return std::max(util::Rational(0), *domain.upper - *domain.lower + util::Rational(1));
  }
  static Domain intersect(Domain domain, bool upper, const util::Rational& endpoint) {
    auto& bound = upper ? domain.upper : domain.lower;
    if (!bound || (upper ? endpoint < *bound : endpoint > *bound)) bound = endpoint;
    return domain;
  }
  static std::string key(std::size_t variable, const Domain& domain) {
    return std::to_string(variable) + ":" + domain.lower->to_string() + ":" +
           domain.upper->to_string();
  }

  Config config_;
  std::mt19937 random_;
  Counts counts_;
  std::unordered_map<std::size_t, DomainState> domains_;
  std::unordered_map<std::size_t, AtomState> atoms_;
  std::unordered_map<std::string, std::size_t> history_;
  std::deque<std::string> history_order_;
};

}  // namespace primo::smt
