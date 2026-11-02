#include "smt/check_sat.hpp"

#include <exception>
#include <string_view>
#include <utility>

#include "smt/qf_uf_export.hpp"
#include "smt/session_portfolio.hpp"
#include "smt/smt_session.hpp"
#include "smtlib/parser.hpp"

namespace primo::smt {

CheckSatOutcome check_sat_smtlib(std::string_view input, CheckSatOptions options) {
  if (!options.export_cnf.empty()) return export_qf_uf_cnf(input, std::move(options));
  if (options.incremental_mode == IncrementalMode::Assumptions) {
    SmtSession session(options);
    CheckSatOutcome outcome;
    try {
      for (auto& command : smtlib::Parser(input).parse_commands()) {
        const auto response = session.execute(std::move(command));
        if (response.kind == ResponseKind::Error || response.kind == ResponseKind::Unsupported) {
          outcome.result = CheckSatResult::Unsupported;
          outcome.message = response.message;
          return outcome;
        }
        if (response.kind == ResponseKind::CheckSat) outcome = *response.outcome;
        if (response.kind == ResponseKind::Exit) break;
      }
    } catch (const std::exception& error) {
      outcome.result = CheckSatResult::Unsupported;
      outcome.message = error.what();
    }
    return outcome;
  }
  SequentialSessionExecutor executor;
  if (options.qf_uf_portfolio == QfUfPortfolioMode::Off) {
    return executor.execute(input, std::move(options));
  }
  return execute_qf_uf_session_portfolio(input, std::move(options), executor);
}

std::string_view to_string(CheckSatResult result) {
  switch (result) {
    case CheckSatResult::Sat:
      return "sat";
    case CheckSatResult::Unsat:
      return "unsat";
    case CheckSatResult::Unknown:
      return "unknown";
    case CheckSatResult::Unsupported:
      return "unsupported";
  }
  return "unknown";
}

}  // namespace primo::smt
