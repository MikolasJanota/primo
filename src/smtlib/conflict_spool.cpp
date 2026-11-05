#include "smtlib/conflict_spool.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <ios>
#include <limits>
#include <memory>
#include <ostream>
#include <span>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace primo::smtlib {
namespace {

[[noreturn]] void io_error(const char* operation) {
  throw std::system_error(errno == 0 ? EIO : errno, std::generic_category(), operation);
}

struct CloseFile {
  void operator()(std::FILE* file) const { static_cast<void>(std::fclose(file)); }
};

std::unique_ptr<std::FILE, CloseFile> temporary_file() {
  auto name = (std::filesystem::temp_directory_path() / "primo-cnf-XXXXXX").string();
  const int descriptor = ::mkstemp(name.data());
  if (descriptor < 0) io_error("create CNF spool");
  if (::unlink(name.c_str()) != 0) {
    const int error = errno;
    static_cast<void>(::close(descriptor));
    errno = error;
    io_error("unlink CNF spool");
  }
  auto* file = ::fdopen(descriptor, "w+b");
  if (file == nullptr) {
    const int error = errno;
    static_cast<void>(::close(descriptor));
    errno = error;
    io_error("open CNF spool stream");
  }
  return std::unique_ptr<std::FILE, CloseFile>(file);
}

class FileBuffer final : public std::streambuf {
 public:
  explicit FileBuffer(std::FILE* file) : file_(file) {}
  std::uint64_t bytes() const { return bytes_; }

 protected:
  std::streamsize xsputn(const char* data, std::streamsize length) override {
    if (length < 0) return 0;
    const auto size = static_cast<std::size_t>(length);
    if (size > std::numeric_limits<std::uint64_t>::max() - bytes_) return 0;
    const auto written = std::fwrite(data, 1, size, file_);
    bytes_ += written;
    return static_cast<std::streamsize>(written);
  }
  int_type overflow(int_type value) override {
    if (traits_type::eq_int_type(value, traits_type::eof())) return traits_type::not_eof(value);
    if (bytes_ == std::numeric_limits<std::uint64_t>::max() ||
        std::fputc(traits_type::to_char_type(value), file_) == EOF) {
      return traits_type::eof();
    }
    ++bytes_;
    return value;
  }
  int sync() override { return std::fflush(file_); }

 private:
  std::FILE* file_;
  std::uint64_t bytes_ = 0;
};

class Section {
 public:
  Section() : file_(temporary_file()), buffer_(file_.get()), output_(&buffer_) {
    output_.exceptions(std::ios::badbit | std::ios::failbit);
  }
  std::ostream& output() { return output_; }
  std::uint64_t bytes() const { return buffer_.bytes(); }
  void flush() { output_.flush(); }
  void copy(std::ostream& output, std::uint64_t offset, std::uint64_t length) {
    if (offset > bytes() || length > bytes() - offset ||
        offset > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
      throw std::out_of_range("invalid CNF spool range");
    }
    if (::fseeko(file_.get(), static_cast<off_t>(offset), SEEK_SET) != 0)
      io_error("seek CNF spool");
    std::array<char, 8192> buffer{};
    while (length != 0) {
      const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(length, buffer.size()));
      if (std::fread(buffer.data(), 1, count, file_.get()) != count) io_error("read CNF spool");
      output.write(buffer.data(), static_cast<std::streamsize>(count));
      if (!output) throw std::runtime_error("write CNF artifact");
      length -= count;
    }
  }

 private:
  std::unique_ptr<std::FILE, CloseFile> file_;
  FileBuffer buffer_;
  std::ostream output_;
};

}  // namespace

class ConflictSpool::Implementation {
 public:
  Implementation(bool index, ConflictFormat selected_format)
      : indexed(index), format(selected_format) {}
  struct Record {
    std::uint64_t offset;
    std::uint64_t length;
  };
  Section signature;
  Section clauses;
  Section conflicts;
  std::vector<Record> records;
  std::size_t clause_count = 0;
  std::size_t conflict_count = 0;
  bool indexed;
  ConflictFormat format;
  bool sealed = false;
  bool busy = false;
  bool failed = false;

  void require_writable() const {
    if (sealed || busy || failed) throw std::logic_error("invalid CNF spool append state");
  }
  template <typename Operation>
  void append(Operation operation) {
    require_writable();
    busy = true;
    try {
      operation();
      busy = false;
    } catch (...) {
      failed = true;
      busy = false;
      throw;
    }
  }
  void write(std::ostream& output, const std::span<const std::size_t>* selected) {
    if (!sealed) throw std::logic_error("CNF spool must be sealed before publication");
    if (selected != nullptr) {
      if (!indexed) throw std::logic_error("CNF conflict spool has no selection index");
      std::size_t previous = 0;
      bool first = true;
      for (const auto index : *selected) {
        if (index >= records.size() || (!first && index <= previous)) {
          throw std::out_of_range("CNF conflict selection must be in original order");
        }
        previous = index;
        first = false;
      }
    }
    output << (format == ConflictFormat::QfUfV1
                   ? "(primo-conflicts\n  (version 1)\n  (logic QF_UF)\n  (signature"
               : format == ConflictFormat::QfLraV2
                   ? "(primo-conflicts\n  (version 2)\n  (logic QF_LRA)\n  (signature"
                   : "(primo-conflicts\n  (version 3)\n  (logic QF_LIA)\n  (signature");
    signature.copy(output, 0, signature.bytes());
    output << ")\n  (cnf";
    clauses.copy(output, 0, clauses.bytes());
    output << ")\n  (conflicts";
    if (selected == nullptr) {
      conflicts.copy(output, 0, conflicts.bytes());
    } else {
      for (const auto index : *selected) {
        const auto& record = records[index];
        conflicts.copy(output, record.offset, record.length);
      }
    }
    output << "))\n";
    if (!output) throw std::runtime_error("write CNF artifact");
  }
};

ConflictSpool::ConflictSpool(bool index_conflicts, ConflictFormat format)
    : implementation_(std::make_unique<Implementation>(index_conflicts, format)) {}
ConflictSpool::~ConflictSpool() = default;

void ConflictSpool::declaration(std::string_view text) {
  auto& state = *implementation_;
  state.append([&] { state.signature.output() << "\n    " << text; });
}
void ConflictSpool::clause(const std::function<void(std::ostream&)>& literals) {
  auto& state = *implementation_;
  state.append([&] {
    auto& output = state.clauses.output();
    output << "\n    (clause";
    literals(output);
    output << ')';
    ++state.clause_count;
  });
}
void ConflictSpool::conflict(const std::function<void(std::ostream&)>& facts) {
  auto& state = *implementation_;
  state.append([&] {
    const auto start = state.conflicts.bytes();
    auto& output = state.conflicts.output();
    output << (state.format == ConflictFormat::QfUfV1    ? "\n    (conflict"
               : state.format == ConflictFormat::QfLraV2 ? "\n    (conflict-lra"
                                                         : "\n    (conflict-lia");
    facts(output);
    output << ')';
    if (state.indexed) state.records.push_back({start, state.conflicts.bytes() - start});
    ++state.conflict_count;
  });
}
void ConflictSpool::seal() {
  auto& state = *implementation_;
  state.require_writable();
  state.append([&] {
    state.signature.flush();
    state.clauses.flush();
    state.conflicts.flush();
  });
  state.sealed = true;
}
void ConflictSpool::write_to(std::ostream& output) { implementation_->write(output, nullptr); }
void ConflictSpool::write_to(std::ostream& output, std::span<const std::size_t> selected) {
  implementation_->write(output, &selected);
}
std::size_t ConflictSpool::clause_count() const { return implementation_->clause_count; }
std::size_t ConflictSpool::conflict_count() const { return implementation_->conflict_count; }
std::uint64_t ConflictSpool::signature_bytes() const { return implementation_->signature.bytes(); }
std::uint64_t ConflictSpool::clause_bytes() const { return implementation_->clauses.bytes(); }
std::uint64_t ConflictSpool::conflict_bytes() const { return implementation_->conflicts.bytes(); }

}  // namespace primo::smtlib
