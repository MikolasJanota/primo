#include "smt/qf_uf_export.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <optional>
#include <ostream>
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
#include "core/term_manager.hpp"
#include "sat/cadical_backend.hpp"
#include "sat/clause.hpp"
#include "sat/clause_subset.hpp"
#include "sat/ipasir_up.hpp"
#include "smt/check_sat.hpp"
#include "smt/clausifier.hpp"
#include "smt/frontend.hpp"
#include "smt/inconclusive_reason.hpp"
#include "smt/solver_error.hpp"
#include "smt/solver_runtime.hpp"
#include "smt/solver_session.hpp"
#include "smtlib/commands.hpp"
#include "smtlib/conflict_spool.hpp"
#include "smtlib/parser.hpp"
#include "smtlib/term_printer.hpp"
#include "util/statistics.hpp"

namespace primo::smt {
namespace {

// Original ground-QF_UF model argument and invariants:
// docs/leads/experimental-sort-erasure.md. Only the printed signature changes.
void append_signature(const core::SymbolTable& symbols, smtlib::ConflictSpool& document,
                      bool erase_sorts) {
  std::string common_sort;
  if (erase_sorts) {
    std::unordered_set<std::string> names;
    for (const auto& sort : symbols.sorts()) names.insert(sort.name);
    for (const auto& symbol : symbols.symbols()) names.insert(symbol.name);
    common_sort = "primo_uf_sort";
    std::size_t suffix = 0;
    while (names.contains(common_sort)) common_sort = "primo_uf_sort_" + std::to_string(suffix++);
    document.declaration("(declare-sort " + common_sort + " 0)");
  } else {
    for (const auto& sort : symbols.sorts()) {
      if (sort.kind == core::SortKind::Uninterpreted) {
        document.declaration("(declare-sort " + smtlib::symbol_text(sort.name) + " 0)");
      }
    }
  }
  const auto printed_sort = [&](core::SortId id) {
    const auto kind = symbols.get_sort(id).kind;
    if (kind != core::SortKind::Bool && kind != core::SortKind::Uninterpreted) {
      throw UnsupportedInput("--export-cnf supports QF_UF sorts only");
    }
    return erase_sorts && kind == core::SortKind::Uninterpreted ? common_sort
                                                                : smtlib::print_sort(symbols, id);
  };
  for (const auto& symbol : symbols.symbols()) {
    std::string declaration = "(declare-fun " + smtlib::symbol_text(symbol.name) + " (";
    bool first = true;
    for (const auto sort : symbol.arg_sorts) {
      if (!first) declaration += ' ';
      declaration += printed_sort(sort);
      first = false;
    }
    declaration += ") " + printed_sort(symbol.result_sort) + ")";
    document.declaration(declaration);
  }
}

}  // namespace

QfUfConflictCollector::QfUfConflictCollector(core::TermManager& terms,
                                             smtlib::ConflictSpool& document,
                                             ExportCnfReduction reduction, bool erase_sorts)
    : terms_(terms), document_(document), reduction_(reduction), erase_sorts_(erase_sorts) {}

void QfUfConflictCollector::record_input_clause(const sat::Clause& clause) {
  if (bound_) throw std::logic_error("clausifier emitted a clause after CNF export was sealed");
  input_clauses_.push_back(clause);
}

void QfUfConflictCollector::bind(const BooleanClausifier& clausifier) {
  const auto started = std::chrono::steady_clock::now();
  if (bound_) throw std::logic_error("CNF export cannot bind multiple solver goals");
  append_signature(terms_.symbols(), document_, erase_sorts_);
  atoms_.clear();
  for (const auto& atom : clausifier.theory_atoms()) {
    const int variable = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
    if (!atoms_.emplace(variable, std::make_pair(atom.term, atom.lit.value)).second) {
      throw std::logic_error("duplicate atom mapping in QF_UF conflict export");
    }
  }
  std::unordered_set<std::string> names;
  for (const auto& symbol : terms_.symbols().symbols()) names.insert(symbol.name);
  for (const auto& sort : terms_.symbols().sorts()) names.insert(sort.name);
  std::unordered_map<int, std::string> auxiliaries;
  std::size_t next_name = 0;
  // Discover auxiliary names before callbacks; callbacks cannot reenter the spool.
  for (const auto& clause : input_clauses_) {
    for (const auto literal : clause) {
      if (literal.value == 0) throw std::logic_error("zero literal in exported input clause");
      const int variable = literal.value > 0 ? literal.value : -literal.value;
      if (atoms_.contains(variable) || auxiliaries.contains(variable)) continue;
      std::string name;
      do {
        name = "primo_cnf_" + std::to_string(next_name++);
      } while (!names.insert(name).second);
      document_.declaration("(declare-fun " + name + " () Bool)");
      auxiliaries.emplace(variable, std::move(name));
    }
  }
  for (const auto& clause : input_clauses_) {
    document_.clause([&](std::ostream& output) {
      for (const auto literal : clause) {
        const int variable = literal.value > 0 ? literal.value : -literal.value;
        const auto atom = atoms_.find(variable);
        output << ' ';
        if (atom != atoms_.end()) {
          const bool negative = literal.value != atom->second.second;
          if (negative) output << "(not ";
          smtlib::print_ground_term(output, terms_, atom->second.first);
          if (negative) output << ')';
        } else {
          if (literal.value < 0) output << "(not ";
          output << auxiliaries.at(variable);
          if (literal.value < 0) output << ')';
        }
      }
    });
  }
  if (reduction_ == ExportCnfReduction::None) input_clauses_ = sat::Cnf{};
  bound_ = true;
  input_format_microseconds_ =
      static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                     std::chrono::steady_clock::now() - started)
                                     .count());
}

void QfUfConflictCollector::record_preprocessing_unsat() {
  // Native preprocessing returned false without invoking SAT. Use the same
  // clausifier to serialize that prepared result; conversion proof is deferred.
  sat::CadicalBackend backend;
  BooleanClausifier clausifier(backend, terms_, false,
                               [this](const sat::Clause& clause) { record_input_clause(clause); });
  clausifier.assert_formula(terms_.mk_bool(false));
  bind(clausifier);
}

void QfUfConflictCollector::record(const sat::Clause& clause) {
  const auto started = std::chrono::steady_clock::now();
  document_.conflict([&](std::ostream& output) {
    for (const auto literal : clause) {
      const int variable = literal.value > 0 ? literal.value : -literal.value;
      const auto found = atoms_.find(variable);
      if (found == atoms_.end())
        throw std::logic_error("unmapped SAT literal in QF_UF conflict export");
      output << ' ';
      // Learned-clause polarity is the complement of the conflicting fact.
      const bool negative = literal.value == found->second.second;
      if (negative) output << "(not ";
      smtlib::print_ground_term(output, terms_, found->second.first);
      if (negative) output << ')';
    }
  });
  if (reduction_ != ExportCnfReduction::None) theory_clauses_.push_back(clause);
  conflict_format_microseconds_ +=
      static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                     std::chrono::steady_clock::now() - started)
                                     .count());
}

sat::ClauseSubsetResult QfUfConflictCollector::reduce(sat::Terminator* terminator) const {
  if (!bound_ || reduction_ == ExportCnfReduction::None) {
    throw std::logic_error("invalid theory-lemma reduction state");
  }
  const auto mode = reduction_ == ExportCnfReduction::Core ? sat::ClauseSubsetMode::Core
                                                           : sat::ClauseSubsetMode::Mus;
  return sat::select_unsat_subset(input_clauses_, theory_clauses_, mode, terminator);
}

CheckSatOutcome export_qf_uf_cnf(std::string_view input, CheckSatOptions options) {
  try {
    if (options.incremental_mode != IncrementalMode::Rebuild ||
        options.qf_uf_portfolio != QfUfPortfolioMode::Off || options.preprocess_only ||
        options.experimental_split_bools != 0) {
      throw UnsupportedInput(
          "--export-cnf does not support incremental, portfolio, branching, or preprocess-only "
          "modes");
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

    smtlib::ConflictSpool document(options.export_cnf_reduction != ExportCnfReduction::None);
    // Validate even unused declarations, including on preprocessing SAT paths.
    const auto& symbols = frontend.terms().symbols();
    const auto require_sort = [&](core::SortId id) {
      const auto kind = symbols.get_sort(id).kind;
      if (kind != core::SortKind::Bool && kind != core::SortKind::Uninterpreted)
        throw UnsupportedInput("--export-cnf supports QF_UF sorts only");
    };
    for (const auto& symbol : symbols.symbols()) {
      require_sort(symbol.result_sort);
      for (const auto sort : symbol.arg_sorts) require_sort(sort);
    }
    QfUfConflictCollector collector(frontend.terms(), document, options.export_cnf_reduction,
                                    options.experimental_export_erase_sorts);
    const auto solve_started = std::chrono::steady_clock::now();
    auto outcome =
        check_sat_frontend_export_once(frontend, std::move(assertions), options, collector);
    if (options.experimental_export_erase_sorts) {
      std::size_t original_sorts = 0;
      for (const auto& sort : frontend.terms().symbols().sorts()) {
        if (sort.kind == core::SortKind::Uninterpreted) ++original_sorts;
      }
      outcome.statistics.set(":export.sort-erasure.enabled", 1);
      outcome.statistics.set(":export.sort-erasure.input-sorts", original_sorts);
    }
    outcome.statistics.set(
        ":export.solve-and-collect.us",
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                       std::chrono::steady_clock::now() - solve_started)
                                       .count()));
    outcome.statistics.set(":export.main-unsat", outcome.result == CheckSatResult::Unsat ? 1 : 0);
    outcome.statistics.set(":export.format-input.us", collector.input_format_microseconds());
    outcome.statistics.set(":export.format-conflicts.us", collector.conflict_format_microseconds());
    outcome.statistics.set(":export.spool.signature-bytes", document.signature_bytes());
    outcome.statistics.set(":export.spool.cnf-bytes", document.clause_bytes());
    outcome.statistics.set(":export.spool.conflict-bytes", document.conflict_bytes());
    const auto collected = document.conflict_count();
    std::optional<std::vector<std::size_t>> selected;
    outcome.statistics.set(":export.theory.conflicts-collected", collected);
    if (outcome.result == CheckSatResult::Unsat &&
        options.export_cnf_reduction != ExportCnfReduction::None) {
      SolveTerminator terminator(options.cancelled, outcome.statistics);
      const auto reduce_started = std::chrono::steady_clock::now();
      auto reduction = collector.reduce(&terminator);
      outcome.statistics.set(
          ":export.reduction.us",
          static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                         std::chrono::steady_clock::now() - reduce_started)
                                         .count()));
      outcome.statistics.set(":export.reduction.sat-calls", reduction.sat_calls);
      outcome.statistics.set(":export.reduction.core-removed", reduction.core_removed);
      outcome.statistics.set(":export.reduction.block-queries",
                             reduction.progression.block_queries);
      outcome.statistics.set(":export.reduction.binary-queries",
                             reduction.progression.binary_queries);
      outcome.statistics.set(":export.reduction.max-block-size",
                             reduction.progression.max_block_size);
      if (reduction.result == sat::SatResult::Sat) {
        throw std::logic_error("captured CNF plus theory lemmas is propositionally SAT");
      }
      if (reduction.result != sat::SatResult::Unsat) {
        outcome.result = CheckSatResult::Unknown;
        outcome.message = "theory-lemma reduction did not complete; no artifact written";
        outcome.reason = terminator.cancelled_reached() ? InconclusiveReason::Cancelled
                                                        : InconclusiveReason::Unknown;
      } else {
        outcome.statistics.set(":export.reduction.completed", 1);
        selected = std::move(reduction.selected);
      }
    }
    const auto retained = selected ? selected->size() : collected;
    outcome.statistics.set(":export.theory.conflicts-removed", collected - retained);
    outcome.statistics.set(":export.cnf.clauses", document.clause_count());
    outcome.statistics.set(":export.theory.conflicts", retained);
    if (outcome.result == CheckSatResult::Unsat) {
      const auto write_started = std::chrono::steady_clock::now();
      document.seal();
      std::ofstream output(options.export_cnf);
      if (!output) throw std::runtime_error("cannot open CNF output file: " + options.export_cnf);
      if (selected)
        document.write_to(output, *selected);
      else
        document.write_to(output);
      output.close();
      if (!output) throw std::runtime_error("cannot write CNF output file: " + options.export_cnf);
      outcome.statistics.set(
          ":export.final-write.us",
          static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                         std::chrono::steady_clock::now() - write_started)
                                         .count()));
    }
    return outcome;
  } catch (const std::exception& error) {
    return {CheckSatResult::Unsupported, error.what(), {}, ""};
  }
}

}  // namespace primo::smt
