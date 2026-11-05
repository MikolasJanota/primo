#include "theory/arithmetic/lia_certificate.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "theory/arithmetic/lra_certificate.hpp"
#include "util/rational.hpp"

namespace primo::theory::arithmetic {
namespace {
using util::Rational;
using Coefficients = std::map<std::size_t, Rational>;

struct Row {
  Coefficients coefficients;
  Rational rhs;
};
struct Stopped {
  LiaCertificateStatus status;
};

Row canonical(const LiaCertificatePremise& premise) {
  Row row{{}, premise.rhs};
  if (!row.rhs.is_integer()) throw std::invalid_argument("noninteger LIA certificate bound");
  for (const auto& [variable, value] : premise.coefficients) {
    if (!value.is_integer()) throw std::invalid_argument("noninteger LIA certificate coefficient");
    row.coefficients[variable] += value;
  }
  std::erase_if(row.coefficients, [](const auto& entry) { return entry.second.is_zero(); });
  return row;
}

Row apply(const std::vector<Row>& rows, const LiaCertificateCut& cut,
          const std::function<void()>& poll) {
  if (!cut.divisor.is_integer() || cut.divisor.sign() <= 0)
    throw std::invalid_argument("invalid LIA cut divisor");
  Row row;
  for (const auto& [reference, weight] : cut.antecedents) {
    poll();
    if (reference >= rows.size() || !weight.is_integer() || weight.sign() < 0)
      throw std::invalid_argument("invalid LIA cut reference or weight");
    row.rhs += weight * rows[reference].rhs;
    for (const auto& [variable, coefficient] : rows[reference].coefficients) {
      poll();
      row.coefficients[variable] += weight * coefficient;
    }
  }
  std::erase_if(row.coefficients, [](const auto& entry) { return entry.second.is_zero(); });
  for (auto& [variable, value] : row.coefficients) {
    static_cast<void>(variable);
    poll();
    value /= cut.divisor;
    if (!value.is_integer())
      throw std::invalid_argument("LIA divisor does not divide a coefficient");
  }
  row.rhs = (row.rhs / cut.divisor).floor();
  return row;
}

bool replay(std::span<const LiaCertificatePremise> premises, const LiaCertificateResult& proof,
            const std::function<void()>& poll) {
  std::vector<Row> rows;
  for (const auto& premise : premises) {
    poll();
    rows.push_back(canonical(premise));
  }
  for (const auto& cut : proof.cuts) rows.push_back(apply(rows, cut, poll));
  return proof.contradiction < rows.size() && rows[proof.contradiction].coefficients.empty() &&
         rows[proof.contradiction].rhs.sign() < 0;
}

}  // namespace

bool check_lia_certificate(std::span<const LiaCertificatePremise> premises,
                           const LiaCertificateResult& proof) {
  try {
    return replay(premises, proof, [] {});
  } catch (const std::invalid_argument&) {
    return false;
  }
}

LiaCertificateResult reconstruct_lia_certificate(std::span<const LiaCertificatePremise> premises,
                                                 const std::function<bool()>& cancelled,
                                                 LiaCertificateLimits limits) {
  LiaCertificateResult proof;
  const auto poll = [&] {
    if (cancelled && cancelled()) throw Stopped{LiaCertificateStatus::Interrupted};
  };
  const auto size_check = [&](const Rational& value) {
    poll();
    const auto digits = value.to_string().size() - (value.sign() < 0 ? 1 : 0);
    if (digits > limits.integer_digits) throw Stopped{LiaCertificateStatus::ResourceLimit};
  };
  const auto check_row = [&](const Row& row) {
    size_check(row.rhs);
    for (const auto& [variable, value] : row.coefficients) {
      static_cast<void>(variable);
      size_check(value);
    }
  };
  try {
    poll();
    if (premises.size() > limits.active_rows) throw Stopped{LiaCertificateStatus::ResourceLimit};
    std::vector<Row> rows;
    for (const auto& premise : premises) {
      poll();
      rows.push_back(canonical(premise));
      check_row(rows.back());
    }
    const auto append = [&](LiaCertificateCut cut, Row row) {
      poll();
      if (proof.cuts.size() >= limits.steps) throw Stopped{LiaCertificateStatus::ResourceLimit};
      check_row(row);
      size_check(cut.divisor);
      for (const auto& [reference, weight] : cut.antecedents) {
        static_cast<void>(reference);
        size_check(weight);
      }
      proof.cuts.push_back(std::move(cut));
      ++proof.generated_steps;
      rows.push_back(std::move(row));
      return rows.size() - 1;
    };
    const auto tighten = [&](std::size_t reference) {
      poll();
      const auto& row = rows[reference];
      if (row.coefficients.empty()) return reference;
      std::vector<Rational> coefficients;
      for (const auto& [variable, coefficient] : row.coefficients) {
        static_cast<void>(variable);
        coefficients.push_back(coefficient);
      }
      const auto divisor = Rational::gcd(coefficients);
      if (divisor == Rational(1)) return reference;
      LiaCertificateCut cut{divisor, {{reference, Rational(1)}}};
      auto tightened = apply(rows, cut, poll);
      return append(std::move(cut), std::move(tightened));
    };
    const auto finish = [&](std::size_t reference) {
      // Retain all original premises but only ancestors of the contradiction.
      std::vector<bool> needed(proof.cuts.size(), false);
      std::vector<std::size_t> pending{reference};
      while (!pending.empty()) {
        poll();
        const auto current = pending.back();
        pending.pop_back();
        if (current < premises.size() || needed[current - premises.size()]) continue;
        needed[current - premises.size()] = true;
        for (const auto& [parent, weight] : proof.cuts[current - premises.size()].antecedents) {
          static_cast<void>(weight);
          pending.push_back(parent);
        }
      }
      std::vector<std::size_t> remapped(rows.size());
      for (std::size_t i = 0; i < premises.size(); ++i) remapped[i] = i;
      std::vector<LiaCertificateCut> selected;
      for (std::size_t i = 0; i < proof.cuts.size(); ++i) {
        poll();
        if (!needed[i]) continue;
        auto cut = proof.cuts[i];
        for (auto& [parent, weight] : cut.antecedents) {
          static_cast<void>(weight);
          parent = remapped[parent];
        }
        remapped[premises.size() + i] = premises.size() + selected.size();
        selected.push_back(std::move(cut));
      }
      proof.pruned_steps = proof.cuts.size() - selected.size();
      proof.cuts = std::move(selected);
      proof.contradiction = remapped[reference];
      if (!replay(premises, proof, poll)) throw std::logic_error("invalid reconstructed LIA proof");
      poll();
      proof.status = LiaCertificateStatus::Verified;
    };
    // Map canonical left-hand sides to the strongest currently active bound.
    using Active = std::map<Coefficients, std::size_t>;
    std::optional<std::size_t> contradiction;
    const auto insert = [&](Active& active, std::size_t reference) {
      poll();
      const auto& row = rows[reference];
      if (row.coefficients.empty()) {
        if (row.rhs.sign() < 0) contradiction = reference;
        return;
      }
      const auto [found, added] = active.emplace(row.coefficients, reference);
      if (!added && row.rhs < rows[found->second].rhs) found->second = reference;
      if (active.size() > limits.active_rows) throw Stopped{LiaCertificateStatus::ResourceLimit};
    };
    Active active;
    for (std::size_t i = 0; i < premises.size(); ++i) insert(active, tighten(i));
    if (!contradiction && !active.empty()) {
      // Reuse the isolated Farkas reconstruction fast path after integer
      // tightening. Clear all weight denominators before adding the cut.
      std::vector<LraCertificatePremise> linear;
      std::vector<std::size_t> references;
      for (const auto& [coefficients, reference] : active) {
        poll();
        linear.push_back({{coefficients.begin(), coefficients.end()}, rows[reference].rhs, false});
        references.push_back(reference);
      }
      ++proof.linear_checks;
      const auto witness = reconstruct_lra_certificate(linear, cancelled);
      proof.linear_pivots += witness.pivots;
      if (witness.status == LraCertificateStatus::Interrupted)
        throw Stopped{LiaCertificateStatus::Interrupted};
      if (witness.status == LraCertificateStatus::Verified) {
        poll();
        const auto scale = Rational::primitive_integer_scale(witness.weights);
        LiaCertificateCut cut;
        for (std::size_t i = 0; i < references.size(); ++i) {
          poll();
          if (!witness.weights[i].is_zero())
            cut.antecedents.emplace_back(references[i], scale * witness.weights[i]);
        }
        auto row = apply(rows, cut, poll);
        const auto reference = append(std::move(cut), std::move(row));
        if (!rows[reference].coefficients.empty() || rows[reference].rhs.sign() >= 0)
          throw std::logic_error("integer-scaled linear witness is not a contradiction");
        contradiction = reference;
      }
    }
    while (!contradiction && !active.empty()) {
      poll();
      struct Counts {
        std::size_t positive = 0;
        std::size_t negative = 0;
        bool positive_unit = true;
        bool negative_unit = true;
        bool exact() const { return positive_unit || negative_unit; }
      };
      std::map<std::size_t, Counts> counts;
      for (const auto& [coefficients, reference] : active) {
        static_cast<void>(reference);
        for (const auto& [variable, coefficient] : coefficients) {
          poll();
          auto& count = counts[variable];
          if (coefficient.sign() > 0) {
            ++count.positive;
            count.positive_unit = count.positive_unit && coefficient == Rational(1);
          } else {
            ++count.negative;
            count.negative_unit = count.negative_unit && coefficient == Rational(-1);
          }
        }
      }
      const auto chosen =
          std::min_element(counts.begin(), counts.end(), [](const auto& a, const auto& b) {
            if (a.second.exact() != b.second.exact()) return a.second.exact();
            const auto pairs = [](const Counts& count) {
              const auto maximum = std::numeric_limits<std::size_t>::max();
              return count.negative != 0 && count.positive > maximum / count.negative
                         ? maximum
                         : count.positive * count.negative;
            };
            return pairs(a.second) < pairs(b.second);
          });
      const auto variable = chosen->first;
      std::vector<std::size_t> positive;
      std::vector<std::size_t> negative;
      Active projected;
      for (const auto& [coefficients, reference] : active) {
        const auto value = coefficients.find(variable);
        if (value == coefficients.end())
          insert(projected, reference);
        else if (value->second.sign() > 0)
          positive.push_back(reference);
        else
          negative.push_back(reference);
      }
      ++proof.projections;
      if (chosen->second.exact()) ++proof.exact_projections;
      for (const auto upper : positive) {
        for (const auto lower : negative) {
          poll();
          if (proof.pairs >= limits.pairs) throw Stopped{LiaCertificateStatus::ResourceLimit};
          ++proof.pairs;
          // Snapshot coefficients before append() may grow the backing row vector.
          const Rational a = rows[upper].coefficients.at(variable);
          const Rational b = -rows[lower].coefficients.at(variable);
          const Rational pair_values[]{a, b};
          const auto common = Rational::gcd(pair_values);
          LiaCertificateCut cut{Rational(1), {{upper, b / common}, {lower, a / common}}};
          auto sum = apply(rows, cut, poll);
          const auto combined = append(std::move(cut), std::move(sum));
          insert(projected, tighten(combined));
          if (contradiction) break;
        }
        if (contradiction) break;
      }
      active = std::move(projected);
    }
    if (contradiction) finish(*contradiction);
  } catch (const Stopped& stopped) {
    proof.status = stopped.status;
  }
  if (proof.status != LiaCertificateStatus::Verified) proof.cuts.clear();
  return proof;
}

}  // namespace primo::theory::arithmetic
