#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "core/ids.hpp"
#include "sat/clause.hpp"
#include "sat/clause_subset.hpp"
#include "sat/cnf.hpp"
#include "smt/check_sat.hpp"

namespace primo::core {
class TermManager;
}
namespace primo::smtlib {
class ConflictSpool;
}

namespace primo::smt {
class BooleanClausifier;
class NumericClauseSpool;

enum class ExportCnfTheory { Uf, Lra, Lia };

// Borrows the query term manager and text spool through publication. Copies SAT
// clauses for reduction; arithmetic spools numeric clauses for selected-only
// witness reconstruction. Retains no clausifier, term-node or stream borrows.
class CnfConflictCollector {
 public:
  CnfConflictCollector(core::TermManager& terms, smtlib::ConflictSpool& document,
                       ExportCnfReduction reduction = ExportCnfReduction::None,
                       bool erase_sorts = false, ExportCnfTheory theory = ExportCnfTheory::Uf);
  ~CnfConflictCollector();
  void record_preprocessing_unsat();
  void record_input_clause(const sat::Clause& clause);
  void bind(const BooleanClausifier& clausifier);
  void record(const sat::Clause& clause);
  sat::ClauseSubsetResult reduce(sat::Terminator* terminator) const;
  // Returns false on interruption; invalid or unavailable witnesses throw and
  // are classified at the single-query export boundary.
  bool reconstruct(std::optional<std::span<const std::size_t>> selected,
                   const std::function<bool()>& cancelled, util::Statistics& statistics);
  std::size_t collected_count() const;
  std::uint64_t input_format_microseconds() const { return input_format_microseconds_; }
  std::uint64_t conflict_format_microseconds() const { return conflict_format_microseconds_; }

 private:
  bool reconstruct_lia(std::optional<std::span<const std::size_t>> selected,
                       const std::function<bool()>& cancelled, util::Statistics& statistics);
  core::TermManager& terms_;
  smtlib::ConflictSpool& document_;
  sat::Cnf input_clauses_;
  sat::Cnf theory_clauses_;
  ExportCnfReduction reduction_;
  bool erase_sorts_;
  ExportCnfTheory theory_;
  std::unique_ptr<NumericClauseSpool> numeric_;
  bool bound_ = false;
  std::uint64_t input_format_microseconds_ = 0;
  std::uint64_t conflict_format_microseconds_ = 0;
  std::unordered_map<int, std::pair<core::TermId, int>> atoms_;
};

// Buffered, nonincremental entry point. Validates the entire script before
// solving; writes export_cnf only after an UNSAT result.
CheckSatOutcome export_cnf_once(std::string_view input, CheckSatOptions options);

}  // namespace primo::smt
