#pragma once

#include <cstdint>
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

// Borrows the query term manager and text spool. Copies SAT clauses only for
// reduction; writes text synchronously and retains no clausifier/stream borrows.
class QfUfConflictCollector {
 public:
  QfUfConflictCollector(core::TermManager& terms, smtlib::ConflictSpool& document,
                        ExportCnfReduction reduction = ExportCnfReduction::None,
                        bool erase_sorts = false);
  void record_preprocessing_unsat();
  void record_input_clause(const sat::Clause& clause);
  void bind(const BooleanClausifier& clausifier);
  void record(const sat::Clause& clause);
  sat::ClauseSubsetResult reduce(sat::Terminator* terminator) const;
  std::uint64_t input_format_microseconds() const { return input_format_microseconds_; }
  std::uint64_t conflict_format_microseconds() const { return conflict_format_microseconds_; }

 private:
  core::TermManager& terms_;
  smtlib::ConflictSpool& document_;
  sat::Cnf input_clauses_;
  sat::Cnf theory_clauses_;
  ExportCnfReduction reduction_;
  bool erase_sorts_;
  bool bound_ = false;
  std::uint64_t input_format_microseconds_ = 0;
  std::uint64_t conflict_format_microseconds_ = 0;
  std::unordered_map<int, std::pair<core::TermId, int>> atoms_;
};

// Buffered, nonincremental entry point. Validates the entire script before
// solving; writes export_cnf only after an UNSAT result.
CheckSatOutcome export_qf_uf_cnf(std::string_view input, CheckSatOptions options);

}  // namespace primo::smt
