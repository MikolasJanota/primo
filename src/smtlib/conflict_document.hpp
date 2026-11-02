#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace primo::smtlib {

// Version-1 text representation. Entries must already be well-sorted SMT-LIB
// declarations/literals; this serializer neither parses nor validates them.
// Owns all text and has no dependency on solver state or numeric IDs.
struct ConflictDocument {
  std::vector<std::string> signature;
  std::vector<std::vector<std::string>> clauses;
  std::vector<std::vector<std::string>> conflicts;
};

void print_conflict_document(std::ostream& output, const ConflictDocument& document);

}  // namespace primo::smtlib
