/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <functional>
#include <memory>
#include <span>
class HypotheticBattle;
struct BattleStackAttacked;
namespace heroSpellProjection
{
/// One already enumerated resolved state; invoked synchronously, never retained.
/// completed means native castEval already committed its completion receipts.
/// localOnly applies receipts without running the candidate-wide continuation.
using OutcomeValue = std::function<float(const std::shared_ptr<HypotheticBattle> &,
	std::span<const BattleStackAttacked>, bool completed, bool localOnly)>;
using OutcomeKernel = std::function<float(const std::shared_ptr<HypotheticBattle> &)>;
/// Local family-kernel delta; subtract the same kernel's core-only delta before weighting.
/// This preserves bounded conditional heuristics, not a global stochastic forecast.
float localReceiptMarginal(const std::shared_ptr<HypotheticBattle> &, const OutcomeKernel &);
}
