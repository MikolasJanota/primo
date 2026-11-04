#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "sat/clause.hpp"

namespace primo::smt {

// Private query-local native-binary records, never a public certificate.
// `none` reads sequentially without an offset index or retained clause vectors.
class NumericClauseSpool {
 public:
  explicit NumericClauseSpool(bool indexed);
  ~NumericClauseSpool();
  NumericClauseSpool(const NumericClauseSpool&) = delete;
  NumericClauseSpool& operator=(const NumericClauseSpool&) = delete;
  void append(const sat::Clause& clause);
  sat::Clause read(std::size_t index);
  std::size_t size() const { return count_; }
  std::uint64_t bytes() const { return bytes_; }

 private:
  std::FILE* file_;
  bool indexed_;
  std::vector<std::uint64_t> offsets_;
  std::size_t count_ = 0;
  std::size_t next_ = 0;
  std::uint64_t bytes_ = 0;
};

}  // namespace primo::smt
