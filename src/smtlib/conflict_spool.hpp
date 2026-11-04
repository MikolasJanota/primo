#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <memory>
#include <span>
#include <string_view>

namespace primo::smtlib {

enum class ConflictFormat { QfUfV1, QfLraV2 };

// Version-1/2 section spools, independent of solver objects and IDs. Callbacks
// borrow a stream only for the call; they must not retain it or reenter this
// object. Temporary files are anonymous and closed on destruction. See
// docs/leads/cnf-export-spooling.md for states and failure guarantees.
class ConflictSpool {
 public:
  explicit ConflictSpool(bool index_conflicts = true,
                         ConflictFormat format = ConflictFormat::QfUfV1);
  ~ConflictSpool();
  ConflictSpool(const ConflictSpool&) = delete;
  ConflictSpool& operator=(const ConflictSpool&) = delete;

  void declaration(std::string_view text);
  void clause(const std::function<void(std::ostream&)>& literals);
  void conflict(const std::function<void(std::ostream&)>& facts);
  void seal();
  void write_to(std::ostream& output);
  void write_to(std::ostream& output, std::span<const std::size_t> selected);

  std::size_t clause_count() const;
  std::size_t conflict_count() const;
  std::uint64_t signature_bytes() const;
  std::uint64_t clause_bytes() const;
  std::uint64_t conflict_bytes() const;

 private:
  class Implementation;
  std::unique_ptr<Implementation> implementation_;
};

}  // namespace primo::smtlib
