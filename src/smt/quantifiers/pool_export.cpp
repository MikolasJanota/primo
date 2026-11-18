#include "smt/quantifiers/pool_export.hpp"

#include <cstddef>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/binders.hpp"
#include "core/symbol_table.hpp"
#include "core/term.hpp"
#include "core/term_manager.hpp"
#include "smtlib/term_printer.hpp"
#include "theory/quantifiers/quantifier_component.hpp"
#include "util/statistics.hpp"

namespace primo::smt {

namespace {

// Decides which terms can be printed in the input's signature: no arithmetic,
// and no internal constant other than the ones the component created. Terms
// found printable are remembered across calls.
class Printability {
 public:
  Printability(const core::TermManager& terms, std::unordered_set<core::SymbolId> created)
      : terms_(terms), created_(std::move(created)) {}

  bool operator()(core::TermId root) {
    std::vector<core::TermId> pending{root};
    std::unordered_set<core::TermId> seen;
    while (!pending.empty()) {
      const core::TermId id = pending.back();
      pending.pop_back();
      if (printable_.contains(id) || !seen.insert(id).second) continue;
      const core::Term& node = terms_.get(id);
      switch (node.kind) {
        case core::TermKind::Rational:
        case core::TermKind::Plus:
        case core::TermKind::Times:
        case core::TermKind::Le:
        case core::TermKind::Lt:
        case core::TermKind::IntDiv:
        case core::TermKind::IntMod:
        case core::TermKind::Abs:
          return false;
        case core::TermKind::Pattern:
          // Not printed.
          continue;
        case core::TermKind::Constant:
        case core::TermKind::Application:
          if (terms_.symbols().get_symbol(node.symbol).internal &&
              !created_.contains(node.symbol)) {
            return false;
          }
          break;
        default:
          break;
      }
      pending.insert(pending.end(), node.children.begin(), node.children.end());
    }
    printable_.insert(seen.begin(), seen.end());
    return true;
  }

 private:
  const core::TermManager& terms_;
  std::unordered_set<core::SymbolId> created_;
  std::unordered_set<core::TermId> printable_;
};

std::string pool_name(const core::SymbolTable& symbols, std::size_t index) {
  std::string name = "primo_pool_" + std::to_string(index);
  while (symbols.lookup_symbol(name).has_value()) name += "_";
  return name;
}

}  // namespace

void write_pool_export(const core::TermManager& terms, std::span<const core::TermId> roots,
                       const theory::QuantifierExport& record, const std::string& path,
                       util::Statistics& statistics) {
  const core::SymbolTable& symbols = terms.symbols();
  std::unordered_set<core::SymbolId> created;
  for (const core::TermId constant : record.constants) created.insert(terms.get(constant).symbol);
  Printability printable(terms, created);

  std::ostringstream out;
  out << "; Primo quantifier pool export (scripts/quantifier_pool_check.py)\n";
  for (const core::TermId constant : record.constants) {
    const core::Term& node = terms.get(constant);
    out << "(declare-fun " << smtlib::symbol_text(symbols.get_symbol(node.symbol).name) << " () "
        << smtlib::print_sort(symbols, node.sort) << ")\n";
  }

  // Top-level assertions a Skolemization can witness: an Exists root, or a Not
  // over a Forall root, numbered by occurrence among the roots of the same
  // kind and variable sorts. An atom is named by its first occurrence.
  std::map<std::pair<core::TermId, bool>, std::size_t> root_occurrence;
  std::map<std::pair<bool, std::vector<core::SortId>>, std::size_t> roots_seen;
  for (const core::TermId root : roots) {
    core::TermId atom = root;
    bool value = true;
    const core::TermKind kind = terms.get(root).kind;
    if (kind == core::TermKind::Not &&
        terms.get(terms.get(root).children.front()).kind == core::TermKind::Forall) {
      atom = terms.get(root).children.front();
      value = false;
    } else if (kind != core::TermKind::Exists) {
      continue;
    }
    const std::size_t occurrence = roots_seen[{value, core::quantifier_sorts(terms, atom)}]++;
    root_occurrence.emplace(std::pair{atom, value}, occurrence);
  }

  std::size_t printed_lemmas = 0;
  std::size_t skipped_lemmas = 0;
  std::size_t skolem_roots = 0;
  for (const auto& skolemization : record.skolemizations) {
    const bool queued = skolemization.lemma != core::kInvalidTermId;
    const bool printed = queued && printable(skolemization.lemma);
    if (queued && !printed) ++skipped_lemmas;
    const auto found = root_occurrence.find({skolemization.atom, skolemization.value});
    if (found != root_occurrence.end()) {
      ++skolem_roots;
      out << "; primo-skolem-root " << (skolemization.value ? "exists" : "not-forall") << " "
          << found->second << " :sorts";
      for (const core::TermId constant : skolemization.constants) {
        out << " " << smtlib::print_sort(symbols, terms.get(constant).sort);
      }
      out << " :constants";
      for (const core::TermId constant : skolemization.constants) {
        out << " " << smtlib::symbol_text(symbols.get_symbol(terms.get(constant).symbol).name);
      }
      out << " :lemma " << (printed ? "next" : "none") << "\n";
    }
    if (printed) {
      ++printed_lemmas;
      out << "(assert " << smtlib::print_term(terms, skolemization.lemma) << ")\n";
    }
  }

  // Ordered by signature, and by first use within one, so the file is stable.
  struct Pool {
    std::vector<std::string> elements;
    std::unordered_set<std::string> seen;
  };
  std::map<std::vector<core::SortId>, Pool> pools;
  std::size_t exported_tuples = 0;
  std::size_t skipped_tuples = 0;
  for (const auto& [sorts, tuple] : record.tuples) {
    bool ok = true;
    for (const core::TermId term : tuple) ok = ok && printable(term);
    if (!ok) {
      ++skipped_tuples;
      continue;
    }
    std::string element;
    if (tuple.size() == 1) {
      element = smtlib::print_term(terms, tuple.front());
    } else {
      element = "(tuple";
      for (const core::TermId term : tuple) element += " " + smtlib::print_term(terms, term);
      element += ")";
    }
    Pool& pool = pools[sorts];
    if (pool.seen.insert(element).second) {
      pool.elements.push_back(std::move(element));
      ++exported_tuples;
    }
  }

  std::size_t index = 0;
  for (const auto& [sorts, pool] : pools) {
    const std::string name = pool_name(symbols, index++);
    std::string sort_list;
    for (const core::SortId sort : sorts) sort_list += " " + smtlib::print_sort(symbols, sort);
    out << "; primo-pool " << name << sort_list << "\n";
    out << "(declare-pool " << name << " ";
    if (sorts.size() == 1) {
      out << smtlib::print_sort(symbols, sorts.front());
    } else {
      out << "(Tuple" << sort_list << ")";
    }
    out << " (";
    for (std::size_t element = 0; element < pool.elements.size(); ++element) {
      out << (element == 0 ? "" : " ") << pool.elements[element];
    }
    out << "))\n";
  }

  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) throw std::runtime_error("cannot write the quantifier pool export to " + path);
  file << out.str();
  if (!file) throw std::runtime_error("cannot write the quantifier pool export to " + path);

  statistics.set_dynamic(":quant.export.constants", record.constants.size());
  statistics.set_dynamic(":quant.export.skolem-lemmas", printed_lemmas);
  statistics.set_dynamic(":quant.export.skolem-roots", skolem_roots);
  statistics.set_dynamic(":quant.export.skipped-lemmas", skipped_lemmas);
  statistics.set_dynamic(":quant.export.tuples", exported_tuples);
  statistics.set_dynamic(":quant.export.skipped-tuples", skipped_tuples);
  statistics.set_dynamic(":quant.export.pools", pools.size());
}

}  // namespace primo::smt
