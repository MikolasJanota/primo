#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstddef>
#include <string>
#include <variant>
#include <vector>

#include "core/ids.hpp"
#include "core/model.hpp"
#include "core/symbol_table.hpp"
#include "core/term_manager.hpp"
#include "smt/check_sat.hpp"
#include "smt/component_model_assembler.hpp"
#include "smt/model_reconstruction.hpp"
#include "smt/typed_model_builder.hpp"
#include "smtlib/printer.hpp"
#include "theory/theory_component.hpp"
#include "theory/theory_solver.hpp"
#include "util/rational.hpp"

using namespace primo;

namespace {
template <typename Fn>
void rejects(Fn fn) {
  bool rejected = false;
  try {
    fn();
  } catch (const core::ModelError&) {
    rejected = true;
  }
  assert(rejected);
}

void simplified_finite_domain_arguments_reconstruct_the_original_model() {
  for (const bool integer : {false, true}) {
    for (const bool post_amo : {false, true}) {
      for (const int value : {0, 1}) {
        const std::string logic = integer ? "QF_UFLIA" : "QF_UFLRA";
        const std::string sort = integer ? "Int" : "Real";
        const std::string input = "(set-option :produce-models true)(set-logic " + logic +
                                  ")(declare-const a " + sort + ")(declare-fun f (" + sort + ") " +
                                  sort + ")(assert (= a 7))(assert (or (= (f a) 0) (= (f a) 1)))" +
                                  "(assert (= (f a) " + std::to_string(value) +
                                  "))(check-sat)(get-model)";
        smt::CheckSatOptions options;
        options.preprocessing_mode = smt::PreprocessingMode::Strong;
        options.finite_domain_post_amo = post_amo;
        options.check_model = true;
        const auto result = smt::check_sat_smtlib(input, options);
        assert(result.result == smt::CheckSatResult::Sat);
        assert(result.typed_model);
        assert(!result.model.empty());
      }
    }
  }
}

void driver_boolean_facts_do_not_bypass_fragment_validation() {
  core::SymbolTable symbols;
  core::TermManager terms(symbols);
  const auto p = symbols.declare_constant("driver_p", symbols.bool_sort());
  const auto pt = terms.mk_constant(p);
  const auto n = symbols.declare_constant("driver_n", symbols.int_sort());
  const auto nt = terms.mk_constant(n);
  std::vector<theory::ComponentModelFact> facts{{pt, true}};
  const std::vector assertions{pt};
  const auto model =
      smt::assemble_component_model(terms, assertions, {}, {}, nullptr, nullptr, facts);
  assert(std::get<bool>(model->apply(p).payload));
  std::vector<theory::ComponentModelFragment> fragments{{{"owner", 1, 1}, {}, {{pt, false}}, {}}};
  rejects(
      [&] { smt::assemble_component_model(terms, {}, fragments, {}, nullptr, nullptr, facts); });
  fragments[0].facts.clear();
  fragments[0].token.component.clear();
  rejects(
      [&] { smt::assemble_component_model(terms, {}, fragments, {}, nullptr, nullptr, facts); });
  facts[0].atom = nt;
  rejects([&] { smt::assemble_component_model(terms, {}, {}, {}, nullptr, nullptr, facts); });
  facts[0].atom = terms.mk_not(pt);
  rejects([&] { smt::assemble_component_model(terms, {}, {}, {}, nullptr, nullptr, facts); });
}

void component_fragments_are_owned_typed_and_checked() {
  core::SymbolTable symbols;
  core::TermManager terms(symbols);
  const auto sort = symbols.declare_sort("FragmentDomain");
  const auto x = symbols.declare_constant("fragment_x", sort);
  const auto y = symbols.declare_constant("fragment_y", sort);
  const auto z = symbols.declare_constant("fragment_z", sort);
  const auto xt = terms.mk_constant(x), yt = terms.mk_constant(y), zt = terms.mk_constant(z);
  const auto domain = [&](std::size_t index) {
    return core::ModelValue{sort, core::DomainValue{index}};
  };
  std::vector<theory::ComponentModelFragment> fragments{
      {{"A", 7, 1}, {{xt, domain(0)}, {yt, domain(1)}}, {}, {}},
      {{"B", 7, 3}, {{xt, domain(42)}, {zt, domain(0)}}, {}, {}}};
  const std::vector assertions{terms.mk_not(terms.mk_eq(xt, yt)),
                               terms.mk_not(terms.mk_eq(xt, zt))};
  theory::ComponentDescriptor descriptor;
  descriptor.id = "A";
  descriptor.shared_sorts = {{sort, theory::StableInfiniteness::Yes, false}};
  const std::vector<theory::ComponentInterfaceScope> scopes{{sort, {xt, yt}}};
  theory::TheoryModelProjection projection{fragments[0].token, {{sort, {{{xt}}, {{yt}}}}}};
  smt::validate_component_fragment(terms, descriptor, scopes, fragments[0].token, &projection, {},
                                   fragments[0]);
  auto corrupted = fragments[0];
  corrupted.bindings.pop_back();
  rejects([&] {
    smt::validate_component_fragment(terms, descriptor, scopes, fragments[0].token, &projection, {},
                                     corrupted);
  });
  corrupted = fragments[0];
  corrupted.bindings[1].value = corrupted.bindings[0].value;
  rejects([&] {
    smt::validate_component_fragment(terms, descriptor, scopes, fragments[0].token, &projection, {},
                                     corrupted);
  });
  corrupted = fragments[0];
  ++corrupted.token.revision;
  rejects([&] {
    smt::validate_component_fragment(terms, descriptor, scopes, fragments[0].token, &projection, {},
                                     corrupted);
  });
  const auto model =
      smt::assemble_component_model(terms, assertions, fragments, {}, nullptr, nullptr);
  assert(model->apply(x) != model->apply(y));
  assert(model->apply(x) != model->apply(z));
  assert(model->apply(y) != model->apply(z));  // Local index 0 is not globally shared.
  fragments[1].bindings.push_back({yt, domain(42)});
  rejects(
      [&] { smt::assemble_component_model(terms, assertions, fragments, {}, nullptr, nullptr); });
  fragments[1].bindings.pop_back();
  fragments[1].token.assignment_epoch = 8;
  rejects(
      [&] { smt::assemble_component_model(terms, assertions, fragments, {}, nullptr, nullptr); });

  const auto i = symbols.int_sort();
  const auto n = symbols.declare_constant("fragment_n", i);
  const auto nt = terms.mk_constant(n);
  const core::ModelValue one{i, util::Rational(1)}, two{i, util::Rational(2)};
  std::vector<theory::ComponentModelFragment> numeric{{{"N", 1, 1}, {{nt, one}}, {}, {}},
                                                      {{"M", 1, 2}, {{nt, two}}, {}, {}}};
  rejects([&] { smt::assemble_component_model(terms, {}, numeric, {}, nullptr, nullptr); });
  numeric.resize(1);
  const std::vector false_assertions{terms.mk_eq(nt, terms.mk_rational(util::Rational(2), i))};
  rejects([&] {
    smt::assemble_component_model(terms, false_assertions, numeric, {}, nullptr, nullptr);
  });
  numeric[0].bindings[0].value.payload = util::Rational(1, 2);
  rejects([&] { smt::assemble_component_model(terms, {}, numeric, {}, nullptr, nullptr); });

  // Equal evaluated arguments must select a single exported function row.
  const auto f = symbols.declare_function("fragment_f", {i}, i);
  const auto app = terms.mk_app(f, {nt});
  const auto equivalent =
      terms.mk_app(f, {terms.mk_plus({nt, terms.mk_rational(util::Rational(0), i)})});
  numeric[0].bindings[0].value = one;
  numeric[0].interpretations = {{f, one, {{{one}, two}}}};
  const std::vector function_assertions{terms.mk_eq(app, equivalent),
                                        terms.mk_eq(app, terms.mk_rational(util::Rational(2), i))};
  const auto function_model =
      smt::assemble_component_model(terms, function_assertions, numeric, {}, nullptr, nullptr);
  assert(function_model->apply(f, std::vector{one}) == two);
  numeric[0].interpretations[0].rows.push_back({{one}, one});
  rejects([&] {
    smt::assemble_component_model(terms, function_assertions, numeric, {}, nullptr, nullptr);
  });
  // The completed immutable snapshot does not retain the mutable export rows.
  assert(function_model->apply(f, std::vector{one}) == two);
}

void typed_tables_and_validation() {
  core::SymbolTable symbols;
  const auto i = symbols.int_sort();
  const auto r = symbols.real_sort();
  const auto f = symbols.declare_function("f", {i, symbols.bool_sort()}, i);
  const core::ModelValue zero{i, util::Rational(0)}, one{i, util::Rational(1)};
  const core::ModelValue yes{symbols.bool_sort(), true};
  const core::Model model(symbols, {{f, zero, {{{one, yes}, one}}}});
  assert(model.apply(f, std::vector{one, yes}) == one);
  assert(model.apply(f, std::vector{zero, yes}) == zero);
  rejects([&] { model.apply(f, std::vector{one}); });
  rejects([&] { model.apply(f, std::vector{core::ModelValue{r, util::Rational(1)}, yes}); });
  rejects([&] { core::Model bad(symbols, {{f, zero, {{{one, yes}, one}, {{one, yes}, zero}}}}); });
  rejects([&] { core::Model bad(symbols, {{f, {i, util::Rational(1, 2)}, {}}}); });
  rejects([&] { core::Model bad(symbols, {{f, {i, true}, {}}}); });
  const auto a = symbols.declare_sort("A");
  const auto b = symbols.declare_sort("B");
  assert((core::ModelValue{a, core::DomainValue{0}} != core::ModelValue{b, core::DomainValue{0}}));
  assert((core::ModelValue{i, util::Rational(1)} != core::ModelValue{r, util::Rational(1)}));
}

void interpreted_terms_use_the_frozen_table() {
  core::SymbolTable symbols;
  const auto i = symbols.int_sort();
  const auto x = symbols.declare_constant("x", i);
  const auto f = symbols.declare_function("f", {i}, i);
  const auto p = symbols.declare_constant("p", symbols.bool_sort());
  // Missing internal interpretation is intentional: the unselected branch
  // must not be evaluated or consult an unrelated term-value cache.
  const auto absent = symbols.declare_internal_constant("$primo_absent", i);
  core::TermManager terms(symbols);
  const auto n = [&](int value) { return terms.mk_rational(util::Rational(value), i); };
  const core::Model model(
      symbols, {{x, {i, util::Rational(2)}, {}},
                {f, {i, util::Rational(0)}, {{{{i, util::Rational(3)}}, {i, util::Rational(7)}}}},
                {p, {symbols.bool_sort(), true}, {}}});
  core::ModelEvaluator evaluator(terms, model);
  const auto app = terms.mk_app(f, {terms.mk_plus({terms.mk_constant(x), n(1)})});
  const auto y = terms.mk_plus({app, n(1)});
  assert(evaluator.evaluate(y) == (core::ModelValue{i, util::Rational(8)}));
  const auto condition = terms.mk_and({terms.mk_constant(p), terms.mk_lt(n(1), app)});
  const auto selected = terms.mk_ite(condition, y, terms.mk_constant(absent));
  assert(evaluator.evaluate(selected) == evaluator.evaluate(y));
  const auto checked = terms.mk_eq(selected, n(8));
  assert(std::get<bool>(evaluator.evaluate(checked).payload));
  const auto before = evaluator.evaluated_terms();
  evaluator.evaluate(checked);
  assert(evaluator.evaluated_terms() == before);
  rejects([&] { evaluator.evaluate(terms.mk_constant(absent)); });
}

void exact_integer_and_real_semantics() {
  core::SymbolTable symbols;
  const auto x = symbols.declare_constant("x", symbols.int_sort());
  const auto y = symbols.declare_constant("y", symbols.real_sort());
  core::TermManager terms(symbols);
  const auto term = terms.mk_constant(x);
  const auto third = terms.mk_constant(y);
  for (int value = -50; value <= 50; ++value) {
    const core::Model model(symbols, {{x, {symbols.int_sort(), util::Rational(value)}, {}},
                                      {y, {symbols.real_sort(), util::Rational(1, 3)}, {}}});
    core::ModelEvaluator evaluator(terms, model);
    for (int divisor = 1; divisor <= 5; ++divisor) {
      const auto quotient = terms.mk_int_div(util::Rational(divisor), term);
      const auto remainder = terms.mk_int_mod(util::Rational(divisor), term);
      const int expected = value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
      assert(std::get<util::Rational>(evaluator.evaluate(quotient).payload) ==
             util::Rational(expected));
      assert(std::get<util::Rational>(evaluator.evaluate(remainder).payload) ==
             util::Rational(value - divisor * expected));
      assert(std::get<util::Rational>(evaluator.evaluate(terms.mk_abs(term)).payload) ==
             util::Rational(value < 0 ? -value : value));
    }
    assert(std::get<util::Rational>(
               evaluator.evaluate(terms.mk_times(util::Rational(-3), third)).payload) ==
           util::Rational(-1));
  }
}

void boolean_connectives() {
  core::SymbolTable symbols;
  const auto p = symbols.declare_constant("p", symbols.bool_sort());
  const auto q = symbols.declare_constant("q", symbols.bool_sort());
  core::TermManager terms(symbols);
  const auto a = terms.mk_constant(p), b = terms.mk_constant(q);
  for (const bool av : {false, true}) {
    for (const bool bv : {false, true}) {
      const core::Model model(
          symbols, {{p, {symbols.bool_sort(), av}, {}}, {q, {symbols.bool_sort(), bv}, {}}});
      core::ModelEvaluator evaluator(terms, model);
      const auto evaluate = [&](core::TermId term) {
        return std::get<bool>(evaluator.evaluate(term).payload);
      };
      assert(evaluate(terms.mk_not(a)) == !av);
      assert(evaluate(terms.mk_and({a, b})) == (av && bv));
      assert(evaluate(terms.mk_or({a, b})) == (av || bv));
      assert(evaluate(terms.mk_implies(a, b)) == (!av || bv));
      assert(evaluate(terms.mk_eq(a, b)) == (av == bv));
    }
  }
}

void deep_applications_are_iterative() {
  core::SymbolTable symbols;
  const auto i = symbols.int_sort();
  const auto f = symbols.declare_function("f", {i}, i);
  core::TermManager terms(symbols);
  const core::Model model(symbols, {{f, {i, util::Rational(0)}, {}}});
  auto term = terms.mk_rational(util::Rational(3), i);
  for (int depth = 0; depth < 30000; ++depth) term = terms.mk_app(f, {term});
  core::ModelEvaluator evaluator(terms, model);
  assert(evaluator.evaluate(term) == (core::ModelValue{i, util::Rational(0)}));
  assert(evaluator.evaluated_terms() == 30001);
}

void printer_owns_signature_and_preserves_meaning() {
  const core::Model model = [] {
    core::SymbolTable symbols;
    const auto i = symbols.int_sort();
    const auto x = symbols.declare_constant("x0", i);
    const auto y = symbols.declare_constant("a b", i);
    const auto f = symbols.declare_function("f", {i}, i);
    return core::Model(
        symbols,
        {{x, {i, util::Rational(2)}, {}},
         {y, {i, util::Rational(8)}, {}},
         {f, {i, util::Rational(0)}, {{{{i, util::Rational(3)}}, {i, util::Rational(7)}}}}});
  }();
  const std::string printed = smtlib::print_model(model);
  assert(printed == smtlib::print_model(model));
  assert(printed.find("|a b|") != std::string::npos);
  assert(printed.find("@primo_model_arg_0") != std::string::npos);
  const auto checked =
      smt::check_sat_smtlib("(set-logic QF_UFLIA)\n" + printed.substr(6, printed.size() - 7) +
                            "\n(assert (not (= |a b| (+ (f (+ x0 1)) 1))))\n(check-sat)\n");
  assert(checked.result == smt::CheckSatResult::Unsat);
}

void canonical_identifier_model_roundtrip() {
  for (const std::string name : {"", "17", "let", "check-sat", "x0", "a,b", "a\\b", "a\001b",
                                 "$primo_user", "$primo_bool_arg_0"}) {
    const std::string spelling = "|" + name + "|";
    smt::CheckSatOptions options;
    options.produce_model = true;
    options.check_model = true;
    const auto result = smt::check_sat_smtlib("(set-logic QF_UFLIA)(declare-const " + spelling +
                                                  " Int)"
                                                  "(declare-fun f (Int) Int)(assert (= " +
                                                  spelling +
                                                  " 2))"
                                                  "(assert (= (f 3) (+ " +
                                                  spelling + " 1)))(check-sat)",
                                              options);
    assert(result.result == smt::CheckSatResult::Sat);
    assert(result.typed_model);
    const auto& printed = result.model;
    const auto replay = smt::check_sat_smtlib(
        "(set-logic QF_UFLIA)" + printed.substr(6, printed.size() - 7) +
        "(assert (or (not (= " + spelling + " 2)) (not (= (f 3) 3))))(check-sat)");
    assert(replay.result == smt::CheckSatResult::Unsat);
  }
}
void reconstruction_uses_staged_constants_and_fixed_tables() {
  core::SymbolTable symbols;
  const auto i = symbols.int_sort();
  const auto x = symbols.declare_constant("x", i);
  const auto y = symbols.declare_constant("y", i);
  const auto f = symbols.declare_function("f", {i}, i);
  core::TermManager terms(symbols);
  const auto xt = terms.mk_constant(x);
  const auto yt = terms.mk_constant(y);
  const auto two = terms.mk_rational(util::Rational(2), i);
  const auto three = terms.mk_rational(util::Rational(3), i);
  const auto original =
      terms.mk_app(f, {terms.mk_plus({yt, terms.mk_rational(util::Rational(1), i)})});
  const auto live = terms.mk_app(f, {three});
  smt::ModelConversionChain conversions;
  conversions = conversions.appended(smt::DirectedEqualitySubstitutionConversion{{{xt, original}}});
  conversions = conversions.appended(smt::DirectedEqualitySubstitutionConversion{{{yt, two}}});
  const auto model =
      smt::build_typed_model(terms, {}, {}, {{live, util::Rational(7)}}, conversions);
  assert(model->apply(x) == core::ModelValue(i, util::Rational(7)));
  assert(model->apply(y) == core::ModelValue(i, util::Rational(2)));
  assert(model->interpretations().at(f).rows.size() == 1);
  core::ModelEvaluator evaluator(terms, *model);
  assert(evaluator.evaluate(original) == evaluator.evaluate(xt));

  // Intermediate versions must not collapse into a last-definition-wins map.
  auto versions = smt::ModelConversionChain{}.appended(
      smt::DirectedEqualitySubstitutionConversion{{{xt, two}}});
  versions = versions.appended(smt::DirectedEqualitySubstitutionConversion{{{xt, three}}});
  const auto restored = smt::build_typed_model(terms, {}, {}, {}, versions);
  assert(restored->apply(x) == core::ModelValue(i, util::Rational(2)));
  // The unrelated applications retained in the arena did not acquire rows.
  assert(restored->interpretations().at(f).rows.empty());

  const auto self = terms.mk_app(f, {xt});
  const auto equality = terms.mk_eq(xt, self);
  const std::vector facts{smt::TheoryModelAssignment{equality, true, false}};
  const auto cyclic = smt::build_typed_model(terms, {}, facts, {}, {});
  core::ModelEvaluator cyclic_evaluator(terms, *cyclic);
  assert(cyclic_evaluator.evaluate(equality) == core::ModelValue(symbols.bool_sort(), true));

  const auto fx = terms.mk_app(f, {xt});
  const auto fy = terms.mk_app(f, {yt});
  rejects([&] {
    smt::build_typed_model(terms, {}, {},
                           {{xt, util::Rational(2)},
                            {yt, util::Rational(2)},
                            {fx, util::Rational(3)},
                            {fy, util::Rational(4)}},
                           {});
  });
}
}  // namespace

int main() {
  simplified_finite_domain_arguments_reconstruct_the_original_model();
  driver_boolean_facts_do_not_bypass_fragment_validation();
  canonical_identifier_model_roundtrip();
  component_fragments_are_owned_typed_and_checked();
  typed_tables_and_validation();
  interpreted_terms_use_the_frozen_table();
  exact_integer_and_real_semantics();
  boolean_connectives();
  deep_applications_are_iterative();
  printer_owns_signature_and_preserves_meaning();
  reconstruction_uses_staged_constants_and_fixed_tables();
}
