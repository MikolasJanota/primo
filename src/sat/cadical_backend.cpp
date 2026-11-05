#include "sat/cadical_backend.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if PRIMO_HAVE_IPASIR
#include <cadical.hpp>
#endif

namespace primo::sat {

namespace {

#if PRIMO_HAVE_IPASIR
std::size_t nonnegative_statistic(CaDiCaL::Solver* solver, const char* name) {
  if (solver == nullptr) {
    return 0;
  }
  const std::int64_t value = solver->get_statistic_value(name);
  return value < 0 ? 0 : static_cast<std::size_t>(value);
}
#endif

}  // namespace

#if PRIMO_HAVE_IPASIR
class CadicalBackend::ExternalPropagatorAdapter final : public CaDiCaL::ExternalPropagator {
 public:
  explicit ExternalPropagatorAdapter(::primo::sat::ExternalPropagator& propagator)
      : propagator_(propagator) {
    scalar_assignment_buffer_.reserve(1);
  }

#if PRIMO_CADICAL_EXTERNAL_NOTIFY_VECTOR
  void notify_assignment(const std::vector<int>& lits) override {
    propagator_.notify_assignment(lits);
  }
#else
  void notify_assignment(int lit, bool /*is_fixed*/) override {
    scalar_assignment_buffer_.clear();
    scalar_assignment_buffer_.push_back(lit);
    propagator_.notify_assignment(scalar_assignment_buffer_);
  }
#endif

  void notify_new_decision_level() override { propagator_.notify_new_decision_level(); }

  void notify_backtrack(size_t new_level) override { propagator_.notify_backtrack(new_level); }

  bool cb_check_found_model(const std::vector<int>& model) override {
    return propagator_.cb_check_found_model(model);
  }

  int cb_decide() override { return propagator_.cb_decide(); }

  int cb_propagate() override { return propagator_.cb_propagate(); }

  int cb_add_reason_clause_lit(int propagated_lit) override {
    return propagator_.cb_add_reason_clause_lit(propagated_lit);
  }

#if PRIMO_CADICAL_EXTERNAL_NOTIFY_VECTOR
  bool cb_has_external_clause(bool& is_forgettable) override {
    return propagator_.cb_has_external_clause(is_forgettable);
  }
#else
  bool cb_has_external_clause() override { return propagator_.cb_has_external_clause(); }
#endif

  int cb_add_external_clause_lit() override { return propagator_.cb_add_external_clause_lit(); }

 private:
  ::primo::sat::ExternalPropagator& propagator_;
  std::vector<int> scalar_assignment_buffer_;
};

class CadicalBackend::TerminatorAdapter final : public CaDiCaL::Terminator {
 public:
  explicit TerminatorAdapter(::primo::sat::Terminator& terminator) : terminator_(terminator) {}

  bool terminate() override { return terminator_.terminate(); }

 private:
  ::primo::sat::Terminator& terminator_;
};
#else
class CadicalBackend::ExternalPropagatorAdapter {};
class CadicalBackend::TerminatorAdapter {};
#endif

std::string_view cadical_signature() {
#if PRIMO_HAVE_IPASIR
  return CaDiCaL::Solver::signature();
#else
  return "unavailable";
#endif
}

CadicalBackend::CadicalBackend() : CadicalBackend(std::vector<std::pair<std::string, int>>{}) {}

CadicalBackend::CadicalBackend(const std::vector<std::pair<std::string, int>>& options)
    : CadicalBackend(options, false) {}

CadicalBackend::CadicalBackend(const std::vector<std::pair<std::string, int>>& options,
                               bool incremental_safe) {
#if PRIMO_HAVE_IPASIR
  std::unique_ptr<CaDiCaL::Solver> solver(new CaDiCaL::Solver());
  if (solver == nullptr) {
    throw std::runtime_error("CaDiCaL solver allocation failed");
  }
  for (const auto& [name, value] : options) {
    if (!solver->set(name.c_str(), value)) {
      throw std::invalid_argument("invalid CaDiCaL option: " + name);
    }
  }
  if (incremental_safe) {
    if (!options.empty())
      throw std::invalid_argument("assumptions mode rejects custom CaDiCaL options");
    if (!solver->configure("plain") || !solver->set("inprocessing", 0))
      throw std::runtime_error("CaDiCaL lacks the incremental-safe configuration");
  }
  solver_ = solver.release();
#else
  (void)options;
  (void)incremental_safe;
#endif
}

void CadicalBackend::protect_variable(int variable) {
#if PRIMO_HAVE_IPASIR
  solver_->freeze(variable);
#else
  (void)variable;
#endif
}

CadicalBackend::~CadicalBackend() {
#if PRIMO_HAVE_IPASIR
  if (solver_ != nullptr) {
    delete solver_;
  }
#endif
}

int CadicalBackend::new_var() {
#if PRIMO_HAVE_IPASIR
#if PRIMO_CADICAL_HAS_DECLARE_MORE_VARIABLES
  max_var_ = solver_->declare_more_variables(1);
#elif PRIMO_CADICAL_HAS_RESIZE
  ++max_var_;
  solver_->resize(max_var_);
#elif PRIMO_CADICAL_HAS_RESERVE
  ++max_var_;
  solver_->reserve(max_var_);
#else
  ++max_var_;
#endif
#else
  ++max_var_;
#endif
  statistics_.variables = static_cast<std::size_t>(max_var_);
  return max_var_;
}

int CadicalBackend::new_observed_var() {
#if PRIMO_HAVE_IPASIR
  if (solver_ == nullptr) {
    throw std::runtime_error("IPASIR solver is not initialized");
  }
  const int next_var = std::max(max_var_, solver_->vars()) + 1;
  solver_->add_observed_var(next_var);
  max_var_ = std::max(next_var, solver_->vars());
#else
  ++max_var_;
  const int next_var = max_var_;
#endif
  statistics_.variables = static_cast<std::size_t>(max_var_);
  return next_var;
}

void CadicalBackend::add_clause(const Clause& clause) {
  ++statistics_.clauses;
  statistics_.clause_literals += clause.size();
  if (clause.size() == 1) {
    ++statistics_.unit_clauses;
  } else if (clause.size() == 2) {
    ++statistics_.binary_clauses;
  }
#if PRIMO_HAVE_IPASIR
  if (solver_ == nullptr) {
    throw std::runtime_error("IPASIR solver is not initialized");
  }
  for (const Lit lit : clause) {
    if (lit.value == 0) {
      throw std::invalid_argument("literal 0 is reserved as IPASIR clause terminator");
    }
    solver_->add(lit.value);
  }
  solver_->add(0);
#else
  (void)clause;
#endif
}

void CadicalBackend::assume(Lit lit) {
#if PRIMO_HAVE_IPASIR
  if (lit.value == 0) {
    throw std::invalid_argument("assumption literal must be non-zero");
  }
  solver_->assume(lit.value);
#else
  (void)lit;
#endif
}

SatResult CadicalBackend::solve() {
#if PRIMO_HAVE_IPASIR
  const int result = solver_->solve();
  if (result == 10) {
    last_result_ = SatResult::Sat;
  } else if (result == 20) {
    last_result_ = SatResult::Unsat;
  } else {
    last_result_ = SatResult::Unknown;
  }
  return last_result_;
#else
  last_result_ = SatResult::Unsupported;
  return last_result_;
#endif
}

std::optional<bool> CadicalBackend::value(int var) const {
#if PRIMO_HAVE_IPASIR
  if (last_result_ != SatResult::Sat) {
    return std::nullopt;
  }
  if (var <= 0) {
    throw std::invalid_argument("SAT variable must be positive");
  }
  const int result = solver_->val(var);
  if (result == var) {
    return true;
  }
  if (result == -var) {
    return false;
  }
  return std::nullopt;
#else
  (void)var;
  return std::nullopt;
#endif
}

bool CadicalBackend::failed(Lit lit) const {
#if PRIMO_HAVE_IPASIR
  if (last_result_ != SatResult::Unsat) {
    return false;
  }
  if (lit.value == 0) {
    throw std::invalid_argument("failed literal must be non-zero");
  }
  return solver_->failed(lit.value);
#else
  (void)lit;
  return false;
#endif
}

BackendStatistics CadicalBackend::statistics() const {
  BackendStatistics stats = statistics_;
#if PRIMO_HAVE_IPASIR
  stats.conflicts = nonnegative_statistic(solver_, "conflicts");
  stats.decisions = nonnegative_statistic(solver_, "decisions");
  stats.propagations = nonnegative_statistic(solver_, "propagations");
  stats.active_irredundant_clauses = static_cast<std::size_t>(solver_->irredundant());
  stats.active_redundant_clauses = static_cast<std::size_t>(solver_->redundant());
#endif
  return stats;
}

IpasirUpCapabilities CadicalBackend::capabilities() const {
  IpasirUpCapabilities capabilities;
#if PRIMO_HAVE_IPASIR
  capabilities.external_propagation = true;
  capabilities.external_clauses = true;
  capabilities.external_decisions = true;
  capabilities.observed_variables = true;
  capabilities.fresh_observed_variables_during_solving = true;
#endif
  return capabilities;
}

void CadicalBackend::phase(Lit lit) {
#if PRIMO_HAVE_IPASIR
  if (lit.value == 0) {
    throw std::invalid_argument("phase literal must be non-zero");
  }
  solver_->phase(lit.value);
#else
  (void)lit;
#endif
}

void CadicalBackend::connect_external_propagator(ExternalPropagator* propagator) {
#if PRIMO_HAVE_IPASIR
  if (propagator == nullptr) {
    solver_->disconnect_external_propagator();
    external_adapter_.reset();
    return;
  }
  external_adapter_ = std::make_unique<ExternalPropagatorAdapter>(*propagator);
  solver_->connect_external_propagator(external_adapter_.get());
#else
  (void)propagator;
#endif
}

void CadicalBackend::disconnect_external_propagator() {
#if PRIMO_HAVE_IPASIR
  if (!external_adapter_) return;
  solver_->disconnect_external_propagator();
  external_adapter_.reset();
#endif
}

void CadicalBackend::connect_terminator(Terminator* terminator) {
#if PRIMO_HAVE_IPASIR
  if (terminator == nullptr) {
    solver_->disconnect_terminator();
    terminator_adapter_.reset();
    return;
  }
  terminator_adapter_ = std::make_unique<TerminatorAdapter>(*terminator);
  solver_->connect_terminator(terminator_adapter_.get());
#else
  (void)terminator;
#endif
}

void CadicalBackend::disconnect_terminator() {
#if PRIMO_HAVE_IPASIR
  solver_->disconnect_terminator();
  terminator_adapter_.reset();
#endif
}

void CadicalBackend::add_observed_var(int var) {
#if PRIMO_HAVE_IPASIR
  solver_->add_observed_var(var);
#else
  (void)var;
#endif
}

}  // namespace primo::sat
