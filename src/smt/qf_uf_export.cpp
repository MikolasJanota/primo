#include "smt/qf_uf_export.hpp"

#include <cstddef>
#include <exception>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "core/sort.hpp"
#include "core/symbol_table.hpp"
#include "core/term.hpp"
#include "core/term_manager.hpp"
#include "sat/clause.hpp"
#include "smt/check_sat.hpp"
#include "smt/clausifier.hpp"
#include "smt/frontend.hpp"
#include "smt/solver_engines.hpp"
#include "smt/solver_error.hpp"
#include "smtlib/commands.hpp"
#include "smtlib/conflict_document.hpp"
#include "smtlib/parser.hpp"
#include "smtlib/term_printer.hpp"
#include "util/statistics.hpp"

namespace primo::smt {
namespace {

// The ordinary frontend already names compound Boolean arguments and lowers
// Boolean equality. Check the fragment, not the shape of its Boolean structure.
void require_supported_terms(const core::TermManager& terms, core::TermId root) {
  std::vector<core::TermId> pending{root};
  std::unordered_set<core::TermId> seen;
  while (!pending.empty()) {
    const auto id = pending.back();
    pending.pop_back();
    if (!seen.insert(id).second) continue;
    const auto& term = terms.get(id);
    switch (term.kind) {
      case core::TermKind::BoolValue:
      case core::TermKind::Constant:
      case core::TermKind::Application:
      case core::TermKind::Equality:
      case core::TermKind::Not:
      case core::TermKind::And:
      case core::TermKind::Or:
      case core::TermKind::Implies:
        break;
      default:
        throw UnsupportedInput(
            "--export-cnf supports ground QF_UF Boolean structure; ite and other extended terms "
            "require lowering");
    }
    pending.insert(pending.end(), term.children.begin(), term.children.end());
  }
}

void append_signature(const core::SymbolTable& symbols, smtlib::ConflictDocument& document) {
  for (const auto& sort : symbols.sorts()) {
    if (sort.kind == core::SortKind::Uninterpreted) {
      document.signature.push_back("(declare-sort " + smtlib::symbol_text(sort.name) + " 0)");
    }
  }
  for (const auto& symbol : symbols.symbols()) {
    const auto require_sort = [&](core::SortId id) {
      const auto kind = symbols.get_sort(id).kind;
      if (kind != core::SortKind::Bool && kind != core::SortKind::Uninterpreted) {
        throw UnsupportedInput("--export-cnf supports QF_UF sorts only");
      }
    };
    require_sort(symbol.result_sort);
    std::string declaration = "(declare-fun " + smtlib::symbol_text(symbol.name) + " (";
    bool first = true;
    for (const auto sort : symbol.arg_sorts) {
      require_sort(sort);
      if (!first) declaration += ' ';
      declaration += smtlib::print_sort(symbols, sort);
      first = false;
    }
    declaration += ") " + smtlib::print_sort(symbols, symbol.result_sort) + ")";
    document.signature.push_back(std::move(declaration));
  }
}

}  // namespace

QfUfConflictCollector::QfUfConflictCollector(const core::TermManager& terms,
                                             smtlib::ConflictDocument& document)
    : terms_(terms), document_(document) {}

void QfUfConflictCollector::record_input_clause(const sat::Clause& clause) {
  if (bound_) throw std::logic_error("clausifier emitted a clause after CNF export was sealed");
  input_clauses_.push_back(clause);
}

void QfUfConflictCollector::bind(const BooleanClausifier& clausifier) {
  atoms_.clear();
  // Each entry's signed literal denotes its positive printed atom. This also
  // handles a mapping whose SAT representative has negative polarity.
  std::unordered_map<int, std::pair<std::string, int>> printed;
  for (const auto& atom : clausifier.theory_atoms()) {
    const int variable = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
    if (!atoms_.emplace(variable, std::make_pair(atom.term, atom.lit.value)).second) {
      throw std::logic_error("duplicate atom mapping in QF_UF conflict export");
    }
    printed.emplace(variable,
                    std::make_pair(smtlib::print_term(terms_, atom.term), atom.lit.value));
  }
  std::unordered_set<std::string> names;
  for (const auto& symbol : terms_.symbols().symbols()) names.insert(symbol.name);
  for (const auto& sort : terms_.symbols().sorts()) names.insert(sort.name);
  std::size_t next_name = 0;
  for (const auto& clause : input_clauses_) {
    std::vector<std::string> literals;
    for (const auto literal : clause) {
      if (literal.value == 0) throw std::logic_error("zero literal in exported input clause");
      const int variable = literal.value > 0 ? literal.value : -literal.value;
      auto found = printed.find(variable);
      if (found == printed.end()) {
        std::string name;
        do {
          name = "primo_cnf_" + std::to_string(next_name++);
        } while (!names.insert(name).second);
        const auto symbol = smtlib::symbol_text(name);
        document_.signature.push_back("(declare-fun " + symbol + " () Bool)");
        found = printed.emplace(variable, std::make_pair(symbol, variable)).first;
      }
      auto text = found->second.first;
      if (literal.value != found->second.second) text = "(not " + text + ")";
      literals.push_back(std::move(text));
    }
    document_.clauses.push_back(std::move(literals));
  }
  input_clauses_.clear();
  bound_ = true;
}

void QfUfConflictCollector::record(const sat::Clause& clause) {
  std::vector<std::string> conflict;
  for (const auto literal : clause) {
    const int variable = literal.value > 0 ? literal.value : -literal.value;
    const auto found = atoms_.find(variable);
    if (found == atoms_.end()) {
      throw std::logic_error("unmapped SAT literal in QF_UF conflict export");
    }
    auto text = smtlib::print_term(terms_, found->second.first);
    // The learned-clause literal is the complement of the conflicting fact.
    if (literal.value == found->second.second) text = "(not " + text + ")";
    conflict.push_back(std::move(text));
  }
  document_.conflicts.push_back(std::move(conflict));
}

CheckSatOutcome export_qf_uf_cnf(std::string_view input, CheckSatOptions options) {
  try {
    if (options.incremental_mode != IncrementalMode::Rebuild ||
        options.qf_uf_portfolio != QfUfPortfolioMode::Off || options.preprocess_only) {
      throw UnsupportedInput(
          "--export-cnf does not support incremental, portfolio, or preprocess-only modes");
    }
    Frontend frontend;
    std::vector<core::TermId> assertions;
    bool checked = false;
    bool exited = false;
    bool logic = false;
    for (const auto& command : smtlib::Parser(input).parse_commands()) {
      using Kind = smtlib::CommandKind;
      if (exited) throw UnsupportedInput("commands after exit are not supported by --export-cnf");
      if (command.kind == Kind::Ignored) continue;
      if (command.kind == Kind::Exit) {
        exited = true;
        continue;
      }
      if (checked) throw UnsupportedInput("--export-cnf requires exactly one final check-sat");
      switch (command.kind) {
        case Kind::SetLogic:
          if (logic || command.logic != "QF_UF") {
            throw UnsupportedInput("--export-cnf requires one (set-logic QF_UF)");
          }
          logic = true;
          break;
        case Kind::DeclareSort:
        case Kind::DeclareFun:
        case Kind::DefineFun:
        case Kind::Assert:
          if (!logic)
            throw UnsupportedInput("set-logic QF_UF must precede declarations and assertions");
          break;
        case Kind::CheckSat:
          checked = true;
          break;
        default:
          throw UnsupportedInput("command unsupported by --export-cnf (no incremental solving)");
      }
      frontend.process_command(command, assertions);
    }
    if (!logic || !checked) throw UnsupportedInput("--export-cnf requires QF_UF and one check-sat");

    smtlib::ConflictDocument document;
    append_signature(frontend.terms().symbols(), document);
    for (const auto assertion : assertions) require_supported_terms(frontend.terms(), assertion);
    QfUfConflictCollector collector(frontend.terms(), document);
    // Use the existing lazy solver without preprocessing or propagation lemmas.
    // No optimized congruence algorithm or new solving algorithm is introduced.
    options.euf_propagation = EufPropagationMode::Off;
    options.euf_engine = EufEngine::Lazy;
    options.pure_theory_filter = PureTheoryFilterMode::Off;
    options.produce_model = false;
    options.top_level_or_tseitin = false;
    util::Statistics statistics;
    auto outcome =
        solve_qf_uf_engine(frontend.terms(), assertions, options, statistics, {}, &collector);
    outcome.statistics.set(":export.cnf.clauses", document.clauses.size());
    outcome.statistics.set(":export.theory.conflicts", document.conflicts.size());
    if (outcome.result == CheckSatResult::Unsat) {
      std::ofstream output(options.export_cnf);
      if (!output) throw std::runtime_error("cannot open CNF output file: " + options.export_cnf);
      smtlib::print_conflict_document(output, document);
      output.close();
      if (!output) throw std::runtime_error("cannot write CNF output file: " + options.export_cnf);
    }
    return outcome;
  } catch (const std::exception& error) {
    return {CheckSatResult::Unsupported, error.what(), {}, ""};
  }
}

}  // namespace primo::smt
