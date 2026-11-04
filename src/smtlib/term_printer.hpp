#pragma once

#include <iosfwd>
#include <string>

#include "core/ids.hpp"

namespace primo::core {
class SymbolTable;
class TermManager;
}  // namespace primo::core

namespace primo::smtlib {

// SMT-LIB text for a symbol or sort name: the name itself when it is a simple
// symbol, otherwise quoted. Throws std::invalid_argument for a name containing
// '|', which SMT-LIB cannot represent.
std::string symbol_text(const std::string& name);

std::string print_sort(const core::SymbolTable& symbols, core::SortId sort);

// SMT-LIB text of a closed term of the quantified UF fragment: Boolean
// connectives, equality, ite, uninterpreted constants and applications, and
// quantifiers. Bound variables get generated names that no declared symbol
// uses. Patterns are omitted: they do not change what a formula means. Throws
// std::invalid_argument for arithmetic terms and for open terms.
std::string print_term(const core::TermManager& terms, core::TermId term);

// Stream a ground UF or linear arithmetic term without caching expanded subterm
// text. The caller must not mutate terms through stream callbacks. Quantifiers,
// open terms and integer-specific operators are rejected. Arithmetic supports
// rational constants, sums, literal scaling and Le/Lt comparisons.
void print_ground_term(std::ostream& output, const core::TermManager& terms, core::TermId term);

}  // namespace primo::smtlib
