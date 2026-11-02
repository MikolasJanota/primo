#pragma once

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
struct ConflictDocument;
}

namespace primo::smt {
class BooleanClausifier;

// Borrows one query's term manager and output document. Neither may be destroyed
// before the solve returns. Copies clauses and literal text; retains no clausifier borrows.
class QfUfConflictCollector {
 public:
  QfUfConflictCollector(const core::TermManager& terms, smtlib::ConflictDocument& document,
                        ExportCnfReduction reduction = ExportCnfReduction::None);
  void record_input_clause(const sat::Clause& clause);
  void bind(const BooleanClausifier& clausifier);
  void record(const sat::Clause& clause);
  sat::ClauseSubsetResult reduce(sat::Terminator* terminator);

 private:
  const core::TermManager& terms_;
  smtlib::ConflictDocument& document_;
  sat::Cnf input_clauses_;
  sat::Cnf theory_clauses_;
  ExportCnfReduction reduction_;
  bool bound_ = false;
  std::unordered_map<int, std::pair<core::TermId, int>> atoms_;
};

// Buffered, nonincremental entry point. Validates the entire script before
// solving; writes export_cnf only after an UNSAT result.
CheckSatOutcome export_qf_uf_cnf(std::string_view input, CheckSatOptions options);

}  // namespace primo::smt
