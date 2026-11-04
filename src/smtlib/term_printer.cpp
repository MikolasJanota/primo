#include "smtlib/term_printer.hpp"

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/binders.hpp"
#include "core/ids.hpp"
#include "core/symbol_table.hpp"
#include "core/term.hpp"
#include "core/term_manager.hpp"
#include "smtlib/lexer.hpp"
#include "util/rational.hpp"

namespace primo::smtlib {

namespace {

// One step of the iterative printer.
struct Step {
  enum class Kind { Term, Text, PopNames, CacheEnd };
  Kind kind = Kind::Text;
  core::TermId term = core::kInvalidTermId;
  std::string text;
  // PopNames: names to drop. CacheEnd: output offset where the term started.
  std::size_t count = 0;
};

const char* operator_name(core::TermKind kind) {
  switch (kind) {
    case core::TermKind::Equality:
      return "=";
    case core::TermKind::Not:
      return "not";
    case core::TermKind::And:
      return "and";
    case core::TermKind::Or:
      return "or";
    case core::TermKind::Implies:
      return "=>";
    case core::TermKind::Ite:
      return "ite";
    default:
      return nullptr;
  }
}

// Arithmetic payloads are literal rationals, not child terms. Emit legal
// SMT-LIB numerals, including unary minus rather than signed numeral tokens.
void print_rational(std::ostream& output, const util::Rational& value) {
  auto text = value.to_string();
  const bool negative = value.sign() < 0;
  if (negative) {
    output << "(- ";
    text.erase(0, 1);
  }
  const auto slash = text.find('/');
  if (slash == std::string::npos)
    output << text;
  else
    output << "(/ " << text.substr(0, slash) << ' ' << text.substr(slash + 1) << ')';
  if (negative) output << ')';
}

}  // namespace

std::string symbol_text(const std::string& name) {
  if (is_simple_symbol(name) && name != "true" && name != "false") return name;
  if (name.find('|') != std::string::npos) {
    throw std::invalid_argument("symbol cannot be represented in SMT-LIB: " + name);
  }
  return "|" + name + "|";
}

std::string print_sort(const core::SymbolTable& symbols, core::SortId sort) {
  return symbol_text(symbols.get_sort(sort).name);
}

std::string print_term(const core::TermManager& terms, core::TermId root) {
  std::string out;
  // Names of the enclosing bound variables, innermost last.
  std::vector<std::string> names;
  // Text of closed subterms already printed.
  std::unordered_map<core::TermId, std::string> closed;
  std::size_t generated = 0;
  const auto fresh_name = [&]() {
    while (true) {
      std::string name = "?x" + std::to_string(generated++);
      if (!terms.symbols().lookup_symbol(name).has_value()) return name;
    }
  };

  std::vector<Step> steps{{Step::Kind::Term, root, {}, 0}};
  while (!steps.empty()) {
    Step step = std::move(steps.back());
    steps.pop_back();
    switch (step.kind) {
      case Step::Kind::Text:
        out += step.text;
        continue;
      case Step::Kind::PopNames:
        names.resize(names.size() - step.count);
        continue;
      case Step::Kind::CacheEnd:
        closed.emplace(step.term, out.substr(step.count));
        continue;
      case Step::Kind::Term:
        break;
    }
    const core::TermId id = step.term;
    const bool is_closed = terms.loose_index(id) == 0;
    if (is_closed) {
      if (const auto found = closed.find(id); found != closed.end()) {
        out += found->second;
        continue;
      }
    }
    const core::Term& node = terms.get(id);
    switch (node.kind) {
      case core::TermKind::BoolValue:
        out += node.bool_value ? "true" : "false";
        continue;
      case core::TermKind::Constant:
        out += symbol_text(terms.symbols().get_symbol(node.symbol).name);
        continue;
      case core::TermKind::BoundVariable: {
        const std::size_t index = node.symbol;
        if (index >= names.size()) throw std::invalid_argument("cannot print an open term");
        out += names[names.size() - 1 - index];
        continue;
      }
      case core::TermKind::And:
      case core::TermKind::Or:
        if (node.children.empty()) {
          out += node.kind == core::TermKind::And ? "true" : "false";
          continue;
        }
        break;
      default:
        break;
    }

    if (is_closed) steps.push_back({Step::Kind::CacheEnd, id, {}, out.size()});
    steps.push_back({Step::Kind::Text, core::kInvalidTermId, ")", 0});
    if (node.kind == core::TermKind::Forall || node.kind == core::TermKind::Exists) {
      const std::uint32_t arity = core::quantifier_arity(terms, id);
      const std::vector<core::SortId> sorts = core::quantifier_sorts(terms, id);
      const core::TermId body = core::quantifier_body(terms, id);
      out += node.kind == core::TermKind::Forall ? "(forall (" : "(exists (";
      for (std::size_t position = 0; position < sorts.size(); ++position) {
        std::string name = fresh_name();
        out += (position == 0 ? "(" : " (") + name + " " +
               print_sort(terms.symbols(), sorts[position]) + ")";
        names.push_back(std::move(name));
      }
      out += ") ";
      steps.push_back({Step::Kind::PopNames, core::kInvalidTermId, {}, arity});
      steps.push_back({Step::Kind::Term, body, {}, 0});
      continue;
    }
    if (node.kind == core::TermKind::Application) {
      out += "(" + symbol_text(terms.symbols().get_symbol(node.symbol).name);
    } else if (const char* name = operator_name(node.kind)) {
      out += std::string("(") + name;
    } else {
      throw std::invalid_argument("term outside the quantified UF fragment");
    }
    for (auto child = node.children.rbegin(); child != node.children.rend(); ++child) {
      steps.push_back({Step::Kind::Term, *child, {}, 0});
      steps.push_back({Step::Kind::Text, core::kInvalidTermId, " ", 0});
    }
  }
  return out;
}

void print_ground_term(std::ostream& output, const core::TermManager& terms, core::TermId root) {
  // Expand the textual tree using IDs and punctuation only; unlike print_term,
  // do not retain a string cache proportional to expanded subterm output.
  std::vector<Step> steps{{Step::Kind::Term, root, {}, 0}};
  while (!steps.empty()) {
    Step step = std::move(steps.back());
    steps.pop_back();
    if (step.kind == Step::Kind::Text) {
      output << step.text;
      continue;
    }
    const auto& node = terms.get(step.term);
    if (node.kind == core::TermKind::BoolValue) {
      output << (node.bool_value ? "true" : "false");
      continue;
    }
    if (node.kind == core::TermKind::Constant) {
      output << symbol_text(terms.symbols().get_symbol(node.symbol).name);
      continue;
    }
    if (node.kind == core::TermKind::Rational) {
      print_rational(output, node.rational_value);
      continue;
    }
    if ((node.kind == core::TermKind::And || node.kind == core::TermKind::Or) &&
        node.children.empty()) {
      output << (node.kind == core::TermKind::And ? "true" : "false");
      continue;
    }
    if (node.kind == core::TermKind::Application) {
      output << '(' << symbol_text(terms.symbols().get_symbol(node.symbol).name);
    } else if (node.kind == core::TermKind::Times) {
      output << "(* ";
      print_rational(output, node.rational_value);
    } else if (node.kind == core::TermKind::Plus || node.kind == core::TermKind::Le ||
               node.kind == core::TermKind::Lt) {
      output << '('
             << (node.kind == core::TermKind::Plus ? "+"
                 : node.kind == core::TermKind::Le ? "<="
                                                   : "<");
    } else if (const char* name = operator_name(node.kind)) {
      output << '(' << name;
    } else {
      throw std::invalid_argument("term outside the ground UF/linear arithmetic fragment");
    }
    steps.push_back({Step::Kind::Text, core::kInvalidTermId, ")", 0});
    for (auto child = node.children.rbegin(); child != node.children.rend(); ++child) {
      steps.push_back({Step::Kind::Term, *child, {}, 0});
      steps.push_back({Step::Kind::Text, core::kInvalidTermId, " ", 0});
    }
  }
}

}  // namespace primo::smtlib
