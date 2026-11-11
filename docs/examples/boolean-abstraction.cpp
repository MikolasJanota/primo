// Executable companion to implementation/sat.md. This uses Primo's internal
// interfaces to isolate clausification from frontend and preprocessing passes.
// Assertions execute solver calls, so keep them active in Release examples too.
#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <cstddef>
#include <iostream>

#include "core/symbol_table.hpp"
#include "core/term_manager.hpp"
#include "sat/cadical_backend.hpp"
#include "sat/ipasir_up.hpp"
#include "sat/lit.hpp"
#include "smt/clausifier.hpp"

namespace {

void check_example(bool guarded) {
  primo::core::SymbolTable symbols;
  const auto p_symbol = symbols.declare_constant("p", symbols.bool_sort());
  const auto q_symbol = symbols.declare_constant("q", symbols.bool_sort());
  const auto r_symbol = symbols.declare_constant("r", symbols.bool_sort());
  primo::core::TermManager terms(symbols);
  const auto p = terms.mk_constant(p_symbol);
  const auto q = terms.mk_constant(q_symbol);
  const auto r = terms.mk_constant(r_symbol);
  const auto not_r = terms.mk_not(r);
  const auto conjunction = terms.mk_and({q, not_r});
  const auto formula = terms.mk_or({p, conjunction});

  // Keep primary variables available for repeated assumption queries.
  primo::sat::CadicalBackend backend({}, true);
  primo::smt::BooleanClausifier clausifier(backend, terms, false);
  // Copy literals: registering the next atom can invalidate an atom reference.
  const auto p_lit = clausifier.ensure_theory_atom(p).lit;
  const auto q_lit = clausifier.ensure_theory_atom(q).lit;
  const auto r_lit = clausifier.ensure_theory_atom(r).lit;
  const std::array literals{p_lit, q_lit, r_lit};
  for (const auto lit : literals) backend.protect_variable(lit.value);

  primo::sat::Lit selector;
  if (guarded) {
    selector = {backend.new_var()};
    backend.protect_variable(selector.value);
    clausifier.assert_guarded_formula(formula, selector);
  } else {
    clausifier.assert_formula(formula);
    const auto statistics = backend.statistics();
    assert(statistics.variables == 4);
    assert(statistics.clauses == 4);
    assert(statistics.binary_clauses == 3);
    assert(statistics.clause_literals == 9);
  }

  for (const bool enabled : std::array{true, false}) {
    if (!guarded && !enabled) continue;
    for (unsigned assignment = 0; assignment < 8; ++assignment) {
      const std::array values{(assignment & 1U) != 0, (assignment & 2U) != 0,
                              (assignment & 4U) != 0};
      for (std::size_t i = 0; i < literals.size(); ++i) {
        backend.assume({values[i] ? literals[i].value : -literals[i].value});
      }
      if (guarded) backend.assume({enabled ? selector.value : -selector.value});
      const bool expected = !enabled || values[0] || (values[1] && !values[2]);
      assert(backend.solve() ==
             (expected ? primo::sat::SatResult::Sat : primo::sat::SatResult::Unsat));
    }
    std::cout << (guarded ? "guarded" : "unguarded") << (enabled ? " active" : " inactive")
              << ": 8 assignments checked\n";
  }
  if (guarded) {
    backend.add_clause({primo::sat::Lit{-selector.value}});
    backend.assume(selector);
    assert(backend.solve() == primo::sat::SatResult::Unsat);
    std::cout << "retired selector: reactivation is UNSAT\n";
  }
}

}  // namespace

int main() {
  if (primo::sat::cadical_signature() == "unavailable") {
    std::cerr << "This example requires a build with CaDiCaL.\n";
    return 1;
  }
  check_example(false);
  check_example(true);
}
