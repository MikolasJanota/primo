#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/ids.hpp"

namespace primo::core {
class TermManager;
}

namespace primo::smtlib {
struct Command;
struct SExpr;
}  // namespace primo::smtlib

namespace primo::smt {

class TermBuilder;

// Owns one SMT-LIB-to-core translation session. No syntax node escapes this
// boundary: clients receive only TermIds owned by terms().
class Frontend {
 public:
  explicit Frontend(bool validate_parameterized_definitions = false);
  ~Frontend();

  Frontend(const Frontend&) = delete;
  Frontend& operator=(const Frontend&) = delete;

  void process_command(const smtlib::Command& command, std::vector<core::TermId>& assertions);
  const std::optional<std::string>& logic() const;
  bool saw_check_sat() const;
  bool requested_model() const;
  bool produce_models() const;
  bool has_declared_real_constants() const;
  bool has_declared_int_constants() const;
  std::vector<core::TermId> declared_real_constant_terms();
  std::vector<core::TermId> declared_int_constant_terms();
  const core::TermManager& terms() const;
  core::TermManager& terms();
  std::size_t nullary_definition_direct_uses() const;
  std::size_t nullary_definition_eager_compiles() const;
  std::size_t nullary_definition_side_assertions() const;
  core::TermId fresh_internal_constant(core::SortId sort, std::string_view prefix);
  void push_scope();
  void pop_scope();

 private:
  friend class TermBuilder;
  struct Implementation;
  std::unique_ptr<Implementation> implementation_;
};

}  // namespace primo::smt
