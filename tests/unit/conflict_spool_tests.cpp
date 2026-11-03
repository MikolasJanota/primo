#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstddef>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "smtlib/conflict_document.hpp"
#include "smtlib/conflict_spool.hpp"

namespace {

std::string serialized(const primo::smtlib::ConflictDocument& document) {
  std::ostringstream output;
  primo::smtlib::print_conflict_document(output, document);
  return output.str();
}

void bytes_and_selection() {
  using primo::smtlib::ConflictDocument;
  const ConflictDocument source{{"(declare-sort U 0)", "(declare-fun a () U)"},
                                {{"true"}, {}, {std::string(20000, 'x')}},
                                {{"false"}, {std::string(20000, 'y'), "false"}, {"true", "false"}}};
  primo::smtlib::ConflictSpool spool;
  for (const auto& declaration : source.signature) spool.declaration(declaration);
  for (const auto& clause : source.clauses) {
    spool.clause([&](std::ostream& output) {
      for (const auto& literal : clause) output << ' ' << literal;
    });
  }
  for (const auto& conflict : source.conflicts) {
    spool.conflict([&](std::ostream& output) {
      for (const auto& literal : conflict) output << ' ' << literal;
    });
  }
  assert(spool.clause_count() == 3 && spool.conflict_count() == 3);
  assert(spool.clause_bytes() > 20000);
  spool.seal();
  std::ostringstream all;
  spool.write_to(all);
  assert(all.str() == serialized(source));
  for (const std::vector<std::size_t>& selected : {std::vector<std::size_t>{}, {0, 2}, {0, 1, 2}}) {
    ConflictDocument expected = source;
    expected.conflicts.clear();
    for (const auto index : selected) expected.conflicts.push_back(source.conflicts[index]);
    std::ostringstream output;
    spool.write_to(output, selected);
    assert(output.str() == serialized(expected));
  }
  for (const std::vector<std::size_t>& invalid : {std::vector<std::size_t>{3}, {1, 0}, {1, 1}}) {
    std::ostringstream output;
    bool threw = false;
    try {
      spool.write_to(output, invalid);
    } catch (const std::out_of_range&) {
      threw = true;
    }
    assert(threw && output.str().empty());
  }
}

void invalid_states() {
  primo::smtlib::ConflictSpool spool(false);
  std::ostringstream output;
  bool threw = false;
  try {
    spool.write_to(output);
  } catch (const std::logic_error&) {
    threw = true;
  }
  assert(threw && output.str().empty());
  primo::smtlib::ConflictSpool unindexed(false);
  unindexed.seal();
  threw = false;
  try {
    const std::vector<std::size_t> empty;
    unindexed.write_to(output, empty);
  } catch (const std::logic_error&) {
    threw = true;
  }
  assert(threw && output.str().empty());
  // A partial callback poisons the spool; it must never publish partial records.
  threw = false;
  try {
    spool.conflict([](std::ostream& stream) {
      stream << " false";
      throw std::runtime_error("callback failure");
    });
  } catch (const std::runtime_error&) {
    threw = true;
  }
  assert(threw);
  threw = false;
  try {
    spool.seal();
  } catch (const std::logic_error&) {
    threw = true;
  }
  assert(threw);
}

}  // namespace

int main() {
  bytes_and_selection();
  invalid_states();
}
