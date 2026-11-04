#include "smt/numeric_clause_spool.hpp"

#include <sys/types.h>

#include <cerrno>
#include <limits>
#include <stdexcept>
#include <system_error>

namespace primo::smt {
namespace {
[[noreturn]] void spool_error() {
  throw std::system_error(errno == 0 ? EIO : errno, std::generic_category(), "numeric lemma spool");
}
}  // namespace

NumericClauseSpool::NumericClauseSpool(bool indexed) : file_(std::tmpfile()), indexed_(indexed) {
  if (file_ == nullptr) spool_error();
}
NumericClauseSpool::~NumericClauseSpool() { static_cast<void>(std::fclose(file_)); }

void NumericClauseSpool::append(const sat::Clause& clause) {
  if (next_ != 0) throw std::logic_error("cannot append after reading numeric lemmas");
  const auto length = static_cast<std::uint64_t>(clause.size());
  if (bytes_ > std::numeric_limits<std::uint64_t>::max() - sizeof(length) ||
      length > (std::numeric_limits<std::uint64_t>::max() - bytes_ - sizeof(length)) /
                   sizeof(std::int32_t))
    throw std::length_error("numeric lemma spool overflow");
  if (indexed_) offsets_.push_back(bytes_);
  if (std::fwrite(&length, sizeof(length), 1, file_) != 1) spool_error();
  for (const auto literal : clause) {
    const auto value = static_cast<std::int32_t>(literal.value);
    if (std::fwrite(&value, sizeof(value), 1, file_) != 1) spool_error();
  }
  bytes_ += sizeof(length) + length * sizeof(std::int32_t);
  ++count_;
}

sat::Clause NumericClauseSpool::read(std::size_t index) {
  if (index >= count_) throw std::out_of_range("numeric lemma index");
  if (indexed_) {
    const auto offset = offsets_.at(index);
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max()))
      throw std::length_error("numeric lemma offset overflow");
    if (::fseeko(file_, static_cast<off_t>(offset), SEEK_SET) != 0) spool_error();
  } else if (next_ == 0) {
    if (index != 0) throw std::logic_error("numeric lemmas require sequential reading");
    if (::fseeko(file_, 0, SEEK_SET) != 0) spool_error();
  } else if (index != next_) {
    throw std::logic_error("numeric lemmas require sequential reading");
  }
  std::uint64_t length = 0;
  if (std::fread(&length, sizeof(length), 1, file_) != 1) spool_error();
  if (length > bytes_ / sizeof(std::int32_t))
    throw std::runtime_error("invalid numeric lemma size");
  sat::Clause clause;
  for (std::uint64_t i = 0; i < length; ++i) {
    std::int32_t value = 0;
    if (std::fread(&value, sizeof(value), 1, file_) != 1) spool_error();
    clause.push_back(sat::Lit{value});
  }
  next_ = index + 1;
  return clause;
}

}  // namespace primo::smt
