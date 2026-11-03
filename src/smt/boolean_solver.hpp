#pragma once

#include <vector>

#include "core/ids.hpp"
#include "smt/check_sat.hpp"
#include "smt/model_reconstruction.hpp"

namespace primo::core {
class TermManager;
}

namespace primo::util {
class Statistics;
}

namespace primo::smt {

class QfUfConflictCollector;

CheckSatOutcome solve_boolean_conjunction(core::TermManager& terms,
                                          const std::vector<core::TermId>& assertions,
                                          const CheckSatOptions& options,
                                          util::Statistics& statistics,
                                          const ModelConversionChain& conversions,
                                          QfUfConflictCollector* collector = nullptr);

}  // namespace primo::smt
