#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "sat/ipasir_up.hpp"

namespace CaDiCaL {
class Solver;
}  // namespace CaDiCaL

namespace primo::sat {

std::string_view cadical_signature();

class CadicalBackend final : public IpasirUpBackend {
 public:
  CadicalBackend();
  explicit CadicalBackend(const std::vector<std::pair<std::string, int>>& options);
  CadicalBackend(const std::vector<std::pair<std::string, int>>& options, bool incremental_safe);
  ~CadicalBackend() override;

  CadicalBackend(const CadicalBackend&) = delete;
  CadicalBackend& operator=(const CadicalBackend&) = delete;

  int new_var() override;
  int new_observed_var() override;
  void add_clause(const Clause& clause) override;
  void assume(Lit lit) override;
  SatResult solve() override;
  std::optional<bool> value(int var) const override;
  bool failed(Lit lit) const override;
  BackendStatistics statistics() const override;
  IpasirUpCapabilities capabilities() const override;
  void phase(Lit lit) override;
  void connect_external_propagator(ExternalPropagator* propagator) override;
  void disconnect_external_propagator() override;
  void add_observed_var(int var) override;
  void connect_terminator(Terminator* terminator) override;
  void disconnect_terminator() override;
  void protect_variable(int variable);

 private:
  class ExternalPropagatorAdapter;
  class TerminatorAdapter;

  CaDiCaL::Solver* solver_ = nullptr;
  std::unique_ptr<ExternalPropagatorAdapter> external_adapter_;
  std::unique_ptr<TerminatorAdapter> terminator_adapter_;
  int max_var_ = 0;
  SatResult last_result_ = SatResult::Unknown;
  BackendStatistics statistics_;
};

}  // namespace primo::sat
