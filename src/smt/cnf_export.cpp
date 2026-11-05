#include "smt/cnf_export.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <span>
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
#include "sat/cadical_backend.hpp"
#include "sat/clause.hpp"
#include "sat/clause_subset.hpp"
#include "sat/ipasir_up.hpp"
#include "smt/check_sat.hpp"
#include "smt/clausifier.hpp"
#include "smt/frontend.hpp"
#include "smt/inconclusive_reason.hpp"
#include "smt/numeric_clause_spool.hpp"
#include "smt/solver_error.hpp"
#include "smt/solver_runtime.hpp"
#include "smt/solver_session.hpp"
#include "smtlib/commands.hpp"
#include "smtlib/conflict_spool.hpp"
#include "smtlib/parser.hpp"
#include "smtlib/term_printer.hpp"
#include "theory/arithmetic/lia_certificate.hpp"
#include "theory/arithmetic/lra_atom.hpp"
#include "theory/arithmetic/lra_certificate.hpp"
#include "util/statistics.hpp"

namespace primo::smt {
namespace {

class CertificateUnavailable final : public std::runtime_error {
 public:
  CertificateUnavailable(const char* message, InconclusiveReason cause)
      : std::runtime_error(message), reason(cause) {}
  InconclusiveReason reason;
};

core::SortKind object_sort(ExportCnfTheory theory) {
  if (theory == ExportCnfTheory::Uf) return core::SortKind::Uninterpreted;
  return theory == ExportCnfTheory::Lra ? core::SortKind::Real : core::SortKind::Int;
}

// Original ground-QF_UF model argument and invariants:
// docs/leads/experimental-sort-erasure.md. Only the printed signature changes.
void append_signature(const core::SymbolTable& symbols, smtlib::ConflictSpool& document,
                      bool erase_sorts, ExportCnfTheory theory) {
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
    if (kind != core::SortKind::Bool && kind != object_sort(theory)) {
      throw UnsupportedInput("unsupported sort in CNF certificate signature");
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

CnfConflictCollector::CnfConflictCollector(core::TermManager& terms,
                                           smtlib::ConflictSpool& document,
                                           ExportCnfReduction reduction, bool erase_sorts,
                                           ExportCnfTheory theory)
    : terms_(terms),
      document_(document),
      reduction_(reduction),
      erase_sorts_(erase_sorts),
      theory_(theory) {
  if (theory_ != ExportCnfTheory::Uf)
    numeric_ = std::make_unique<NumericClauseSpool>(reduction != ExportCnfReduction::None);
}

CnfConflictCollector::~CnfConflictCollector() = default;

std::size_t CnfConflictCollector::collected_count() const {
  return numeric_ ? numeric_->size() : document_.conflict_count();
}

void CnfConflictCollector::record_input_clause(const sat::Clause& clause) {
  if (bound_) throw std::logic_error("clausifier emitted a clause after CNF export was sealed");
  input_clauses_.push_back(clause);
}

void CnfConflictCollector::bind(const BooleanClausifier& clausifier) {
  const auto started = std::chrono::steady_clock::now();
  if (bound_) throw std::logic_error("CNF export cannot bind multiple solver goals");
  append_signature(terms_.symbols(), document_, erase_sorts_, theory_);
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

void CnfConflictCollector::record_preprocessing_unsat() {
  // Native preprocessing returned false without invoking SAT. Use the same
  // clausifier to serialize that prepared result; conversion proof is deferred.
  sat::CadicalBackend backend;
  BooleanClausifier clausifier(backend, terms_, false,
                               [this](const sat::Clause& clause) { record_input_clause(clause); });
  clausifier.assert_formula(terms_.mk_bool(false));
  bind(clausifier);
}

void CnfConflictCollector::record(const sat::Clause& clause) {
  const auto started = std::chrono::steady_clock::now();
  if (numeric_) {
    numeric_->append(clause);
  } else
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

bool CnfConflictCollector::reconstruct(std::optional<std::span<const std::size_t>> selected,
                                       const std::function<bool()>& cancelled,
                                       util::Statistics& statistics) {
  if (!numeric_) return true;
  if (theory_ == ExportCnfTheory::Lia) return reconstruct_lia(selected, cancelled, statistics);
  using theory::arithmetic::LraCertificatePremise;
  using theory::arithmetic::LraCertificateStatus;
  const auto poll = [&] { return cancelled && cancelled(); };
  statistics.set(":export.spool.numeric-lemma-bytes", numeric_->bytes());
  const auto retained = selected ? selected->size() : numeric_->size();
  statistics.set(":export.reconstruction.selected", retained);
  statistics.set(":export.reconstruction.completed", 0);
  statistics.set(":export.reconstruction.lemmas", 0);
  std::size_t pivots = 0;
  std::size_t entries = 0;
  std::size_t nonzeros = 0;
  std::size_t largest_weight_bytes = 0;
  std::size_t largest_variables = 0;
  for (std::size_t position = 0; position < retained; ++position) {
    if (poll()) return false;
    const auto index = selected ? (*selected)[position] : position;
    const auto clause = numeric_->read(index);
    std::vector<LraCertificatePremise> premises;
    std::vector<std::pair<core::TermId, bool>> facts;
    for (const auto literal : clause) {
      if (poll()) return false;
      const int variable = literal.value > 0 ? literal.value : -literal.value;
      const auto found = atoms_.find(variable);
      if (found == atoms_.end()) throw std::logic_error("unmapped LRA certificate literal");
      const auto [atom, positive_literal] = found->second;
      const bool negative = literal.value == positive_literal;
      const auto kind = terms_.get(atom).kind;
      if (kind != core::TermKind::Le && kind != core::TermKind::Lt)
        throw UnsupportedInput("non-inequality premise in LRA certificate");
      std::vector<core::TermId> pending = terms_.get(atom).children;
      std::unordered_set<core::TermId> seen;
      while (!pending.empty()) {
        if (poll()) return false;
        const auto id = pending.back();
        pending.pop_back();
        if (!seen.insert(id).second) continue;
        const auto& term = terms_.get(id);
        if (term.kind != core::TermKind::Constant && term.kind != core::TermKind::Rational &&
            term.kind != core::TermKind::Plus && term.kind != core::TermKind::Times)
          throw UnsupportedInput("non-affine premise in LRA certificate");
        pending.insert(pending.end(), term.children.begin(), term.children.end());
      }
      auto normalized = theory::arithmetic::normalize_atom(terms_, atom);
      LraCertificatePremise premise;
      premise.rhs = normalized.rhs;
      premise.strict = normalized.relation == theory::arithmetic::LraRelation::Lt;
      if (negative) {
        normalized.polynomial.scale(util::Rational(-1));
        premise.rhs = -premise.rhs;
        premise.strict = !premise.strict;
      }
      for (const auto& [id, coefficient] : normalized.polynomial.terms())
        premise.coefficients.emplace_back(id, coefficient);
      premises.push_back(std::move(premise));
      facts.emplace_back(atom, negative);
    }
    statistics.set(":export.reconstruction.attempted", position + 1);
    const auto witness = theory::arithmetic::reconstruct_lra_certificate(premises, cancelled);
    if (witness.status == LraCertificateStatus::Interrupted) return false;
    if (witness.status != LraCertificateStatus::Verified)
      throw std::logic_error("selected LRA lemma has no weighted contradiction");
    pivots += witness.pivots;
    nonzeros += witness.nonzeros;
    entries += witness.weights.size();
    largest_variables = std::max(largest_variables, witness.variables);
    document_.conflict([&](std::ostream& output) {
      for (std::size_t i = 0; i < facts.size(); ++i) {
        output << " (";
        if (facts[i].second) output << "(not ";
        smtlib::print_ground_term(output, terms_, facts[i].first);
        if (facts[i].second) output << ')';
        const auto& value = witness.weights[i];
        const auto text = value.to_string();
        largest_weight_bytes = std::max(largest_weight_bytes, text.size());
        const auto slash = text.find('/');
        if (slash == std::string::npos) {
          output << ' ' << text;
        } else {
          output << " (/ " << text.substr(0, slash) << ' ' << text.substr(slash + 1) << ')';
        }
        output << ')';
      }
    });
    statistics.set(":export.reconstruction.lemmas", position + 1);
    statistics.set(":export.reconstruction.pivots", pivots);
    statistics.set(":export.reconstruction.entries", entries);
    statistics.set(":export.reconstruction.nonzeros", nonzeros);
    statistics.set(":export.reconstruction.max-variables", largest_variables);
    statistics.set(":export.reconstruction.max-weight-bytes", largest_weight_bytes);
  }
  statistics.set(":export.reconstruction.pivots", pivots);
  statistics.set(":export.reconstruction.entries", entries);
  statistics.set(":export.reconstruction.nonzeros", nonzeros);
  if (poll()) return false;
  statistics.set(":export.reconstruction.completed", 1);
  return true;
}

bool CnfConflictCollector::reconstruct_lia(std::optional<std::span<const std::size_t>> selected,
                                           const std::function<bool()>& cancelled,
                                           util::Statistics& statistics) {
  using theory::arithmetic::LiaCertificatePremise;
  using theory::arithmetic::LiaCertificateStatus;
  const auto poll = [&] { return cancelled && cancelled(); };
  const auto retained = selected ? selected->size() : numeric_->size();
  statistics.set(":export.spool.numeric-lemma-bytes", numeric_->bytes());
  statistics.set(":export.reconstruction.selected", retained);
  statistics.set(":export.reconstruction.completed", 0);
  statistics.set(":export.reconstruction.lemmas", 0);
  std::size_t cuts = 0;
  std::size_t pairs = 0;
  std::size_t projections = 0;
  std::size_t exact_projections = 0;
  std::size_t linear_checks = 0;
  std::size_t linear_pivots = 0;
  std::size_t pruned = 0;
  std::size_t entries = 0;
  for (std::size_t position = 0; position < retained; ++position) {
    if (poll()) return false;
    const auto clause = numeric_->read(selected ? (*selected)[position] : position);
    std::vector<LiaCertificatePremise> premises;
    std::vector<std::pair<core::TermId, bool>> facts;
    for (const auto literal : clause) {
      if (poll()) return false;
      const int variable = literal.value > 0 ? literal.value : -literal.value;
      const auto found = atoms_.find(variable);
      if (found == atoms_.end()) throw std::logic_error("unmapped LIA certificate literal");
      const auto [atom, positive_literal] = found->second;
      const bool negative = literal.value == positive_literal;
      const auto kind = terms_.get(atom).kind;
      if (kind != core::TermKind::Le && kind != core::TermKind::Lt)
        throw UnsupportedInput("non-inequality premise in LIA certificate");
      std::vector<core::TermId> pending = terms_.get(atom).children;
      std::unordered_set<core::TermId> seen;
      while (!pending.empty()) {
        if (poll()) return false;
        const auto id = pending.back();
        pending.pop_back();
        if (!seen.insert(id).second) continue;
        const auto& term = terms_.get(id);
        if (term.sort != terms_.symbols().int_sort() ||
            (term.kind != core::TermKind::Constant && term.kind != core::TermKind::Rational &&
             term.kind != core::TermKind::Plus && term.kind != core::TermKind::Times))
          throw UnsupportedInput("non-affine or non-Int premise in LIA certificate");
        pending.insert(pending.end(), term.children.begin(), term.children.end());
      }
      auto normalized = theory::arithmetic::normalize_atom(terms_, atom);
      bool strict = normalized.relation == theory::arithmetic::LraRelation::Lt;
      if (negative) {
        normalized.polynomial.scale(util::Rational(-1));
        normalized.rhs = -normalized.rhs;
        strict = !strict;
      }
      LiaCertificatePremise premise;
      premise.rhs = normalized.rhs - util::Rational(strict ? 1 : 0);
      for (const auto& [id, coefficient] : normalized.polynomial.terms())
        premise.coefficients.emplace_back(id, coefficient);
      premises.push_back(std::move(premise));
      facts.emplace_back(atom, negative);
    }
    statistics.set(":export.reconstruction.attempted", position + 1);
    const auto proof = theory::arithmetic::reconstruct_lia_certificate(premises, cancelled);
    cuts += proof.generated_steps;
    pairs += proof.pairs;
    projections += proof.projections;
    exact_projections += proof.exact_projections;
    linear_checks += proof.linear_checks;
    linear_pivots += proof.linear_pivots;
    pruned += proof.pruned_steps;
    statistics.set(":export.reconstruction.lia-generated-cuts", cuts);
    statistics.set(":export.reconstruction.lia-pairs", pairs);
    statistics.set(":export.reconstruction.lia-projections", projections);
    statistics.set(":export.reconstruction.lia-exact-projections", exact_projections);
    statistics.set(":export.reconstruction.lia-linear-checks", linear_checks);
    statistics.set(":export.reconstruction.lia-linear-pivots", linear_pivots);
    statistics.set(":export.reconstruction.lia-pruned-cuts", pruned);
    if (proof.status == LiaCertificateStatus::Interrupted) return false;
    if (proof.status == LiaCertificateStatus::ResourceLimit)
      throw CertificateUnavailable("LIA cut reconstruction limit reached; no artifact written",
                                   InconclusiveReason::ResourceLimit);
    if (proof.status != LiaCertificateStatus::Verified)
      throw CertificateUnavailable(
          "bounded LIA reconstruction found no cut proof; no artifact written",
          InconclusiveReason::Unknown);
    const auto name = [&](std::size_t reference) {
      return reference < premises.size() ? "p" + std::to_string(reference)
                                         : "s" + std::to_string(reference - premises.size());
    };
    document_.conflict([&](std::ostream& output) {
      output << "\n      (premises";
      for (std::size_t i = 0; i < facts.size(); ++i) {
        output << "\n        (" << name(i) << ' ';
        if (facts[i].second) output << "(not ";
        smtlib::print_ground_term(output, terms_, facts[i].first);
        if (facts[i].second) output << ')';
        output << ')';
      }
      output << ")\n      (proof";
      for (std::size_t i = 0; i < proof.cuts.size(); ++i) {
        const auto& cut = proof.cuts[i];
        output << "\n        (cut " << name(premises.size() + i) << ' ' << cut.divisor.to_string();
        for (const auto& [reference, weight] : cut.antecedents)
          output << " (" << name(reference) << ' ' << weight.to_string() << ')';
        output << ')';
      }
      output << "\n        (contradiction " << name(proof.contradiction) << "))";
    });
    entries += premises.size();
    statistics.set(":export.reconstruction.entries", entries);
    statistics.set(":export.reconstruction.lemmas", position + 1);
  }
  if (poll()) return false;
  statistics.set(":export.reconstruction.completed", 1);
  return true;
}

sat::ClauseSubsetResult CnfConflictCollector::reduce(sat::Terminator* terminator) const {
  if (!bound_ || reduction_ == ExportCnfReduction::None) {
    throw std::logic_error("invalid theory-lemma reduction state");
  }
  const auto mode = reduction_ == ExportCnfReduction::Core ? sat::ClauseSubsetMode::Core
                                                           : sat::ClauseSubsetMode::Mus;
  return sat::select_unsat_subset(input_clauses_, theory_clauses_, mode, terminator);
}

CheckSatOutcome export_cnf_once(std::string_view input, CheckSatOptions options) {
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
    ExportCnfTheory theory = ExportCnfTheory::Uf;
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
          if (logic || (command.logic != "QF_UF" && command.logic != "QF_LRA" &&
                        command.logic != "QF_LIA")) {
            throw UnsupportedInput("--export-cnf requires one QF_UF, QF_LRA or QF_LIA set-logic");
          }
          theory = command.logic == "QF_LRA"   ? ExportCnfTheory::Lra
                   : command.logic == "QF_LIA" ? ExportCnfTheory::Lia
                                               : ExportCnfTheory::Uf;
          logic = true;
          break;
        case Kind::DeclareSort:
        case Kind::DeclareFun:
        case Kind::DefineFun:
        case Kind::Assert:
          if (!logic) throw UnsupportedInput("set-logic must precede declarations and assertions");
          break;
        case Kind::CheckSat:
          checked = true;
          break;
        default:
          throw UnsupportedInput("command unsupported by --export-cnf (no incremental solving)");
      }
      frontend.process_command(command, assertions);
    }
    if (!logic || !checked)
      throw UnsupportedInput("--export-cnf requires an admitted logic and one check-sat");

    if (theory != ExportCnfTheory::Uf && options.experimental_export_erase_sorts)
      throw UnsupportedInput("sort erasure is only supported for QF_UF export");
    smtlib::ConflictSpool document(
        theory == ExportCnfTheory::Uf && options.export_cnf_reduction != ExportCnfReduction::None,
        theory == ExportCnfTheory::Uf    ? smtlib::ConflictFormat::QfUfV1
        : theory == ExportCnfTheory::Lra ? smtlib::ConflictFormat::QfLraV2
                                         : smtlib::ConflictFormat::QfLiaV3);
    // Validate even unused declarations, including on preprocessing SAT paths.
    const auto& symbols = frontend.terms().symbols();
    if (theory != ExportCnfTheory::Uf) {
      for (const auto& sort : symbols.sorts()) {
        if (sort.kind == core::SortKind::Uninterpreted)
          throw UnsupportedInput("uninterpreted sorts are unsupported in arithmetic certificates");
      }
    }
    const auto require_sort = [&](core::SortId id) {
      const auto kind = symbols.get_sort(id).kind;
      if (kind != core::SortKind::Bool && kind != object_sort(theory))
        throw UnsupportedInput("unsupported sort in CNF certificate signature");
    };
    for (const auto& symbol : symbols.symbols()) {
      if (theory != ExportCnfTheory::Uf && !symbol.arg_sorts.empty())
        throw UnsupportedInput("arithmetic export requires constant declarations");
      require_sort(symbol.result_sort);
      for (const auto sort : symbol.arg_sorts) require_sort(sort);
    }
    CnfConflictCollector collector(frontend.terms(), document, options.export_cnf_reduction,
                                   options.experimental_export_erase_sorts, theory);
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
    const auto collected = collector.collected_count();
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
    if (outcome.result == CheckSatResult::Unsat && theory != ExportCnfTheory::Uf) {
      const auto started = std::chrono::steady_clock::now();
      bool timed_out = false;
      const auto stopped = [&] {
        timed_out = std::chrono::steady_clock::now() - started >= std::chrono::seconds(60);
        return timed_out || (options.cancelled && options.cancelled());
      };
      try {
        const std::optional<std::span<const std::size_t>> selection =
            selected ? std::optional<std::span<const std::size_t>>(*selected) : std::nullopt;
        if (!collector.reconstruct(selection, stopped, outcome.statistics)) {
          outcome.result = CheckSatResult::Unknown;
          outcome.message = "arithmetic witness reconstruction interrupted; no artifact written";
          outcome.reason =
              timed_out ? InconclusiveReason::ResourceLimit : InconclusiveReason::Cancelled;
        }
      } catch (const CertificateUnavailable& error) {
        outcome.result = CheckSatResult::Unknown;
        outcome.reason = error.reason;
        outcome.message = error.what();
      } catch (const std::exception& error) {
        outcome.result = CheckSatResult::Unsupported;
        outcome.reason = InconclusiveReason::Unsupported;
        outcome.message = error.what();
      }
      outcome.statistics.set(
          ":export.reconstruction.us",
          static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                         std::chrono::steady_clock::now() - started)
                                         .count()));
      outcome.statistics.set(":export.spool.conflict-bytes", document.conflict_bytes());
    }
    if (outcome.result == CheckSatResult::Unsat) {
      const auto write_started = std::chrono::steady_clock::now();
      document.seal();
      std::ofstream output(options.export_cnf);
      if (!output) throw std::runtime_error("cannot open CNF output file: " + options.export_cnf);
      if (selected && theory == ExportCnfTheory::Uf)
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
