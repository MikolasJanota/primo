#pragma once

#include <string_view>
#include <unordered_map>
#include <utility>

#include "core/ids.hpp"
#include "sat/clause.hpp"
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
// before the solve returns. Copies literal text; retains no clausifier borrows.
class QfUfConflictCollector {
 public:
  QfUfConflictCollector(const core::TermManager& terms, smtlib::ConflictDocument& document);
  void prepare(BooleanClausifier& clausifier);
  void bind(const BooleanClausifier& clausifier);
  void record(const sat::Clause& clause);

 private:
  const core::TermManager& terms_;
  smtlib::ConflictDocument& document_;
  std::unordered_map<int, std::pair<core::TermId, int>> atoms_;
};

// Buffered, nonincremental entry point. Validates the entire script before
// solving; writes export_cnf only after an UNSAT result.
CheckSatOutcome export_qf_uf_cnf(std::string_view input, CheckSatOptions options);

}  // namespace primo::smt
