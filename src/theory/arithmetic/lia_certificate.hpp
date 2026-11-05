#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <utility>
#include <vector>

#include "util/rational.hpp"

namespace primo::theory::arithmetic {

// Integer-valued Rationals keep GMP and overflow handling behind util's API.
// Variable identities are opaque; no source terms or SAT objects are retained.
struct LiaCertificatePremise {
  std::vector<std::pair<std::size_t, util::Rational>> coefficients;
  util::Rational rhs{0};
};

struct LiaCertificateCut {
  util::Rational divisor{1};
  // References: original premises first, then preceding cuts in vector order.
  std::vector<std::pair<std::size_t, util::Rational>> antecedents;
};

enum class LiaCertificateStatus { Verified, NotFound, Interrupted, ResourceLimit };

struct LiaCertificateLimits {
  std::size_t steps = 8192;
  std::size_t active_rows = 4096;
  std::size_t pairs = 65536;
  std::size_t integer_digits = 1024;
};

struct LiaCertificateResult {
  LiaCertificateStatus status = LiaCertificateStatus::NotFound;
  std::vector<LiaCertificateCut> cuts;
  std::size_t contradiction = 0;
  std::size_t generated_steps = 0;
  std::size_t pruned_steps = 0;
  std::size_t pairs = 0;
  std::size_t projections = 0;
  std::size_t exact_projections = 0;
  std::size_t linear_checks = 0;
  std::size_t linear_pivots = 0;
};

// Replay the integer cut rule independently of the producer's projection state.
bool check_lia_certificate(std::span<const LiaCertificatePremise> premises,
                           const LiaCertificateResult& proof);

// Pugh, Omega Test (expanded 1992), §§2.1 and 2.3.1: coefficient-GCD
// tightening and pairwise real-shadow projection. Bounded refutation baseline;
// no dark shadows, splinters, integer model decision or branch replay.
// See docs/leads/lia-conflict-certificates.md for the implemented subset.
LiaCertificateResult reconstruct_lia_certificate(std::span<const LiaCertificatePremise> premises,
                                                 const std::function<bool()>& cancelled = {},
                                                 LiaCertificateLimits limits = {});

}  // namespace primo::theory::arithmetic
