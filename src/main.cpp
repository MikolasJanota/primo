#include <signal.h>
#include <unistd.h>

#include <CLI/CLI.hpp>
#include <array>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cli/option_names.hpp"
#include "cli/response_printer.hpp"
#include "generated/primo_git_revision.hpp"
#include "sat/cadical_backend.hpp"
#include "smt/check_sat.hpp"
#include "smt/experimental_tactics.hpp"
#include "smt/smt_session.hpp"
#include "smtlib/parser.hpp"
#include "util/statistics.hpp"

namespace {

volatile std::sig_atomic_t g_cancel_requested = 0;
volatile std::sig_atomic_t g_cancel_grace_seconds = 1;

// Cooperative cancellation is the path that can still flush statistics, but it
// is only observed at tactic boundaries and inside the SAT backend. A signal
// arriving during parsing, frontend construction, or one preprocessing pass is
// therefore not acted on until that phase ends, and a harness that escalates to
// SIGKILL records the run as a crash rather than as unknown. Arming an alarm
// bounds that wait. alarm() is async-signal-safe, which is what keeps this
// handler legal; the price is that the deadline handler below can only write,
// so a run that reaches it reports no statistics.
void handle_cancel_signal(int) {
  g_cancel_requested = 1;
  alarm(static_cast<unsigned int>(g_cancel_grace_seconds));
}

void handle_cancel_deadline(int) {
  static const char kUnknownAnswer[] = "unknown\n";
  const ssize_t written = write(STDOUT_FILENO, kUnknownAnswer, sizeof(kUnknownAnswer) - 1);
  static_cast<void>(written);
  _exit(0);
}

void install_cancellation_handlers() {
  struct sigaction cancel;
  cancel.sa_handler = handle_cancel_signal;
  sigemptyset(&cancel.sa_mask);
  cancel.sa_flags = 0;
  sigaction(SIGTERM, &cancel, nullptr);
  sigaction(SIGINT, &cancel, nullptr);

  struct sigaction deadline;
  deadline.sa_handler = handle_cancel_deadline;
  sigemptyset(&deadline.sa_mask);
  deadline.sa_flags = 0;
  sigaction(SIGALRM, &deadline, nullptr);
}

std::string version_text() {
  std::ostringstream version;
  version << "primo 0.2.0\n";
  version << "git: " << PRIMO_GIT_REVISION << "\n";
  version << "build: " << PRIMO_BUILD_TYPE << ", sanitizers: " << PRIMO_SANITIZERS_BANNER << "\n";
  version << "cadical: " << primo::sat::cadical_signature();
  return version.str();
}

void print_startup_banner() {
  std::cerr << "primo git " << PRIMO_GIT_REVISION << " (" << PRIMO_BUILD_TYPE
            << ", sanitizers: " << PRIMO_SANITIZERS_BANNER << ")\n";
  std::cerr << "cadical " << primo::sat::cadical_signature() << "\n";
}

int execute_stream(std::istream& input, const primo::smt::CheckSatOptions& options,
                   bool print_stats) {
  if (!options.export_cnf.empty()) {
    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (input.bad()) throw std::runtime_error("cannot read export input");
    const auto outcome = primo::smt::check_sat_smtlib(buffer.str(), options);
    primo::smt::CommandResponse response;
    response.kind = primo::smt::ResponseKind::CheckSat;
    response.outcome = std::make_shared<const primo::smt::CheckSatOutcome>(outcome);
    primo::cli::print_response(std::cout, response);
    std::cout.flush();
    if (print_stats) primo::util::print_statistics(std::cerr, outcome.statistics);
    if (!outcome.message.empty()) std::cerr << outcome.message << '\n';
    if (g_cancel_requested != 0) alarm(0);
    return outcome.result == primo::smt::CheckSatResult::Unsupported ? 2 : 0;
  }
  primo::smt::SmtSession session(options);
  primo::smtlib::Parser parser(input);
  int exit_code = 0;
  bool answered = false;
  while (!session.terminated() && g_cancel_requested == 0) {
    primo::smt::CommandResponse response;
    try {
      const auto start = std::chrono::steady_clock::now();
      auto command = parser.next_command();
      if (!command) break;
      const auto parse_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - start)
                                .count();
      response = session.execute(std::move(*command), static_cast<std::uint64_t>(parse_us));
    } catch (const std::exception& error) {
      response.kind = primo::smt::ResponseKind::Error;
      response.message = error.what();
    }
    primo::cli::print_response(std::cout, response);
    std::cout.flush();
    answered = response.kind == primo::smt::ResponseKind::CheckSat ||
               response.kind == primo::smt::ResponseKind::Model;
    if (response.kind == primo::smt::ResponseKind::CheckSat && print_stats) {
      primo::util::print_statistics(std::cerr, response.outcome->statistics);
    }
    if (!response.message.empty()) std::cerr << response.message << '\n';
    if (response.kind == primo::smt::ResponseKind::Error) return 2;
    if (response.kind == primo::smt::ResponseKind::Unsupported ||
        (response.outcome && response.outcome->result == primo::smt::CheckSatResult::Unsupported)) {
      exit_code = 2;
    }
  }
  if (g_cancel_requested != 0) {
    if (!answered) std::cout << "unknown\n" << std::flush;
    alarm(0);
  }
  return exit_code;
}

std::optional<primo::smt::AutoBudget> parse_auto_budget(const std::string& text) {
  primo::smt::AutoBudget budget;
  if (text == "auto") {
    budget.kind = primo::smt::AutoBudget::Kind::Auto;
    return budget;
  }
  if (text == "unlimited") {
    budget.kind = primo::smt::AutoBudget::Kind::Unlimited;
    return budget;
  }
  if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos) {
    return std::nullopt;
  }
  try {
    std::size_t consumed = 0;
    const unsigned long long value = std::stoull(text, &consumed);
    if (consumed != text.size()) {
      return std::nullopt;
    }
    if (value > std::numeric_limits<std::size_t>::max()) {
      return std::nullopt;
    }
    budget.kind = primo::smt::AutoBudget::Kind::Fixed;
    budget.value = static_cast<std::size_t>(value);
    return budget;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

// Inverse of parse_auto_budget, for --help defaults.
std::string format_auto_budget(const primo::smt::AutoBudget& budget) {
  switch (budget.kind) {
    case primo::smt::AutoBudget::Kind::Auto:
      return "auto";
    case primo::smt::AutoBudget::Kind::Unlimited:
      return "unlimited";
    case primo::smt::AutoBudget::Kind::Fixed:
      return std::to_string(budget.value);
  }
  return "auto";
}

std::optional<std::pair<std::string, int>> parse_cadical_option(const std::string& text) {
  const std::size_t separator = text.find('=');
  if (separator == std::string::npos || separator == 0 || separator + 1 == text.size()) {
    return std::nullopt;
  }
  try {
    std::size_t consumed = 0;
    const long value = std::stol(text.substr(separator + 1), &consumed);
    if (consumed != text.size() - separator - 1) {
      return std::nullopt;
    }
    if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) {
      return std::nullopt;
    }
    return std::pair{text.substr(0, separator), static_cast<int>(value)};
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

CLI::Option* add_flag_with_default(CLI::App& app, std::string name, bool& value,
                                   std::string description) {
  CLI::Option* option = app.add_flag(std::move(name), value, std::move(description));
  option->option_text(value ? "[default: on]" : "[default: off]");
  return option;
}

// An option selecting an enumerator of `value` by its name in `names`. The
// parsed value is written straight into `value`, and --help shows the name of
// the value it holds when the option is added, so the default is written only
// where `value` is initialized. The callback borrows `value` and `names`:
// both must outlive the app, and the table must remain unchanged. Call sites
// use static name tables and options declared before the app.
template <typename Enum, std::size_t N>
CLI::Option* add_enum_option(CLI::App& app, std::string name, Enum& value,
                             const std::array<primo::cli::EnumName<Enum>, N>& names,
                             std::string description) {
  std::vector<std::string> choices;
  for (const auto& entry : names) {
    choices.emplace_back(entry.name);
  }
  CLI::Option* option = app.add_option_function<std::string>(
      name,
      [name, &value, &names](const std::string& text) {
        const auto parsed = primo::cli::parse_enum(names, text);
        if (!parsed) throw CLI::ValidationError(name, "unrecognized value: " + text);
        value = *parsed;
      },
      std::move(description));
  option->check(CLI::IsMember(std::move(choices)));
  option->default_str(std::string(primo::cli::enum_name(names, value)));
  return option;
}

// --help section headings. CLI11 emits sections in order of the first
// option registered into each, so the printed order follows registration
// order in main() -- currently Procedural, QF_UF, QF_LRA, Preprocessing,
// SAT -- not the order of these declarations.
constexpr const char* kGroupLra = "QF_LRA theory options";
constexpr const char* kGroupLia = "QF_LIA theory options";
constexpr const char* kGroupUf = "QF_UF / EUF theory options";
constexpr const char* kGroupPreprocessing = "Preprocessing options";
constexpr const char* kGroupSat = "SAT and DPLL(T) options";
constexpr const char* kGroupProcedural = "Procedural options";
constexpr const char* kGroupExperimentalTactics = "Experimental tactic options";

}  // namespace

int run_main(int argc, char** argv) {
  install_cancellation_handlers();
  // Wired before the command line is parsed so a signal arriving during
  // parsing is not dropped on the floor.
  primo::smt::CheckSatOptions solver_options;
  solver_options.cancelled = [] { return g_cancel_requested != 0; };
  std::vector<std::string> files;
  bool quiet = false;
  bool print_stats = false;
  unsigned int cancel_grace_seconds = static_cast<unsigned int>(g_cancel_grace_seconds);
  bool no_uf_symmetry_breaking = false;
  bool no_finite_domain_value_precedence = false;
  bool finite_domain_equality_definitions = solver_options.auto_finite_domain_equality_definitions;
  bool no_finite_domain_equality_definitions = false;
  bool no_finite_domain_post_amo = false;
  bool no_extended_simplification = false;
  bool no_ite_equality_distribution = false;
  bool no_cite_simplification = false;
  bool no_eq_bridge = false;
  bool no_quantifier_instance_simplification = false;
  bool no_lra_propagation = false;
  bool no_solved_constant_substitution = false;
  bool no_gaussian_elimination = false;
  bool no_lia_conflict_generalization = false;
  bool no_qf_uflra_fixed_elimination = false;
  bool qf_uflra_candidate_final_scan = solver_options.qf_uflra_candidate_final_scan;
  bool qf_uflra_model_reconciliation = solver_options.qf_uflra_model_reconciliation;
  bool no_qf_uflra_model_reconciliation = false;
  bool experimental_generic_model_combination = false;
  bool qf_uflia_model_combination = solver_options.qf_uflia_model_combination;
  bool no_qf_uflia_model_combination = false;
  bool qf_uflia_online_components = solver_options.qf_uflia_online_components;
  bool no_qf_uflia_online_components = false;
  std::size_t qf_uflia_model_decision_budget = solver_options.qf_uflia_model_decision_budget;
  bool qf_uflra_online_euf = solver_options.qf_uflra_online_euf;
  bool guarded_real_equality_lowering = solver_options.guarded_real_equality_lowering;
  bool lra_model_phase = solver_options.lra_model_phase.value_or(false);
  bool no_lra_model_phase = false;
  bool monotone_elimination = solver_options.monotone_elimination.value_or(false);
  bool no_monotone_elimination = false;
  bool top_level_or_tseitin = solver_options.top_level_or_tseitin;
  bool no_top_level_or_tseitin = false;
  bool no_auto_top_level_or_tseitin = false;
  bool no_auto_basic_single = false;
  bool no_auto_basic_single_predicate_polarity = false;
  bool auto_basic_multi = solver_options.auto_basic_multi_preprocessing;
  bool no_auto_basic_multi = false;
  bool no_auto_basic_multi_polarity = false;
  std::string qf_uf_profile = "default";
  bool no_lra_sparse_leaving = false;
  bool lra_least_violated_leaving = false;
  bool euf_proof_forest_diagnostics = false;
  bool euf_diseq_propagation_diagnostics = false;
  bool euf_ackermann_diagnostics = false;
  bool nelson_oppen_diagnostics = false;
  bool nelson_oppen_model_equality_branching = false;
  bool no_nelson_oppen_model_equality_mutation = false;
  bool euf_dynamic_ackermann_shadow = solver_options.euf_dynamic_ackermann_shadow;
  bool euf_diseq_ackermann = solver_options.euf_diseq_ackermann;
  std::string euf_prop_budget = format_auto_budget(solver_options.euf_propagation_budget);
  std::string euf_prop_delivery = format_auto_budget(solver_options.euf_propagation_delivery);
  std::size_t nelson_oppen_arrangement_probe_limit = 0;
  std::size_t max_theory_conflicts = 0;
  std::vector<std::string> cadical_options;
  CLI::App app{"Primo SMT solver scaffold"};
  app.add_option("--export-cnf", solver_options.export_cnf,
                 "Write generated CNF and conflicts for one UNSAT QF_UF query to FILE")
      ->type_name("FILE");
  add_enum_option(app, "--export-cnf-reduction", solver_options.export_cnf_reduction,
                  primo::cli::kExportCnfReductionNames,
                  "Reduce exported theory lemmas: none, an UNSAT core, or a subset-minimal MUS")
      ->needs("--export-cnf");
  app.add_flag("--experimental-export-erase-sorts", solver_options.experimental_export_erase_sorts,
               "EXPERIMENTAL, temporary: erase exported object sorts for checker testing")
      ->needs("--export-cnf");
  app.add_flag("--check-model", solver_options.check_model,
               "Check original assertions against produced typed models");
  app.option_defaults()->always_capture_default();
  add_flag_with_default(app, "-q,--quiet", quiet, "Suppress startup version banner");
  add_flag_with_default(app, "--stats", print_stats, "Print solver statistics to stderr");
  app.add_option("--experimental-split-bools", solver_options.experimental_split_bools,
                 "EXPERIMENTAL: split on up to k Boolean atoms, materialize 2^k disjunctive "
                 "goals, and solve them sequentially in false-first order; 0 disables")
      ->check(CLI::Range(0, static_cast<int>(primo::smt::kMaxExperimentalSplitBools)));
  add_flag_with_default(app, "--uf-symmetry-breaking", solver_options.uf_symmetry_breaking,
                        "Enable QF_UF symmetry breaking over invariant finite-domain values");
  add_flag_with_default(app, "--no-uf-symmetry-breaking", no_uf_symmetry_breaking,
                        "Disable QF_UF finite-domain symmetry breaking");
  add_flag_with_default(
      app, "--finite-domain-value-precedence", solver_options.finite_domain_value_precedence,
      "Enable fixed-term value precedence inside finite-domain symmetry breaking");
  add_flag_with_default(
      app, "--no-finite-domain-value-precedence", no_finite_domain_value_precedence,
      "Disable fixed-term value precedence inside finite-domain symmetry breaking");
  add_flag_with_default(app, "--finite-domain-equality-definitions",
                        finite_domain_equality_definitions,
                        "Enable guarded finite-domain equality definitions in auto preprocessing");
  add_flag_with_default(app, "--no-finite-domain-equality-definitions",
                        no_finite_domain_equality_definitions,
                        "Disable finite-domain SAT-visible equality definition clauses");
  add_flag_with_default(app, "--no-finite-domain-post-amo", no_finite_domain_post_amo,
                        "Disable the post-simplification finite-domain AMO scan");
  add_flag_with_default(app, "--no-extended-simplification", no_extended_simplification,
                        "Disable the ite/div/mod/abs rewrites that run before lowering "
                        "(lowering itself always runs)");
  app.add_option("--extended-simplification-rounds", solver_options.extended_simplification_rounds,
                 "Traversals the extended-term simplification runs; more rounds reach deeper "
                 "nested ite, at a cost that is exponential in the limit")
      ->capture_default_str()
      ->check(CLI::Range(std::size_t{0}, std::size_t{64}));
  add_flag_with_default(app, "--ite-equality-distribution",
                        solver_options.ite_equality_distribution,
                        "Distribute (= u (ite c a b)) over the branches when neither branch is "
                        "itself an ite");
  add_flag_with_default(app, "--no-ite-equality-distribution", no_ite_equality_distribution,
                        "Disable (= u (ite c a b)) distribution");
  add_flag_with_default(app, "--cite-simplification", solver_options.cite_simplification,
                        "Decide atoms over constant if-then-else terms branch by branch "
                        "(open-leads lead 16, piece 1)");
  add_flag_with_default(app, "--no-cite-simplification", no_cite_simplification,
                        "Disable constant if-then-else simplification");
  add_flag_with_default(app, "--quantifier-instance-simplification",
                        solver_options.quantifier_instance_simplification,
                        "Quantified UF: normalize instances before duplicate detection");
  add_flag_with_default(app, "--no-quantifier-instance-simplification",
                        no_quantifier_instance_simplification,
                        "Quantified UF: compare and assert instances as instantiated");
  add_flag_with_default(app, "--eq-bridge", solver_options.eq_bridge,
                        "Add equalities implied by every branch of selected QF_UF disjunctions");
  add_flag_with_default(app, "--no-eq-bridge", no_eq_bridge,
                        "Disable equality bridging unless required by the preprocessing preset");
  add_flag_with_default(app, "--lra-propagation", solver_options.lra_propagation,
                        "Enable QF_LRA same-slack bound propagation (Milestone 3b)");
  add_flag_with_default(app, "--no-lra-propagation", no_lra_propagation,
                        "Disable QF_LRA bound propagation (conflict detection only)");
  add_flag_with_default(app, "--lra-row-propagation", solver_options.lra_row_propagation,
                        "Enable experimental QF_LRA row-based/interval bound propagation across "
                        "different variables (only takes effect if --lra-propagation is also on)");
  add_flag_with_default(app, "--lra-bidirectional-row-propagation",
                        solver_options.lra_bidirectional_row_propagation,
                        "Enable the reverse direction of --lra-row-propagation: tighten a row's "
                        "other terms from its own bound, not just derive the row's own implied "
                        "bound (only takes effect if --lra-row-propagation is also on)");
  app.add_option("--lra-row-propagation-max-row-size",
                 solver_options.lra_row_propagation_limits.max_row_size,
                 "Cap on the row length --lra-row-propagation will derive an implied bound "
                 "for. Rows longer than this are skipped, not truncated")
      ->check(CLI::PositiveNumber);
  app.add_option("--lra-row-propagation-max-fanout",
                 solver_options.lra_row_propagation_limits.max_fanout,
                 "Cap on how many rows a single bound change may trigger a "
                 "--lra-row-propagation scan over. A variable occurring in more "
                 "rows than this propagates nothing that round")
      ->check(CLI::PositiveNumber);
  app.add_option("--lra-bidirectional-row-propagation-max-row-size",
                 solver_options.lra_row_propagation_limits.bidirectional_max_row_size,
                 "Row-length cap for the reverse direction "
                 "(--lra-bidirectional-row-propagation). Smaller than the forward "
                 "cap because that direction costs O(row size) per other term, not per row")
      ->check(CLI::PositiveNumber);
  app.add_option("--lra-bidirectional-row-propagation-max-fanout",
                 solver_options.lra_row_propagation_limits.bidirectional_max_fanout,
                 "Fanout cap for the reverse direction (--lra-bidirectional-row-propagation)")
      ->check(CLI::PositiveNumber);
  add_flag_with_default(app, "--lra-theory-decisions", solver_options.lra_theory_decisions,
                        "Enable experimental QF_LRA theory-guided decisions: the arithmetic theory "
                        "ranks candidate atoms via CaDiCaL's cb_decide and supplies the decision "
                        "literal itself instead of leaving every decision to generic VSIDS");
  add_flag_with_default(app, "--lra-equality-propagation", solver_options.lra_equality_propagation,
                        "Enable experimental pure QF_LRA model-unit equality discovery and "
                        "propagation of existing difference comparisons (IPASIR-UP only)");
  add_flag_with_default(app, "--lra-model-phase", lra_model_phase,
                        "Enable model-based SAT phase hints (automatic for QF_LRA rebuild "
                        "queries with auto preprocessing): keep CaDiCaL's "
                        "variable choice, but refresh arithmetic atom polarity from the current "
                        "tableau assignment at quiescence points")
      ->option_text("[default: auto]");
  add_flag_with_default(app, "--no-lra-model-phase", no_lra_model_phase,
                        "Disable model-based SAT phase hints");
  add_enum_option(app, "--lra-pivoting-rule", solver_options.lra_pivoting_rule,
                  primo::cli::kLraPivotingRuleNames,
                  "QF_LRA Check/Pivot variable-selection rule: sparse (prices "
                  "what a pivot costs: among the entering candidates that all repair the "
                  "violation equally, take the one occurring in the fewest rows, since "
                  "those are exactly the rows the pivot rewrites), bland "
                  "(anti-cycling-proven), dantzig "
                  "(largest-violation/largest-coefficient), or soi "
                  "(Sum-of-Infeasibilities Simplex, King/Barrett/Dutertre FMCAD 2013 -- a "
                  "global Check/Pivot objective across every violated row at once). All "
                  "but bland fall back to bland selection after enough pivots within one "
                  "check(), which is what keeps termination proven");
  add_flag_with_default(app, "--lra-least-violated-leaving", lra_least_violated_leaving,
                        "Experimental leaving-variable rule from Griggio's thesis (2.5.3, p.53):\n"
                        "pick the LEAST-violated basic variable, tie-broken by the shorter row.\n"
                        "Only meaningful under --lra-pivoting-rule sparse");
  add_flag_with_default(app, "--no-lra-sparse-leaving", no_lra_sparse_leaving,
                        "Disable the leaving-side half of --lra-pivoting-rule sparse "
                        "(prefer repairing a violated variable whose row is short, using "
                        "violation magnitude only as a tie-break), for ablation");
  app.add_option("--lra-sparse-pricing-candidates",
                 solver_options.lra_tuning.sparse_pricing_candidates,
                 "How many eligible entering candidates --lra-pivoting-rule sparse scores "
                 "before taking the best seen so far (0 means score every "
                 "eligible candidate). 1 is the cheapest possible pricing. Never cuts short "
                 "the search for whether an eligible candidate exists, so any value is sound")
      ->check(CLI::Range(0, 1000000));
  add_enum_option(app, "--lra-soi-minimize", solver_options.lra_tuning.soi_minimization,
                  primo::cli::kLraSoiMinimizationNames,
                  "How --lra-pivoting-rule soi shrinks a multi-row conflict before turning "
                  "it into a clause: quickxplain (Junker 2001), deletion "
                  "(iterative, and steerable by --lra-soi-minimize-order), or none "
                  "(ablation baseline). Inert under every other pivoting rule. Measured, "
                  "the error set averages 2.83 variables and never exceeded 9, so this "
                  "changes which minimal conflict is learned rather than how fast it is "
                  "found");
  add_enum_option(app, "--lra-soi-minimize-order", solver_options.lra_tuning.soi_minimization_order,
                  primo::cli::kLraSoiMinimizationOrderNames,
                  "Order --lra-soi-minimize deletion offers candidates for removal, worst "
                  "first: id (ascending, reproduces quickxplain's input order), "
                  "row-support (longest row first -- the row support dominates clause "
                  "length, so this sheds the rows contributing most literals), or "
                  "violation (largest bound violation first)");
  app.add_option("--lra-bland-fallback-factor", solver_options.lra_tuning.bland_fallback_factor,
                 "Per-variable coefficient of check()'s Bland's-rule fallback point, which is "
                 "factor * variables + offset pivots within one check. Smaller "
                 "switches to Bland's rule sooner: slower but anti-cycling-proven")
      ->check(CLI::Range(0, 1000000));
  app.add_option("--lra-bland-fallback-offset", solver_options.lra_tuning.bland_fallback_offset,
                 "Constant term of the Bland's-rule fallback point")
      ->check(CLI::Range(0, 1000000000));
  add_enum_option(app, "--mixed-dispatch", solver_options.mixed_dispatch,
                  primo::cli::kMixedDispatchNames,
                  "Which formulas reach the mixed EUF/LRA solver: narrow (only a "
                  "Bool-sorted function APPLICATION counts as an EUF predicate, so a formula "
                  "that is pure QF_LRA apart from propositional variables stays on the pure "
                  "path) or wide (a Bool-sorted constant counts too -- the pre-2026-08-07 "
                  "behaviour, routing any QF_LRA formula containing one propositional variable "
                  "to the mixed solver). Usually much worse, but genuinely better on some "
                  "instances; kept as a portfolio dimension and so differential fuzzing can "
                  "reach the mixed path");
  add_flag_with_default(
      app, "--lra-fixed-elimination", solver_options.lra_fixed_elimination,
      "Pivot a basic LRA variable out of the basis as soon as its bounds coincide "
      "(heuristics item H7). It nets to nothing on the scrambled "
      "competition set, but doubles the >5MB band, so it is worth enabling on "
      "equality-dominated input");
  add_flag_with_default(app, "--no-qf-uflra-fixed-elimination", no_qf_uflra_fixed_elimination,
                        "Disable mixed QF_UFLRA fixed-variable elimination, which exposes "
                        "EqRow interface equalities before the exact pair fallback");
  add_flag_with_default(app, "--ground-congruence", solver_options.ground_congruence,
                        "Enable bounded SAT-visible ground congruence clauses");
  add_flag_with_default(app, "--ground-unit-substitution", solver_options.ground_unit_substitution,
                        "Enable experimental ground substitution from visible unit equalities");
  add_flag_with_default(
      app, "--guarded-ground-unit-substitution", solver_options.guarded_ground_unit_substitution,
      "Enable guarded ground substitution only when preprocessing metrics predict a win");
  add_flag_with_default(app, "--no-solved-constant-substitution", no_solved_constant_substitution,
                        "Disable QF_UF solved-constant substitution");
  add_flag_with_default(app, "--gaussian-elimination", solver_options.gaussian_elimination,
                        "Enable QF_LRA Gauss-Jordan elimination over asserted linear "
                        "equality systems, feeding solve-eqs (campaign-fuzzed 2026-07-25)");
  add_flag_with_default(app, "--no-gaussian-elimination", no_gaussian_elimination,
                        "Disable QF_LRA Gauss-Jordan equality-system elimination");
  add_flag_with_default(
      app, "--qf-uflra-candidate-final-scan", qf_uflra_candidate_final_scan,
      "Enable experimental mixed QF_UFLRA candidate-first final LRA equality scans before "
      "falling back to the full shared-Real pair scan");
  add_enum_option(app, "--qf-uflra-lra-equality-producer",
                  solver_options.qf_uflra_lra_equality_producer,
                  primo::cli::kQfUflraLraEqualityProducerNames,
                  "LRA-to-EUF opportunistic equality producer: legacy exact-offset row indexes "
                  "or model-unit current-model-filtered unit rows; adaptive bootstraps legacy to "
                  "a propagation fixed point, then uses model-unit until a later shared-model "
                  "split");
  add_flag_with_default(
      app, "--qf-uflra-model-reconciliation", qf_uflra_model_reconciliation,
      "Explicitly enable convex QF_UFLRA shared-model reconciliation with scratch "
      "LRA mutation, EUF compatibility checks, and lazy positive equality splits");
  add_flag_with_default(app, "--no-qf-uflra-model-reconciliation", no_qf_uflra_model_reconciliation,
                        "Disable convex QF_UFLRA shared-model reconciliation and use the "
                        "explained exact equality-sharing fallback");
  add_flag_with_default(
      app, "--experimental-generic-model-combination", experimental_generic_model_combination,
      "Route QF_UFLRA shared-model decisions through the generic sequential coordinator");
  add_flag_with_default(
      app, "--qf-uflia-model-combination,--experimental-qf-uflia-model-combination",
      qf_uflia_model_combination,
      "Guide non-convex QF_UFLIA arrangement search with component models before complete "
      "fallback");
  add_flag_with_default(app, "--no-qf-uflia-model-combination", no_qf_uflia_model_combination,
                        "Disable QF_UFLIA model guidance and start from the complete arrangement");
  add_flag_with_default(
      app, "--qf-uflia-online-components,--experimental-qf-uflia-online-components",
      qf_uflia_online_components,
      "Reuse trail-synchronized EUF and rational-arithmetic state during QF_UFLIA SAT search");
  add_flag_with_default(app, "--no-qf-uflia-online-components", no_qf_uflia_online_components,
                        "Disable trail-synchronized QF_UFLIA components");
  app.add_option("--qf-uflia-model-decision-budget", qf_uflia_model_decision_budget,
                 "Maximum QF_UFLIA model-choice batches before complete arrangement fallback");
  add_flag_with_default(
      app, "--qf-uflra-online-euf", qf_uflra_online_euf,
      "Enable experimental incremental EUF callbacks and live final-model closure reuse "
      "in mixed QF_UFLRA solving");
  add_flag_with_default(app, "--monotone-elimination", monotone_elimination,
                        "Enable monotone (one-sided) real-variable elimination (automatic for\n"
                        "QF_LRA rebuild queries with auto preprocessing): a\n"
                        "variable every constraint pushes the same way is discharged and its\n"
                        "atoms folded to constants, shrinking the Boolean structure. Models are\n"
                        "reconstructed for eliminated variables")
      ->option_text("[default: auto]");
  add_flag_with_default(app, "--no-monotone-elimination", no_monotone_elimination,
                        "Disable monotone real-variable elimination");
  add_flag_with_default(
      app, "--guarded-real-equality-lowering", guarded_real_equality_lowering,
      "Enable experimental guarded Real equality lowering: rewrite a disjunction "
      "with one Real equality child directly to two guarded inequalities, avoiding "
      "one Tseitin layer on some QF_LRA encodings");
  add_flag_with_default(
      app, "--arithmetic-atom-canonicalization", solver_options.arithmetic_atom_canonicalization,
      "Enable experimental primitive-integer canonicalization of proportional Real "
      "comparisons and equalities before clausification");
  app.add_option("--lia-branch-limit", solver_options.lia_branch_limit,
                 "QF_LIA branch-and-bound node cap: plain branch-and-bound alone has no "
                 "completeness argument (an unbounded integer-infeasible polytope can branch "
                 "forever), so once this many nodes have been explored the search reports "
                 "Unknown instead of continuing")
      ->check(CLI::Range(1, 100000000));
  app.add_option("--lia-gomory-cut-limit", solver_options.lia_gomory_cut_limit,
                 "QF_LIA Gomory-cut round cap, run before branch-and-bound on every candidate "
                 "model: caps how many cut-and-recheck rounds are attempted before falling "
                 "through to branch-and-bound regardless")
      ->check(CLI::Range(0, 100000000));
  app.add_option("--lia-patch-limit", solver_options.lia_patch_limit,
                 "QF_LIA patching budget, run before the integer refutation stages on every "
                 "candidate model: caps how many nonbasic moves are taken to nudge Int variables "
                 "onto integer values. 0 disables patching")
      ->check(CLI::Range(0, 100000000));
  add_flag_with_default(
      app, "--no-lia-conflict-generalization", no_lia_conflict_generalization,
      "Disable QF_LIA conflict generalization: build each integer-infeasibility lemma "
      "from every arithmetic atom in the problem instead of from the GCD/cut/branch "
      "derivation that actually proved it. The coarse form excludes one Boolean "
      "assignment per round");
  add_flag_with_default(app, "--top-level-or-tseitin", top_level_or_tseitin,
                        "Always encode top-level disjunctions with Tseitin support clauses");
  add_flag_with_default(
      app, "--no-top-level-or-tseitin", no_top_level_or_tseitin,
      "Never encode them: keep each top-level disjunction as one clause, and turn "
      "the automatic rule off too");
  add_flag_with_default(
      app, "--no-auto-top-level-or-tseitin", no_auto_top_level_or_tseitin,
      "Disable the automatic rule, which encodes Tseitin-style only on finite-domain "
      "problems having 1000-2500 top-level OR nodes and at most 700 negations");
  add_flag_with_default(
      app, "--preprocess-only", solver_options.preprocess_only,
      "Diagnostic: stop before search; report unsat if preprocessing refutes, otherwise unknown. "
      "Quantified input stops after the frontend");
  app.add_option("--cancel-grace-seconds", cancel_grace_seconds,
                 "Seconds SIGTERM or SIGINT waits for the cooperative cancellation path, "
                 "which reports statistics, before answering unknown and exiting")
      ->check(CLI::Range(1U, 3600U))
      ->capture_default_str();
  add_enum_option(app, "--preprocessing-mode", solver_options.preprocessing_mode,
                  primo::cli::kPreprocessingModeNames,
                  "Preprocessing preset for all supported logics");
  add_enum_option(app, "--incremental-mode", solver_options.incremental_mode,
                  primo::cli::kIncrementalModeNames,
                  "Session engine; assumptions is opt-in for QF_LRA and QF_UF");
  add_enum_option(app, "--auto-preprocess-selector", solver_options.auto_preprocess_selector,
                  primo::cli::kAutoPreprocessSelectorNames,
                  "Auto preprocessing selector: legacy assertion-count heuristics or "
                  "representation-stable structural analysis");
  app.add_option("--auto-preprocess-large-conjunct-limit",
                 solver_options.auto_preprocess_tuning.large_conjunct_limit,
                 "Maximum implicit top-level conjunct occurrences for repeated auto "
                 "preprocessing; 0 means unlimited")
      ->check(CLI::Range(0, 100000000));
  app.add_option("--auto-basic-compact-term-limit",
                 solver_options.auto_preprocess_tuning.compact_term_limit,
                 "Maximum reachable terms for compact-clause basic auto preprocessing; 0 "
                 "means unlimited")
      ->check(CLI::Range(0, 100000000));
  app.add_option("--auto-basic-compact-min-conjuncts",
                 solver_options.auto_preprocess_tuning.compact_min_conjuncts,
                 "Minimum implicit top-level conjunct occurrences for compact-clause basic "
                 "auto preprocessing")
      ->check(CLI::Range(1, 100000000));
  app.add_option("--auto-basic-single-term-limit",
                 solver_options.auto_preprocess_tuning.finite_domain_term_limit,
                 "Maximum reachable terms for finite-domain basic auto preprocessing; 0 "
                 "means unlimited")
      ->check(CLI::Range(0, 100000000));
  app.add_option("--auto-basic-single-min-domains",
                 solver_options.auto_preprocess_tuning.finite_domain_min_domains,
                 "Minimum recognized finite domains for finite-domain basic auto preprocessing")
      ->check(CLI::Range(1, 100000000));
  app.add_option("--auto-basic-single-max-symmetry-terms",
                 solver_options.auto_preprocess_tuning.finite_domain_max_symmetry_terms,
                 "Maximum terms in the accepted finite-domain symmetry group; 0 means unlimited")
      ->check(CLI::Range(0, 100000000));
  add_flag_with_default(app, "--no-auto-basic-single", no_auto_basic_single,
                        "Disable the guarded auto basic-single preprocessing selector");
  add_flag_with_default(
      app, "--no-auto-basic-single-predicate-polarity", no_auto_basic_single_predicate_polarity,
      "Disable polarity phase for auto basic-single formulas with Boolean UF predicates");
  add_flag_with_default(
      app, "--auto-basic-multi", auto_basic_multi,
      "Enable the experimental auto basic-multi preprocessing and phase heuristic");
  add_flag_with_default(app, "--no-auto-basic-multi", no_auto_basic_multi,
                        "Disable the auto basic-multi preprocessing and phase heuristic");
  add_flag_with_default(app, "--no-auto-basic-multi-polarity", no_auto_basic_multi_polarity,
                        "Disable polarity phase for auto basic-multi formulas");
  CLI::Option* profile_option = app.add_option("--qf-uf-profile", qf_uf_profile,
                                               "QF_UF profile: default or tuned-interesting")
                                    ->check(CLI::IsMember({"default", "tuned-interesting"}));
  add_enum_option(app, "--qf-uf-portfolio", solver_options.qf_uf_portfolio,
                  primo::cli::kQfUfPortfolioNames, "QF_UF portfolio mode");
  app.add_option("--qf-uf-portfolio-conflicts", solver_options.qf_uf_portfolio_conflicts,
                 "Theory-conflict budget for the first QF_UF portfolio attempt")
      ->check(CLI::Range(1, 1000000000));
  add_enum_option(app, "--euf-prop", solver_options.euf_propagation,
                  primo::cli::kEufPropagationNames, "EUF propagation mode");
  add_enum_option(app, "--euf-engine", solver_options.euf_engine, primo::cli::kEufEngineNames,
                  "EUF engine");
  add_enum_option(app, "--euf-explain-search", solver_options.euf_explain_search,
                  primo::cli::kEufExplainSearchNames, "Incremental EUF explanation search");
  add_flag_with_default(app, "--euf-proof-forest-diagnostics", euf_proof_forest_diagnostics,
                        "Record proof-forest path counters for incremental EUF explanations");
  add_flag_with_default(
      app, "--euf-diseq-prop-diagnostics", euf_diseq_propagation_diagnostics,
      "Record diagnostic-only QF_UF app-disequality propagation opportunity counters");
  add_flag_with_default(app, "--euf-ackermann-diagnostics", euf_ackermann_diagnostics,
                        "Record diagnostic-only QF_UF Ackermannization cost counters");
  add_flag_with_default(app, "--nelson-oppen-diagnostics", nelson_oppen_diagnostics,
                        "Record diagnostic-only Nelson-Oppen interface counters");
  add_flag_with_default(
      app, "--model-equality-branching", nelson_oppen_model_equality_branching,
      "Enable experimental QF_UFLRA model-equality branching over shared Real terms; "
      "not a full Nelson-Oppen model-equality implementation");
  add_flag_with_default(
      app, "--no-model-equality-mutation", no_nelson_oppen_model_equality_mutation,
      "Disable the Section 5.2 greedy LRA model-diversification sweep used by the "
      "experimental QF_UFLRA shared-model and model-equality branching paths");
  add_enum_option(app, "--model-equality-mutation-scope",
                  solver_options.nelson_oppen_model_equality_mutation_scope,
                  primo::cli::kModelEqualityMutationScopeNames,
                  "QF_UFLRA LRA model-mutation scope: all shared Real terms or current EUF "
                  "care-pair endpoints");
  add_enum_option(app, "--nelson-oppen-prop", solver_options.nelson_oppen_propagation,
                  primo::cli::kNelsonOppenPropagationNames,
                  "QF_UFLRA Nelson-Oppen interface equality propagation mode. Exact keeps the "
                  "mandatory convex closure but disables opportunistic LRA producers; off is a "
                  "backward-compatible alias for exact. Pure QF_UF and QF_LRA keep their direct "
                  "theory propagators");
  app.add_option("--model-equality-branch-budget",
                 solver_options.nelson_oppen_model_equality_branch_budget,
                 "Maximum QF_UFLRA model-equality branch assumptions per check-sat")
      ->check(CLI::Range(0, 1000000));
  add_enum_option(app, "--model-equality-branch-policy",
                  solver_options.nelson_oppen_model_equality_branch_policy,
                  primo::cli::kModelEqualityBranchPolicyNames,
                  "QF_UFLRA model-equality branch policy");
  CLI::Option* no_arrangement_probe_limit_option =
      app.add_option("--nelson-oppen-arrangement-probe-limit", nelson_oppen_arrangement_probe_limit,
                     "Return unknown once mixed QF_UFLRA final equality sharing or "
                     "arrangement/model reconstruction has used this many temporary LRA "
                     "consistency probes")
          ->check(CLI::Range(1, 1000000000))
          ->default_str("unlimited");
  add_flag_with_default(
      app, "--euf-dynamic-ackermann-shadow", euf_dynamic_ackermann_shadow,
      "Shadow-evaluate SAT-visible dynamic Ackermann clauses without adding them");
  add_flag_with_default(app, "--euf-diseq-ackermann", euf_diseq_ackermann,
                        "Enable watched QF_UF disequality Ackermann propagation");
  app.add_option("--euf-diseq-ackermann-max-width", solver_options.euf_diseq_ackermann_max_width,
                 "Maximum width of watched disequality Ackermann clauses")
      ->check(CLI::Range(2, 1000));
  app.add_option(
         "--euf-diseq-ackermann-queue-budget", solver_options.euf_diseq_ackermann_queue_budget,
         "Maximum watched disequality Ackermann candidates to inspect per propagation round")
      ->check(CLI::Range(0, 1000000000));
  app.add_option("--euf-diseq-ackermann-per-symbol", solver_options.euf_diseq_ackermann_per_symbol,
                 "Maximum watched disequality Ackermann candidates per function symbol")
      ->check(CLI::Range(0, 1000000000));
  add_enum_option(app, "--pure-theory-filter", solver_options.pure_theory_filter,
                  primo::cli::kPureTheoryFilterNames, "T-pure theory literal filtering mode");
  CLI::Option* theory_phase_option =
      add_enum_option(app, "--theory-phase", solver_options.theory_phase,
                      primo::cli::kTheoryPhaseNames, "Initial SAT phase for theory atoms");
  app.add_option("--euf-prop-budget", euf_prop_budget,
                 "EUF propagation candidate budget: auto, unlimited, or a nonnegative integer");
  app.add_option("--euf-prop-delivery,--prop-delivery-budget", euf_prop_delivery,
                 "EUF propagation delivery budget: auto, unlimited, or a nonnegative integer; 0 "
                 "means unlimited");
  CLI::Option* max_theory_conflicts_option =
      app.add_option("--max-theory-conflicts", max_theory_conflicts,
                     "Return unknown after at least this many learned theory conflicts")
          ->check(CLI::Range(1, 1000000000))
          ->default_str("unlimited");
  app.add_option_function<std::string>(
         "--cadical-option",
         [&cadical_options](const std::string& option) { cadical_options.push_back(option); },
         "Set a CaDiCaL integer option as name=value; may be repeated")
      ->multi_option_policy(CLI::MultiOptionPolicy::TakeAll);
  CLI::Option* preprocess_passes_option =
      app.add_option("--preprocessing-passes,--preprocess-passes",
                     solver_options.preprocessing_passes,
                     "Number of shared preprocessing passes to run")
          ->check(CLI::Range(0, 100));
  app.add_option("--boolean-flatten-threshold", solver_options.boolean_flatten_threshold,
                 "Flatten nested and/or nodes during preprocessing up to this arity; 0 disables")
      ->check(CLI::Range(0, 1000000));
  app.add_option("--boolean-flatten-post-threshold", solver_options.boolean_flatten_post_threshold,
                 "Apply the same bounded And/Or flattening again AFTER the rewriting "
                 "preprocessing passes, up to this arity; 0 disables. Those passes build "
                 "fresh nodes through TermManager, which no longer flattens at construction, "
                 "so they reintroduce nesting that --boolean-flatten-threshold (which runs "
                 "before them) cannot see. 0 disables")
      ->check(CLI::Range(0, 1000000));
  add_enum_option(app, "--boolean-implies-rewrite", solver_options.boolean_implies_rewrite,
                  primo::cli::kBooleanImpliesRewriteNames,
                  "Post-preprocessing implication rewrite: off, clause (only expose flat "
                  "clauses), or all (rewrite A=>B to (or (not A) B) without distribution)");
  app.add_option("--preprocessing-adaptive-extra-passes",
                 solver_options.preprocessing_adaptive_extra_passes,
                 "Additional adaptive preprocessing passes after the configured pass cap")
      ->check(CLI::Range(0, 100));
  app.add_option("--preprocessing-adaptive-shrink-percent",
                 solver_options.preprocessing_adaptive_shrink_percent,
                 "Minimum reachable-term shrink percentage required for another adaptive pass")
      ->check(CLI::Range(0, 100));
  app.add_option(
         "--finite-domain-eqdefs-auto-domain-limit",
         solver_options.finite_domain_eqdefs_auto_domain_limit,
         "Maximum finite-domain value count for auto equality definitions; 0 means unlimited")
      ->check(CLI::Range(0, 1000000));
  app.add_option("--finite-domain-eqdefs-auto-clause-limit",
                 solver_options.finite_domain_eqdefs_auto_clause_limit,
                 "Maximum generated equality-definition clauses per check-sat in auto; 0 "
                 "means unlimited")
      ->check(CLI::Range(0, 100000000));
  app.add_option("--finite-domain-eqdefs-auto-term-limit",
                 solver_options.finite_domain_eqdefs_auto_term_limit,
                 "Maximum initial reachable-term count for auto equality definitions on "
                 "single-assertion formulas; 0 means unlimited")
      ->check(CLI::Range(0, 100000000));
  CLI::Option* prop_interval_option =
      app.add_option("--prop-interval", solver_options.euf_propagation_interval,
                     "Online EUF propagation discovery interval")
          ->check(CLI::Range(1, 1000000));
  CLI::Option* prop_assign_threshold_option =
      app.add_option("--prop-assign-threshold", solver_options.euf_propagation_assign_threshold,
                     "Skip online EUF propagation discovery above this assigned-atom fraction")
          ->check(CLI::Range(0.0, 1.0));
  app.add_option("--quantifier-instances-per-round", solver_options.quantifier_instances_per_round,
                 "Quantified UF: enumerative instances queued per complete assignment")
      ->check(CLI::Range(1, 1000000000));
  app.add_option("--quantifier-stall-tuples", solver_options.quantifier_stall_tuples,
                 "Quantified UF: tuples a quantifier may visit without a new lemma before a "
                 "round that has queued lemmas hands them to SAT; 0 never yields")
      ->check(CLI::Range(0, 1000000000));
  app.add_option("--quantifier-max-instances", solver_options.quantifier_max_instances,
                 "Quantified UF: distinct instances generated before answering unknown")
      ->check(CLI::Range(1, 1000000000));
  add_enum_option(app, "--quantifier-tuple-order", solver_options.quantifier_tuple_order,
                  primo::cli::kTupleOrderNames, "Quantified UF: tuple enumeration order");
  app.add_option("--quantifier-deepening-increment", solver_options.quantifier_deepening_increment,
                 "Quantified UF: maximum-depth increment of the deepening tuple order")
      ->check(CLI::Range(1, 1000000));
  app.add_option("--quantifier-random-seed", solver_options.quantifier_random_seed,
                 "Quantified UF: seed of the random tuple order");
  app.add_option("--quantifier-export-pools", solver_options.quantifier_export_pools,
                 "Quantified UF: write the instantiation tuples and Skolem lemmas as SMT-LIB "
                 "pools to FILE, for scripts/quantifier_pool_check.py")
      ->type_name("FILE")
      ->default_str("");
  app.add_option("files", files, "SMT-LIB input files. Reads stdin when omitted")
      ->type_name("FILE")
      ->default_str("");
  app.set_version_flag("--version", version_text());

  // Sort the 60-odd options into sections for --help. Done here, by name,
  // rather than by threading a group into every add_flag/add_option call:
  // one table to read instead of sixty scattered ->group() calls, and a new
  // option lands in the right section by virtue of being named consistently
  // with the ones it belongs beside.
  //
  // The split follows what each option acts *on*, which its own help text
  // already states: theory solver (QF_LRA / QF_UF), preprocessing, the SAT
  // layer and DPLL(T) boundary, or the run itself. Only the placements that
  // are not obvious from the prefix are listed explicitly below; anything
  // left unmatched lands in "Other options", which is a bug rather than a
  // category -- `primo --help` should never print that heading.
  const auto starts_with = [](const std::string& text, std::string_view prefix) {
    return text.rfind(prefix, 0) == 0;
  };
  const auto group_for = [&starts_with](const std::string& name) -> std::string {
    if (starts_with(name, "--lra-") || starts_with(name, "--no-lra-") ||
        name == "--gaussian-elimination" || name == "--no-gaussian-elimination" ||
        name == "--qf-uflra-candidate-final-scan" || name == "--qf-uflra-lra-equality-producer" ||
        name == "--qf-uflra-model-reconciliation" || name == "--no-qf-uflra-model-reconciliation" ||
        name == "--no-qf-uflra-fixed-elimination" || name == "--guarded-real-equality-lowering" ||
        name == "--monotone-elimination" || name == "--no-monotone-elimination" ||
        name == "--lra-least-violated-leaving" || name == "--mixed-dispatch") {
      return kGroupLra;
    }
    if (starts_with(name, "--lia-") || starts_with(name, "--no-lia-") ||
        starts_with(name, "--qf-uflia-") || starts_with(name, "--no-qf-uflia-")) {
      return kGroupLia;
    }
    if (name == "--incremental-mode" || name == "--preprocessing-mode" ||
        name == "--arithmetic-atom-canonicalization" || name == "--auto-preprocess-selector" ||
        name == "--boolean-flatten-threshold" || name == "--boolean-flatten-post-threshold" ||
        name == "--boolean-implies-rewrite" || starts_with(name, "--auto-preprocess-") ||
        starts_with(name, "--preprocessing-") || starts_with(name, "--preprocess-passes") ||
        starts_with(name, "--extended-simplification") ||
        starts_with(name, "--no-extended-simplification") ||
        starts_with(name, "--cite-simplification") ||
        starts_with(name, "--no-cite-simplification") || name == "--cite-simplification" ||
        name == "--ite-equality-distribution" || name == "--no-ite-equality-distribution") {
      return kGroupPreprocessing;
    }
    // The SAT layer proper, plus the knobs that sit on the DPLL(T)
    // boundary: initial phase for theory atoms, T-pure filtering, the
    // theory-conflict budget, and how top-level disjunctions get clausified.
    if (name == "--cadical-option" || name == "--max-theory-conflicts" ||
        name == "--qf-uflra-online-euf" || name == "--pure-theory-filter" ||
        name == "--theory-phase" || starts_with(name, "--top-level-or-tseitin") ||
        starts_with(name, "--no-top-level-or-tseitin") ||
        starts_with(name, "--no-auto-top-level-or-tseitin")) {
      return kGroupSat;
    }
    // How the run behaves rather than how the solver reasons. `--help` is
    // CLI11's own built-in flag and would otherwise be the one option left
    // unclassified.
    if (name == "-q" || name == "--quiet" || name == "--stats" || name == "--version" ||
        name == "--help" || name == "--preprocess-only" || name == "--cancel-grace-seconds" ||
        name == "--check-model" || starts_with(name, "--export-cnf") ||
        name == "--experimental-export-erase-sorts" || name == "files") {
      return kGroupProcedural;
    }
    if (starts_with(name, "--experimental-")) {
      return kGroupExperimentalTactics;
    }
    // Everything else is QF_UF/EUF: the euf-*, finite-domain-*, ground-*,
    // qf-uf-* and auto-basic-* families, plus eq-bridge, symmetry breaking,
    // solved-constant substitution, the online-propagation knobs, and
    // quantified UF instantiation.
    if (starts_with(name, "--euf-") || starts_with(name, "--quantifier-") ||
        starts_with(name, "--no-quantifier-") || starts_with(name, "--no-euf-") ||
        starts_with(name, "--finite-domain-") || starts_with(name, "--no-finite-domain-") ||
        starts_with(name, "--ground-") || starts_with(name, "--guarded-ground-") ||
        starts_with(name, "--model-equality-") || starts_with(name, "--no-model-equality-") ||
        starts_with(name, "--nelson-oppen-") || starts_with(name, "--qf-uf-") ||
        starts_with(name, "--auto-basic-") || starts_with(name, "--no-auto-basic-") ||
        name == "--eq-bridge" || name == "--no-eq-bridge" || name == "--uf-symmetry-breaking" ||
        name == "--no-uf-symmetry-breaking" || name == "--no-solved-constant-substitution" ||
        name == "--prop-interval" || name == "--prop-assign-threshold" ||
        name == "--prop-delivery-budget") {
      return kGroupUf;
    }
    return "Other options";
  };
  for (CLI::Option* option : app.get_options()) {
    // get_name() gives the canonical (longest) name; aliases follow the
    // same family, so classifying on it is enough.
    std::string group = group_for(option->get_name());
    if (group == "Other options") {
      // Self-policing: whoever adds an option the table above does not
      // cover finds out immediately, rather than by noticing a stray
      // heading in --help months later. Costs one string compare per
      // option, once per process.
      std::cerr << "warning: option " << option->get_name()
                << " has no --help section; add it to group_for() in main.cpp\n";
    }
    option->group(std::move(group));
  }

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& error) {
    return app.exit(error);
  }

  if (qf_uflra_online_euf && nelson_oppen_model_equality_branching) {
    std::cerr << "error: --qf-uflra-online-euf and --model-equality-branching cannot be used "
                 "together (untested interaction between online interface-equality "
                 "backtracking and shared-equality-decision state)\n";
    return 2;
  }
  if (profile_option->count() > 0 && qf_uf_profile == "tuned-interesting") {
    solver_options.uf_symmetry_breaking = true;
    solver_options.eq_bridge = true;
    if (preprocess_passes_option->count() == 0) {
      solver_options.preprocessing_passes = 4;
    }
    if (prop_interval_option->count() == 0) {
      solver_options.euf_propagation_interval = 32;
    }
    if (prop_assign_threshold_option->count() == 0) {
      solver_options.euf_propagation_assign_threshold = 0.25;
    }
    if (euf_prop_delivery == "auto") {
      euf_prop_delivery = "1000";
    }
  }
  if (no_uf_symmetry_breaking) {
    solver_options.uf_symmetry_breaking = false;
  }
  if (no_finite_domain_value_precedence) {
    solver_options.finite_domain_value_precedence = false;
  }
  if (finite_domain_equality_definitions) {
    solver_options.auto_finite_domain_equality_definitions = true;
    solver_options.finite_domain_equality_definitions = true;
  }
  if (no_finite_domain_equality_definitions) {
    solver_options.finite_domain_equality_definitions = false;
    solver_options.auto_finite_domain_equality_definitions = false;
  }
  if (no_finite_domain_post_amo) {
    solver_options.finite_domain_post_amo = false;
  }
  if (no_extended_simplification) {
    solver_options.extended_simplification = false;
  }
  if (no_ite_equality_distribution) {
    solver_options.ite_equality_distribution = false;
  }
  if (no_cite_simplification) {
    solver_options.cite_simplification = false;
  }
  if (no_eq_bridge) {
    solver_options.eq_bridge = false;
  }
  if (no_quantifier_instance_simplification) {
    solver_options.quantifier_instance_simplification = false;
  }
  if (no_lra_propagation) {
    solver_options.lra_propagation = false;
  }
  if (no_solved_constant_substitution) {
    solver_options.solved_constant_substitution = false;
  }
  if (no_gaussian_elimination) {
    solver_options.gaussian_elimination = false;
  }
  if (no_lia_conflict_generalization) {
    solver_options.lia_conflict_generalization = false;
  }
  if (no_qf_uflra_fixed_elimination) {
    solver_options.qf_uflra_fixed_elimination = false;
  }
  solver_options.qf_uflra_candidate_final_scan = qf_uflra_candidate_final_scan;
  if (qf_uflra_model_reconciliation) {
    solver_options.qf_uflra_model_reconciliation = true;
  }
  if (no_qf_uflra_model_reconciliation) {
    solver_options.qf_uflra_model_reconciliation = false;
  }
  solver_options.experimental_generic_model_combination = experimental_generic_model_combination;
  solver_options.qf_uflia_model_combination =
      qf_uflia_model_combination && !no_qf_uflia_model_combination;
  solver_options.qf_uflia_online_components =
      qf_uflia_online_components && !no_qf_uflia_online_components;
  solver_options.qf_uflia_model_decision_budget = qf_uflia_model_decision_budget;
  solver_options.qf_uflra_online_euf = qf_uflra_online_euf;
  if (app.get_option("--lra-model-phase")->count() != 0) {
    solver_options.lra_model_phase = lra_model_phase;
  }
  if (no_lra_model_phase) {
    solver_options.lra_model_phase = false;
  }
  if (app.get_option("--monotone-elimination")->count() != 0) {
    solver_options.monotone_elimination = monotone_elimination;
  }
  if (no_monotone_elimination) {
    solver_options.monotone_elimination = false;
  }
  if (guarded_real_equality_lowering) {
    solver_options.guarded_real_equality_lowering = true;
  }
  if (top_level_or_tseitin) {
    solver_options.top_level_or_tseitin = true;
  }
  if (no_top_level_or_tseitin) {
    solver_options.top_level_or_tseitin = false;
    solver_options.auto_top_level_or_tseitin = false;
  }
  if (no_auto_top_level_or_tseitin) {
    solver_options.auto_top_level_or_tseitin = false;
  }
  if (no_auto_basic_single) {
    solver_options.auto_basic_single_preprocessing = false;
    solver_options.auto_basic_single_predicate_polarity = false;
  }
  if (no_auto_basic_single_predicate_polarity) {
    solver_options.auto_basic_single_predicate_polarity = false;
  }
  if (auto_basic_multi) {
    solver_options.auto_basic_multi_preprocessing = true;
    solver_options.auto_basic_multi_polarity = true;
  }
  if (no_auto_basic_multi) {
    solver_options.auto_basic_multi_preprocessing = false;
    solver_options.auto_basic_multi_polarity = false;
  }
  if (no_auto_basic_multi_polarity) {
    solver_options.auto_basic_multi_polarity = false;
  }
  solver_options.euf_proof_forest_diagnostics = euf_proof_forest_diagnostics;
  if (lra_least_violated_leaving) {
    solver_options.lra_least_violated_leaving = true;
  }
  if (no_lra_sparse_leaving) {
    solver_options.lra_sparse_leaving = false;
  }
  solver_options.euf_diseq_propagation_diagnostics = euf_diseq_propagation_diagnostics;
  solver_options.euf_ackermann_diagnostics = euf_ackermann_diagnostics;
  solver_options.nelson_oppen_diagnostics = nelson_oppen_diagnostics;
  solver_options.nelson_oppen_model_equality_branching = nelson_oppen_model_equality_branching;
  if (no_nelson_oppen_model_equality_mutation) {
    solver_options.nelson_oppen_model_equality_mutation = false;
  }
  solver_options.euf_dynamic_ackermann_shadow = euf_dynamic_ackermann_shadow;
  solver_options.euf_diseq_ackermann = euf_diseq_ackermann;
  if (theory_phase_option->count() > 0) {
    solver_options.auto_basic_single_predicate_polarity = false;
    solver_options.auto_basic_multi_polarity = false;
  }
  const std::optional<primo::smt::AutoBudget> parsed_prop_budget =
      parse_auto_budget(euf_prop_budget);
  const std::optional<primo::smt::AutoBudget> parsed_prop_delivery =
      parse_auto_budget(euf_prop_delivery);
  if (!parsed_prop_budget.has_value()) {
    std::cerr << "error: invalid --euf-prop-budget value: " << euf_prop_budget << "\n";
    return 1;
  }
  if (!parsed_prop_delivery.has_value()) {
    std::cerr << "error: invalid --euf-prop-delivery value: " << euf_prop_delivery << "\n";
    return 1;
  }
  solver_options.euf_propagation_budget = parsed_prop_budget.value();
  solver_options.euf_propagation_delivery = parsed_prop_delivery.value();
  if (max_theory_conflicts_option->count() > 0) {
    solver_options.max_theory_conflicts = max_theory_conflicts;
  }
  if (no_arrangement_probe_limit_option->count() > 0) {
    solver_options.nelson_oppen_arrangement_probe_limit = nelson_oppen_arrangement_probe_limit;
  }
  for (const std::string& option : cadical_options) {
    const std::optional<std::pair<std::string, int>> parsed = parse_cadical_option(option);
    if (!parsed.has_value()) {
      std::cerr << "error: invalid --cadical-option value: " << option << "\n";
      return 1;
    }
    solver_options.cadical_options.push_back(parsed.value());
  }

  if (app.get_option("--export-cnf")->count() != 0 && solver_options.export_cnf.empty()) {
    throw std::runtime_error("--export-cnf requires a nonempty output filename");
  }
  if (!solver_options.export_cnf.empty()) {
    if (files.size() > 1) throw std::runtime_error("--export-cnf accepts at most one input file");
    if (!files.empty() && std::filesystem::exists(solver_options.export_cnf) &&
        std::filesystem::equivalent(files.front(), solver_options.export_cnf)) {
      throw std::runtime_error("CNF output must differ from the input file");
    }
  }
  if (!quiet) {
    print_startup_banner();
  }
  g_cancel_grace_seconds = static_cast<std::sig_atomic_t>(cancel_grace_seconds);

  if (files.empty()) {
    return execute_stream(std::cin, solver_options, print_stats);
  }

  int exit_code = 0;
  for (const auto& file : files) {
    std::ifstream input(file);
    if (!input) {
      std::cerr << "error: cannot read input file: " << file << "\n";
      return 1;
    }
    const int stream_exit_code = execute_stream(input, solver_options, print_stats);
    if (stream_exit_code != 0) exit_code = stream_exit_code;
    if (g_cancel_requested != 0) {
      // The answer for this file is out; abandon the rest of the batch rather
      // than letting the armed watchdog fire partway through the next one.
      break;
    }
  }
  return exit_code;
}

int main(int argc, char** argv) {
  try {
    return run_main(argc, argv);
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << "\n";
    return 1;
  }
}
