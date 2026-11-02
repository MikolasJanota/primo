#include "smt/smt_session.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "core/term_manager.hpp"
#include "smt/check_sat.hpp"
#include "smt/frontend.hpp"
#include "smt/incremental_engine.hpp"
#include "smt/preprocessing_tactics.hpp"
#include "smt/session_portfolio.hpp"
#include "smt/solver_error.hpp"
#include "smt/solver_session.hpp"
#include "smt/tactic.hpp"
#include "smtlib/ast.hpp"
#include "smtlib/commands.hpp"
#include "smtlib/parser.hpp"
#include "smtlib/printer.hpp"
#include "util/statistics.hpp"

namespace primo::smt {
namespace {
using Clock = std::chrono::steady_clock;
using Command = smtlib::Command;
using Kind = smtlib::CommandKind;

util::Statistics::Value micros(Clock::time_point start) {
  return static_cast<util::Statistics::Value>(
      std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count());
}
}  // namespace

struct SmtSession::Implementation final : SessionExecutor {
  struct Entry {
    std::size_t depth;
    Command command;
  };

  explicit Implementation(CheckSatOptions supplied) : options(std::move(supplied)) {}

  CheckSatOptions options;
  std::optional<Command> logic;
  std::vector<Entry> journal;
  std::size_t depth = 0;
  std::size_t queries = 0;
  std::size_t rebuilds = 0;
  bool produce_models = false;
  bool print_success = false;
  bool stopped = false;
  std::unique_ptr<Frontend> frontend;
  std::unique_ptr<IncrementalEngine> engine;
  std::vector<std::size_t> assertion_scopes;
  std::size_t engine_builds = 0;
  std::size_t translated_assertions = 0;
  util::Statistics preprocessing_statistics;
  util::Statistics::Value preprocessing_us = 0;
  std::vector<core::TermId> assertions;
  std::shared_ptr<const CheckSatOutcome> last;
  util::Statistics::Value parse_us = 0;
  util::Statistics::Value frontend_us = 0;

  CommandResponse response(ResponseKind kind, std::string message = {}) const {
    return {kind, print_success, std::move(message), nullptr};
  }

  void discard_translation() {
    engine.reset();
    assertion_scopes.clear();
    assertions.clear();
    frontend.reset();
  }

  void ensure_frontend() {
    if (frontend) return;
    const auto start = Clock::now();
    frontend = std::make_unique<Frontend>(assumptions());
    if (logic) frontend->process_command(*logic, assertions);
    for (const auto& entry : journal) frontend->process_command(entry.command, assertions);
    if (assumptions()) {
      engine = logic->logic == "QF_UF" ? make_incremental_uf_engine(frontend->terms(), options)
                                       : make_incremental_lra_engine(frontend->terms(), options);
      ++engine_builds;
    }
    ++rebuilds;
    frontend_us += micros(start);
  }

  bool assumptions() const { return options.incremental_mode == IncrementalMode::Assumptions; }

  void validate_incremental_options(std::string_view selected_logic) const {
    if (selected_logic == "QF_UF") {
      if (options.euf_engine != EufEngine::Incremental ||
          (options.euf_propagation != EufPropagationMode::Auto &&
           options.euf_propagation != EufPropagationMode::Online &&
           options.euf_propagation != EufPropagationMode::Conflict) ||
          options.euf_dynamic_ackermann_shadow || options.euf_diseq_ackermann)
        throw std::invalid_argument(
            "QF_UF assumptions requires incremental EUF, "
            "auto/online/conflict propagation and no experimental Ackermann modes");
      if (options.pure_theory_filter == PureTheoryFilterMode::On)
        throw std::invalid_argument(
            "QF_UF assumptions requires pure-theory-filter=auto or off; auto resolves to off");
    }
    if (options.preprocessing_mode != PreprocessingMode::Auto &&
        options.preprocessing_mode != PreprocessingMode::Off)
      throw std::invalid_argument("assumptions mode requires preprocessing-mode=auto or off");
    if (options.qf_uf_portfolio != QfUfPortfolioMode::Off ||
        options.experimental_split_bools != 0 || options.lra_fixed_elimination ||
        options.preprocess_only || !options.cadical_options.empty())
      throw std::invalid_argument(
          "assumptions mode rejects portfolios, splitting, fixed-variable "
          "elimination, preprocess-only and custom CaDiCaL options");
    if (options.monotone_elimination.value_or(false) || options.ground_unit_substitution ||
        options.guarded_ground_unit_substitution || options.ground_congruence)
      throw std::invalid_argument("assumptions mode rejects assertion-dependent preprocessing");
  }

  void append_incremental(std::size_t first) {
    if (first == assertions.size()) return;
    for (auto root = assertions.begin() + static_cast<std::ptrdiff_t>(first);
         root != assertions.end(); ++root) {
      if (frontend->terms().contains_quantifier(*root))
        throw UnsupportedInput("assumptions mode does not support quantified formulas");
    }
    const auto start = Clock::now();
    TacticContext context(frontend->terms(), preprocessing_statistics, {}, false,
                          [this](core::SortId sort, std::string_view prefix) {
                            return frontend->fresh_internal_constant(sort, prefix);
                          });
    context.require_incremental_safety();
    auto goal = AnyOf::single(Goal(std::vector<core::TermId>(
        assertions.begin() + static_cast<std::ptrdiff_t>(first), assertions.end())));
    if (options.preprocessing_mode == PreprocessingMode::Auto) {
      if (options.extended_simplification)
        goal = make_simplify_extended_terms({options.extended_simplification_rounds,
                                             options.ite_equality_distribution})(context, goal);
      goal = make_normalize_boolean({})(context, goal);
    }
    goal = make_lower_extended_terms()(context, goal);
    if (logic->logic == "QF_LRA")
      goal = make_normalize_arithmetic({.canonicalize = options.arithmetic_atom_canonicalization,
                                        .lower_real_equalities = true})(context, goal);
    if (goal.size() != 1) throw TacticError("incremental pipeline must preserve one conjunction");
    preprocessing_us += micros(start);
    engine->append(goal.alternatives().front().assertions());
  }

  CheckSatOutcome check_incremental(CheckSatOptions selected) {
    ensure_frontend();
    const auto check_start = Clock::now();
    selected.produce_model = selected.produce_model || selected.check_model;
    auto outcome = logic->logic == "QF_UF"
                       ? engine->check(assertions, selected)
                       : engine->check(frontend->declared_real_constant_terms(), selected);
    if (outcome.typed_model) {
      const auto start = Clock::now();
      if (selected.check_model) {
        core::ModelEvaluator evaluator(frontend->terms(), *outcome.typed_model);
        for (const auto root : assertions) {
          if (evaluator.evaluate(root) !=
              core::ModelValue{frontend->terms().symbols().bool_sort(), true})
            throw core::ModelError("active incremental assertion is false");
        }
      }
      outcome.model = smtlib::print_model(*outcome.typed_model);
      outcome.statistics.set(":time.model.check-and-print-us", micros(start));
    }
    outcome.statistics.set(":session.engine-builds", engine_builds);
    outcome.statistics.set(":session.translated-assertions", translated_assertions);
    outcome.statistics.set(":session.incremental-assumptions", 1);
    outcome.statistics.set(":session.safe-rewrite-pipeline",
                           options.preprocessing_mode == PreprocessingMode::Auto ? 1 : 0);
    outcome.statistics.set(":session.pipeline.lower-extended-terms", 1);
    outcome.statistics.set(":session.pipeline.lower-real-equalities",
                           logic->logic == "QF_LRA" ? 1 : 0);
    for (const auto& key : preprocessing_statistics.keys())
      outcome.statistics.set(key, preprocessing_statistics.get(key));
    preprocessing_statistics = {};
    outcome.statistics.set(":time.preprocessing-us", preprocessing_us);
    preprocessing_us = 0;
    outcome.statistics.set(":time.total-us", micros(check_start));
    return outcome;
  }

  void validate_parameterized_definition(const Command& command) const {
    if (assumptions()) return;  // Validated directly by the persistent frontend.
    if (command.kind != Kind::DefineFun || command.parameters.empty()) return;
    // Existing macros expand on use. Validate in an isolated generation so
    // unused/forward-referencing bodies fail now without perturbing query IDs.
    Frontend validation;
    std::vector<core::TermId> roots;
    validation.process_command(*logic, roots);
    for (const auto& entry : journal) {
      if (entry.command.kind != Kind::Assert) validation.process_command(entry.command, roots);
    }
    validation.process_command(command, roots);
    std::vector<smtlib::SExpr> application{smtlib::SExpr::make_atom(command.name)};
    for (const auto& parameter : command.parameters) {
      const auto sort = validation.terms().symbols().lookup_sort(parameter.sort);
      if (!sort) throw std::invalid_argument("unknown parameter sort: " + parameter.sort);
      const auto term = validation.fresh_internal_constant(*sort, "definition_check");
      const auto symbol = validation.terms().get(term).symbol;
      application.push_back(
          smtlib::SExpr::make_atom(validation.terms().symbols().get_symbol(symbol).name));
    }
    auto call = smtlib::SExpr::make_list(std::move(application));
    Command check;
    check.kind = Kind::Assert;
    check.assertion = smtlib::SExpr::make_list({smtlib::SExpr::make_atom("="), call, call});
    validation.process_command(check, roots);
  }

  // Portfolio policy is shared with the one-shot API. Each arm consumes its
  // own frontend; a fallback replays the original journal, never a solved goal.
  CheckSatOutcome execute(std::string_view, CheckSatOptions selected) override {
    ensure_frontend();
    auto roots = std::move(assertions);
    assertions.clear();
    return check_sat_frontend_once(std::move(frontend), std::move(roots), std::move(selected));
  }

  CommandResponse apply(Command command) {
    if (command.kind == Kind::Unsupported) {
      return response(ResponseKind::Unsupported, "unsupported command: " + command.name);
    }
    if (command.kind == Kind::Exit) {
      stopped = true;
      return response(ResponseKind::Exit);
    }
    if (command.kind == Kind::Ignored) return response(ResponseKind::Success);
    if (command.kind == Kind::Reset) {
      logic.reset();
      journal.clear();
      depth = 0;
      last.reset();
      discard_translation();
      produce_models = false;
      print_success = false;
      return response(ResponseKind::Success);
    }
    if (command.kind == Kind::SetOption) {
      const bool start_only =
          command.option == ":produce-models" || command.option == ":global-declarations";
      if (start_only && logic) throw std::invalid_argument(command.option + " requires start mode");
      if (start_only || command.option == ":print-success") {
        if (command.value != "true" && command.value != "false") {
          throw std::invalid_argument(command.option + " expects true or false");
        }
        const bool enabled = command.value == "true";
        if (command.option == ":global-declarations" && enabled) {
          return response(ResponseKind::Unsupported, "global declarations are not supported");
        }
        if (command.option == ":produce-models") produce_models = enabled;
        if (command.option == ":print-success") print_success = enabled;
        return response(ResponseKind::Success);
      }
      return response(ResponseKind::Unsupported, "unsupported option: " + command.option);
    }
    if (command.kind == Kind::SetLogic) {
      if (logic) throw std::invalid_argument("set-logic requires start mode; use reset first");
      if (command.logic != "QF_UF" && command.logic != "QF_LRA" && command.logic != "QF_UFLRA" &&
          command.logic != "QF_LIA" && command.logic != "QF_UFLIA" && command.logic != "UF") {
        return response(ResponseKind::Unsupported, "unsupported logic: " + command.logic);
      }
      if (assumptions()) {
        validate_incremental_options(command.logic);
        if (command.logic != "QF_LRA" && command.logic != "QF_UF")
          return response(ResponseKind::Unsupported,
                          "assumptions mode supports only QF_LRA and QF_UF");
      }
      discard_translation();
      logic = std::move(command);
      return response(ResponseKind::Success);
    }
    if (command.kind == Kind::ResetAssertions) {
      journal.clear();
      depth = 0;
      last.reset();
      discard_translation();
      return response(ResponseKind::Success);
    }
    if (!logic) throw std::invalid_argument("command requires set-logic");
    if (command.kind == Kind::Push || command.kind == Kind::Pop) {
      if (command.kind == Kind::Push) {
        if (command.count > std::numeric_limits<std::size_t>::max() - depth) {
          throw std::invalid_argument("scope depth overflow");
        }
        if (assumptions()) {
          ensure_frontend();
          for (std::size_t i = 0; i < command.count; ++i) {
            assertion_scopes.push_back(assertions.size());
            frontend->push_scope();
            engine->push();
          }
        }
        depth += command.count;
      } else {
        if (command.count > depth)
          throw std::invalid_argument("cannot pop the base assertion level");
        if (assumptions()) {
          for (std::size_t i = 0; i < command.count; ++i) {
            engine->pop();
            frontend->pop_scope();
            assertions.resize(assertion_scopes.back());
            assertion_scopes.pop_back();
          }
        }
        depth -= command.count;
        while (!journal.empty() && journal.back().depth > depth) journal.pop_back();
        if (command.count != 0 && !assumptions()) discard_translation();
      }
      last.reset();
      return response(ResponseKind::Success);
    }
    if (command.kind == Kind::GetModel) {
      if (!produce_models) throw std::invalid_argument("get-model requires :produce-models true");
      if (!last || last->result != CheckSatResult::Sat || !last->typed_model) {
        throw std::invalid_argument("get-model requires a current SAT model");
      }
      return {ResponseKind::Model, print_success, {}, last};
    }
    if (command.kind == Kind::CheckSat) {
      last.reset();
      CheckSatOptions selected = options;
      selected.produce_model = produce_models || selected.produce_model;
      auto outcome = assumptions() ? check_incremental(selected)
                     : selected.qf_uf_portfolio == QfUfPortfolioMode::Off
                         ? execute({}, selected)
                         : execute_qf_uf_session_portfolio({}, selected, *this);
      ++queries;
      outcome.statistics.set(":session.queries", queries);
      outcome.statistics.set(":session.frontend-builds", rebuilds);
      outcome.statistics.set(":time.parse-us", parse_us);
      outcome.statistics.set(":time.frontend-us", frontend_us);
      outcome.statistics.set(":time.total-us",
                             outcome.statistics.get(":time.total-us") + parse_us + frontend_us);
      parse_us = 0;
      frontend_us = 0;
      last = std::make_shared<CheckSatOutcome>(std::move(outcome));
      return {ResponseKind::CheckSat, print_success, last->message, last};
    }
    ensure_frontend();
    const auto start = Clock::now();
    validate_parameterized_definition(command);
    if (assumptions() && logic->logic != "QF_UF" && command.kind == Kind::DeclareSort)
      throw std::invalid_argument("assumptions mode rejects uninterpreted sorts");
    const auto first = assertions.size();
    frontend->process_command(command, assertions);
    if (assumptions()) {
      if (command.kind == Kind::Assert) ++translated_assertions;
      append_incremental(first);
    }
    frontend_us += micros(start);
    journal.push_back({depth, std::move(command)});
    last.reset();
    return response(ResponseKind::Success);
  }
};

SmtSession::SmtSession(CheckSatOptions options)
    : implementation_(std::make_unique<Implementation>(std::move(options))) {
  if (!implementation_->options.export_cnf.empty() ||
      implementation_->options.export_cnf_reduction != ExportCnfReduction::None) {
    throw std::invalid_argument("CNF export requires buffered check_sat_smtlib, not SmtSession");
  }
}
SmtSession::~SmtSession() = default;
bool SmtSession::terminated() const { return implementation_->stopped; }

CommandResponse SmtSession::execute(std::string_view text) {
  auto& state = *implementation_;
  if (state.stopped) return state.response(ResponseKind::Error, "session has terminated");
  try {
    const auto start = Clock::now();
    auto commands = smtlib::Parser(text).parse_commands();
    const auto parse_us = micros(start);
    if (commands.size() != 1) throw std::invalid_argument("execute expects exactly one command");
    return execute(std::move(commands.front()), parse_us);
  } catch (const std::exception& error) {
    state.stopped = true;
    state.last.reset();
    state.discard_translation();
    return state.response(ResponseKind::Error, error.what());
  }
}

CommandResponse SmtSession::execute(smtlib::Command command, std::uint64_t parse_microseconds) {
  auto& state = *implementation_;
  if (state.stopped) return state.response(ResponseKind::Error, "session has terminated");
  try {
    state.parse_us += parse_microseconds;
    return state.apply(std::move(command));
  } catch (const std::exception& error) {
    state.stopped = true;
    state.last.reset();
    state.discard_translation();
    return state.response(ResponseKind::Error, error.what());
  }
}

}  // namespace primo::smt
