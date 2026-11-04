#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <utility>
#include <vector>

#include "util/rational.hpp"

namespace primo::theory::arithmetic {

// Already normalized a*x <= b (or < b). Variable identities are opaque;
// there are no SAT atoms, input formulas or tableau identities in a witness.
struct LraCertificatePremise {
  std::vector<std::pair<std::size_t, util::Rational>> coefficients;
  util::Rational rhs{0};
  bool strict = false;
};

enum class LraCertificateStatus { Verified, Infeasible, Interrupted };

struct LraCertificateResult {
  LraCertificateStatus status = LraCertificateStatus::Infeasible;
  std::vector<util::Rational> weights;
  std::size_t variables = 0;
  std::size_t nonzeros = 0;
  std::size_t pivots = 0;
};

// Exact cancellation/strictness check, independent of reconstruction state.
bool check_lra_certificate(std::span<const LraCertificatePremise> premises,
                           std::span<const util::Rational> weights);

// Motzkin witness reconstruction after clause selection. See the normalized
// multiplier feasibility construction and proof in docs/leads/lra-conflict-certificates.md.
// Uses only these premises. A satisfying private LP model supplies the weights;
// no provenance from the main arithmetic search is required.
LraCertificateResult reconstruct_lra_certificate(std::span<const LraCertificatePremise> premises,
                                                 const std::function<bool()>& cancelled = {});

}  // namespace primo::theory::arithmetic
