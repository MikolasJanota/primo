#include "smt/qf_uf_export.hpp"

#include <exception>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
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

// Restrict the first producer to atoms whose term arguments introduce no
// hidden Boolean definitions. Full formula-to-CNF export is separate work.
void require_atomic_terms(const core::TermManager& terms, core::TermId root) {
  std::vector<core::TermId> pending{root};
  std::unordered_set<core::TermId> seen;
  while (!pending.empty()) {
    const auto id = pending.back();
    pending.pop_back();
    if (!seen.insert(id).second) continue;
    const auto& term = terms.get(id);
    if (term.kind != core::TermKind::BoolValue && term.kind != core::TermKind::Constant &&
        term.kind != core::TermKind::Application) {
      throw UnsupportedInput(
          "--export-cnf requires atomic term arguments (no Boolean expressions or ite)");
    }
    for (const auto child : term.children) pending.push_back(child);
  }
}

std::string literal_text(const core::TermManager& terms, core::TermId literal) {
  auto atom = literal;
  if (terms.get(atom).kind == core::TermKind::Not) atom = terms.get(atom).children.front();
  const auto& node = terms.get(atom);
  if (node.kind == core::TermKind::Equality) {
    for (const auto child : node.children) require_atomic_terms(terms, child);
  } else {
    require_atomic_terms(terms, atom);
  }
  return smtlib::print_term(terms, literal);
}

void append_cnf(const core::TermManager& terms, core::TermId root,
                smtlib::ConflictDocument& document) {
  std::vector<core::TermId> pending{root};
  while (!pending.empty()) {
    const auto id = pending.back();
    pending.pop_back();
    const auto& term = terms.get(id);
    if (term.kind == core::TermKind::And) {
      pending.insert(pending.end(), term.children.rbegin(), term.children.rend());
      continue;
    }
    std::vector<std::string> clause;
    std::vector<core::TermId> disjuncts{id};
    while (!disjuncts.empty()) {
      const auto disjunct = disjuncts.back();
      disjuncts.pop_back();
      const auto& node = terms.get(disjunct);
      if (node.kind == core::TermKind::Or) {
        disjuncts.insert(disjuncts.end(), node.children.rbegin(), node.children.rend());
      } else {
        clause.push_back(literal_text(terms, disjunct));
      }
    }
    document.clauses.push_back(std::move(clause));
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

// Preserve the two-valued Bool invariant without frontend naming clauses.
// Snapshot root IDs before registration can grow the clausifier atom vector.
void QfUfConflictCollector::prepare(BooleanClausifier& clausifier) {
  std::vector<core::TermId> pending;
  for (const auto& atom : clausifier.theory_atoms()) pending.push_back(atom.term);
  std::unordered_set<core::TermId> seen;
  while (!pending.empty()) {
    const auto id = pending.back();
    pending.pop_back();
    if (!seen.insert(id).second) continue;
    const auto& term = terms_.get(id);
    const bool boolean = term.sort == terms_.symbols().bool_sort();
    const auto kind = term.kind;
    pending.insert(pending.end(), term.children.begin(), term.children.end());
    if (boolean && (kind == core::TermKind::Constant || kind == core::TermKind::Application)) {
      static_cast<void>(clausifier.ensure_theory_atom(id));
    }
  }
}

void QfUfConflictCollector::bind(const BooleanClausifier& clausifier) {
  atoms_.clear();
  for (const auto& atom : clausifier.theory_atoms()) {
    const int variable = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
    if (!atoms_.emplace(variable, std::make_pair(atom.term, atom.lit.value)).second) {
      throw std::logic_error("duplicate atom mapping in QF_UF conflict export");
    }
  }
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
    Frontend frontend(false, FrontendBooleanMode::PreserveAtoms);
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
        case Kind::Assert:
          if (!logic)
            throw UnsupportedInput("set-logic QF_UF must precede declarations and assertions");
          break;
        case Kind::CheckSat:
          checked = true;
          break;
        default:
          throw UnsupportedInput(
              "command unsupported by --export-cnf (no incremental solving or definitions)");
      }
      frontend.process_command(command, assertions);
    }
    if (!logic || !checked) throw UnsupportedInput("--export-cnf requires QF_UF and one check-sat");

    smtlib::ConflictDocument document;
    append_signature(frontend.terms().symbols(), document);
    for (const auto assertion : assertions) append_cnf(frontend.terms(), assertion, document);
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
