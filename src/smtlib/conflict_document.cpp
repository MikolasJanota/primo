#include "smtlib/conflict_document.hpp"

#include <ostream>

namespace primo::smtlib {

void print_conflict_document(std::ostream& output, const ConflictDocument& document) {
  output << "(primo-conflicts\n  (version 1)\n  (logic QF_UF)\n  (signature";
  for (const auto& declaration : document.signature) output << "\n    " << declaration;
  output << ")\n  (cnf";
  for (const auto& clause : document.clauses) {
    output << "\n    (clause";
    for (const auto& literal : clause) output << ' ' << literal;
    output << ')';
  }
  output << ")\n  (conflicts";
  for (const auto& conflict : document.conflicts) {
    output << "\n    (conflict";
    for (const auto& literal : conflict) output << ' ' << literal;
    output << ')';
  }
  output << "))\n";
}

}  // namespace primo::smtlib
