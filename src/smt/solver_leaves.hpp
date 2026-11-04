#pragma once

#include "smt/check_sat.hpp"
#include "smt/strategy.hpp"

namespace primo::util {
class Statistics;
}

namespace primo::smt {

class CnfConflictCollector;

class Frontend;
class TacticContext;

// Returns an immutable strategy whose executable leaf borrows frontend,
// statistics, and context. The caller must keep those session resources alive
// through compilation and execution. Options are copied into immutable shared
// storage and do not carry execution state.
Strategy make_session_solver_strategy(Frontend& frontend, CheckSatOptions options,
                                      util::Statistics& statistics, const TacticContext& context,
                                      CnfConflictCollector* collector = nullptr);

}  // namespace primo::smt
