#ifdef NDEBUG
#undef NDEBUG
#endif

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <ios>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <vector>

#include "core/ids.hpp"
#include "core/symbol_table.hpp"
#include "core/term_manager.hpp"
#include "smt/check_sat.hpp"
#include "smt/frontend.hpp"
#include "smtlib/parser.hpp"
#include "smtlib/term_printer.hpp"
#include "util/rational.hpp"

using primo::core::TermId;
using primo::smt::Frontend;
using primo::smtlib::print_term;

namespace {

const std::string kPrelude = R"(
  (set-logic UF)
  (declare-sort U 0)
  (declare-sort |odd sort| 0)
  (declare-fun a () U)
  (declare-fun |b c| () U)
  (declare-fun f (U) U)
  (declare-fun p (U U) Bool)
  (declare-fun q (|odd sort|) Bool)
  (declare-fun ?x0 () U)
  (declare-const k Bool)
)";

// The assertions of `commands` after the prelude, printed.
std::vector<std::string> printed(const std::string& commands) {
  Frontend frontend;
  std::vector<TermId> assertions;
  for (const auto& command : primo::smtlib::Parser(kPrelude + commands).parse_commands()) {
    frontend.process_command(command, assertions);
  }
  std::vector<std::string> result;
  for (const TermId assertion : assertions) {
    result.push_back(print_term(frontend.terms(), assertion));
  }
  return result;
}

// Printing, parsing the text and printing again gives the same text.
void round_trip_is_stable() {
  const std::vector<std::string> formulas = {
      "(forall ((x U) (y U)) (=> (p x y) (p y x)))",
      "(forall ((x U)) (exists ((y U)) (and (= (f y) x) (not (= y ?x0)))))",
      "(forall ((x U)) (! (forall ((x U)) (p x x)) :pattern ((f x))))",
      "(exists ((s |odd sort|)) (or (q s) k))",
      "(forall ((x U) (y U)) (= (ite (p x y) x y) (f |b c|)))",
      "(or k (forall ((x U)) (= x a)))",
  };
  std::string commands;
  for (const auto& formula : formulas) commands += "(assert " + formula + ")";
  const std::vector<std::string> first = printed(commands);
  assert(first.size() == formulas.size());
  std::string reprinted;
  for (const auto& text : first) reprinted += "(assert " + text + ")";
  assert(printed(reprinted) == first);
}

// Generated variable names avoid declared symbols; other names are quoted as
// SMT-LIB requires.
void names_are_fresh_and_quoted() {
  const std::string text = printed("(assert (forall ((x U)) (= x |b c|)))").front();
  assert(text.starts_with("(forall ((?x1 U)) "));
  assert(text.find("|b c|") != std::string::npos);
  const std::string sorted = printed("(assert (forall ((s |odd sort|)) (q s)))").front();
  assert(sorted.find("(?x1 |odd sort|)") != std::string::npos);
}

// Printed assertions give the same answers as the originals.
void answers_survive_printing() {
  for (const std::string problem :
       {"(assert (forall ((x U) (y U)) (= x y))) (assert (not (= a |b c|)))",
        "(assert (exists ((x U)) (p x a)))"}) {
    const auto original = primo::smt::check_sat_smtlib(kPrelude + problem + "(check-sat)");
    std::string commands;
    for (const auto& text : printed(problem)) commands += "(assert " + text + ")";
    const auto reprinted = primo::smt::check_sat_smtlib(kPrelude + commands + "(check-sat)");
    assert(original.result == reprinted.result);
  }
}

// Open and arithmetic terms have no text in the quantified UF fragment.
void open_and_arithmetic_terms_are_rejected() {
  Frontend frontend;
  std::vector<TermId> assertions;
  for (const auto& command : primo::smtlib::Parser(kPrelude).parse_commands()) {
    frontend.process_command(command, assertions);
  }
  auto& terms = frontend.terms();
  const auto u = *terms.symbols().lookup_sort("U");
  bool threw = false;
  try {
    static_cast<void>(print_term(terms, terms.mk_bound_variable(0, u)));
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  assert(threw);
  threw = false;
  try {
    static_cast<void>(print_term(terms, terms.mk_rational(primo::util::Rational{})));
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  assert(threw);
}

// A shared term DAG can have a large textual expansion. The streaming API
// emits bounded tokens rather than caching those expanded strings.
void ground_streaming() {
  Frontend frontend;
  std::vector<TermId> assertions;
  for (const auto& command :
       primo::smtlib::Parser(kPrelude +
                             "(declare-fun g (U U) U)"
                             "(assert (=> (p a a) (or k (not (= (ite k a |b c|) (f a))))))")
           .parse_commands()) {
    frontend.process_command(command, assertions);
  }
  for (const auto root : assertions) {
    std::ostringstream output;
    primo::smtlib::print_ground_term(output, frontend.terms(), root);
    assert(output.str() == print_term(frontend.terms(), root));
  }
  struct CountingBuffer final : std::streambuf {
    std::size_t bytes = 0;
    std::streamsize largest = 0;
    std::streamsize xsputn(const char*, std::streamsize count) override {
      bytes += static_cast<std::size_t>(count);
      largest = std::max(largest, count);
      return count;
    }
    int_type overflow(int_type value) override {
      if (!traits_type::eq_int_type(value, traits_type::eof())) ++bytes;
      return traits_type::not_eof(value);
    }
  } buffer;
  auto& terms = frontend.terms();
  const auto a = *terms.symbols().lookup_symbol("a");
  const auto g = *terms.symbols().lookup_symbol("g");
  TermId root = terms.mk_constant(a);
  std::size_t expected_bytes = 1;
  for (int depth = 0; depth < 18; ++depth) {
    root = terms.mk_app(g, {root, root});
    expected_bytes = 5 + 2 * expected_bytes;
  }
  std::ostream output(&buffer);
  primo::smtlib::print_ground_term(output, terms, root);
  assert(buffer.bytes == expected_bytes && buffer.largest <= 5);
}

void arithmetic_streaming_round_trip() {
  const std::string source =
      "(set-logic QF_LRA)(declare-fun |real x| () Real)(declare-fun y () Real)"
      "(assert (<= (+ (* (- (/ 2 7)) |real x|) y) (- (/ 3 11))))"
      "(assert (< (+ |real x| (* 2 y)) 0))";
  Frontend frontend;
  std::vector<TermId> assertions;
  for (const auto& command : primo::smtlib::Parser(source).parse_commands())
    frontend.process_command(command, assertions);
  std::string printed = "(set-logic QF_LRA)(declare-fun |real x| () Real)(declare-fun y () Real)";
  for (const auto root : assertions) {
    std::ostringstream output;
    primo::smtlib::print_ground_term(output, frontend.terms(), root);
    printed += "(assert " + output.str() + ")";
  }
  Frontend reparsed;
  std::vector<TermId> repeated;
  for (const auto& command : primo::smtlib::Parser(printed).parse_commands())
    reparsed.process_command(command, repeated);
  assert(repeated.size() == assertions.size());
  for (std::size_t i = 0; i < repeated.size(); ++i) {
    std::ostringstream before;
    std::ostringstream after;
    primo::smtlib::print_ground_term(before, frontend.terms(), assertions[i]);
    primo::smtlib::print_ground_term(after, reparsed.terms(), repeated[i]);
    assert(before.str() == after.str());
  }
}

}  // namespace

int main() {
  arithmetic_streaming_round_trip();
  ground_streaming();
  round_trip_is_stable();
  names_are_fresh_and_quoted();
  answers_survive_printing();
  open_and_arithmetic_terms_are_rejected();
  return 0;
}
