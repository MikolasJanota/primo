// Qualification-only exporter: lowered input AND NOT(emitted conjunction).
// No solving occurs here; an independent reference must refute the output.
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "core/sort.hpp"
#include "core/symbol_table.hpp"
#include "core/term.hpp"
#include "core/term_manager.hpp"
#include "smt/check_sat.hpp"
#include "smt/frontend.hpp"
#include "smt/preprocess_bound_tightening.hpp"
#include "smt/preprocessing_tactics.hpp"
#include "smt/tactic.hpp"
#include "smtlib/commands.hpp"
#include "smtlib/parser.hpp"
#include "smtlib/term_printer.hpp"
#include "util/rational.hpp"
#include "util/statistics.hpp"

namespace {
std::string numeral(const primo::util::Rational& value, bool real) {
  const auto absolute = value.sign() < 0 ? -value : value;
  auto text = absolute.numerator().to_string();
  if (absolute.is_integer() && real) text += ".0";
  if (!absolute.is_integer()) text = "(/ " + text + " " + absolute.denominator().to_string() + ")";
  return value.sign() < 0 ? "(- " + text + ")" : text;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 2) throw std::runtime_error("expected one nonincremental input file");
    std::ifstream input(argv[1]);
    if (!input) throw std::runtime_error("cannot read input");
    primo::smt::Frontend frontend;
    std::vector<primo::core::TermId> assertions;
    const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    for (const auto& command : primo::smtlib::Parser(text).parse_commands()) {
      if (command.kind == primo::smtlib::CommandKind::CheckSat) break;
      frontend.process_command(command, assertions);
    }
    auto& terms = frontend.terms();
    // Match the production prefix preceding tightening, including its default
    // simplifiers. A different lowered goal could miss the actual bound batch.
    const primo::smt::CheckSatOptions defaults;
    primo::util::Statistics statistics;
    primo::smt::TacticContext context(terms, statistics, {}, false,
                                      [&frontend](auto sort, auto prefix) {
                                        return frontend.fresh_internal_constant(sort, prefix);
                                      });
    auto goals = primo::smt::AnyOf::single(primo::smt::Goal(assertions));
    if (defaults.extended_simplification) {
      if (defaults.cite_simplification)
        goals = primo::smt::make_simplify_constant_ites()(context, goals);
      goals = primo::smt::make_simplify_extended_terms(
          {.rounds = defaults.extended_simplification_rounds,
           .distribute_equalities = defaults.ite_equality_distribution})(context, goals);
    }
    goals = primo::smt::make_lower_extended_terms()(context, goals);
    const auto lowered = goals.alternatives().front().assertions();
    const auto result = primo::smt::analyze_preprocessing_bounds(terms, lowered);
    const auto extra = primo::smt::materialize_preprocessing_bounds(terms, result);
    if (result.cancelled) throw std::runtime_error("unexpected cancellation");
    std::cerr << "bounds " << extra.size() << " work " << result.work << '\n';
    const auto& symbols = terms.symbols();
    for (const auto& sort : symbols.sorts()) {
      if (sort.kind == primo::core::SortKind::Uninterpreted)
        std::cout << "(declare-sort " << primo::smtlib::symbol_text(sort.name) << " 0)\n";
    }
    for (const auto& symbol : symbols.symbols()) {
      std::cout << "(declare-fun " << primo::smtlib::symbol_text(symbol.name) << " (";
      for (const auto sort : symbol.arg_sorts)
        std::cout << primo::smtlib::print_sort(symbols, sort) << ' ';
      std::cout << ") " << primo::smtlib::print_sort(symbols, symbol.result_sort) << ")\n";
    }
    // Give each reachable DAG node a fresh nullary name; shared terms expand
    // once, and user names cannot collide with the generated definitions.
    std::string prefix = "bound_probe_";
    for (;;) {
      bool collision = false;
      for (const auto& symbol : symbols.symbols()) {
        if (symbol.name.starts_with(prefix)) collision = true;
      }
      if (!collision) break;
      prefix += '_';
    }
    const auto name = [&prefix](auto id) { return prefix + std::to_string(id); };
    std::map<primo::core::TermId, bool> done;
    std::vector<std::pair<primo::core::TermId, bool>> pending;
    for (const auto id : lowered) pending.emplace_back(id, false);
    for (const auto id : extra) pending.emplace_back(id, false);
    while (!pending.empty()) {
      const auto [id, expanded] = pending.back();
      pending.pop_back();
      if (done.contains(id)) continue;
      const auto& node = terms.get(id);
      if (!expanded) {
        pending.emplace_back(id, true);
        for (const auto child : node.children) pending.emplace_back(child, false);
        continue;
      }
      using primo::core::TermKind;
      std::string expression;
      switch (node.kind) {
        case TermKind::BoolValue:
          expression = node.bool_value ? "true" : "false";
          break;
        case TermKind::Rational:
          expression = numeral(node.rational_value, node.sort == symbols.real_sort());
          break;
        case TermKind::Constant:
          expression = primo::smtlib::symbol_text(symbols.get_symbol(node.symbol).name);
          break;
        default: {
          std::string op;
          switch (node.kind) {
            case TermKind::Application:
              op = primo::smtlib::symbol_text(symbols.get_symbol(node.symbol).name);
              break;
            case TermKind::Equality:
              op = "=";
              break;
            case TermKind::Not:
              op = "not";
              break;
            case TermKind::And:
              op = "and";
              break;
            case TermKind::Or:
              op = "or";
              break;
            case TermKind::Implies:
              op = "=>";
              break;
            case TermKind::Plus:
              op = "+";
              break;
            case TermKind::Times:
              op = "* " + numeral(node.rational_value, node.sort == symbols.real_sort());
              break;
            case TermKind::Ite:
              op = "ite";
              break;
            case TermKind::IntDiv:
              expression = "(div " + name(node.children.at(0)) + " " +
                           numeral(node.rational_value, false) + ")";
              break;
            case TermKind::IntMod:
              expression = "(mod " + name(node.children.at(0)) + " " +
                           numeral(node.rational_value, false) + ")";
              break;
            case TermKind::Abs:
              op = "abs";
              break;
            case TermKind::Le:
              op = "<=";
              break;
            case TermKind::Lt:
              op = "<";
              break;
            default:
              throw std::runtime_error("unsupported probe term kind");
          }
          if (expression.empty()) {
            expression = "(" + op;
            for (const auto child : node.children) expression += " " + name(child);
            expression += ")";
          }
        }
      }
      std::cout << "(define-fun " << name(id) << " () "
                << primo::smtlib::print_sort(symbols, node.sort) << ' ' << expression << ")\n";
      done.emplace(id, true);
    }
    for (const auto id : lowered) std::cout << "(assert " << name(id) << ")\n";
    std::cout << "(assert (not (and true";
    for (const auto id : extra) std::cout << ' ' << name(id);
    std::cout << ")))(check-sat)\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 2;
  }
}
