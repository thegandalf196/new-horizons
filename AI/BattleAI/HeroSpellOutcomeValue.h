/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
#include <functional>
#include <cstdint>
#include <memory>
#include <span>
class HypotheticBattle;
class PlayerColor;
namespace battleSearch { class SearchBudget; }
struct BattleStackAttacked;
namespace heroSpellProjection
{
using OutcomeKernel = std::function<float(const std::shared_ptr<HypotheticBattle> &)>;
/// Synchronous existing signed health kernel; caller retains the budget owner.
DLL_LINKAGE OutcomeKernel healthOutcomeKernel(PlayerColor, battleSearch::SearchBudget *);
struct OutcomeReceipt
{
	std::span<const uint32_t> resolved;
	/// Existing kernel already weights this conditional original spell outcome.
	bool originalChanceWeighted = true;
	const OutcomeKernel * kernel = nullptr;
};
struct OutcomeResult
{
	float value = 0;
	bool completeLocal = false;
	OutcomeResult(float value = 0, bool completeLocal = false) : value(value), completeLocal(completeLocal) {}
	operator float() const { return value; }
};
/// Synchronous existing outcome only. No new outcome or continuation is selected.
/// completed identifies native castEval completion; localOnly forbids a queue.
using OutcomeValue = std::function<OutcomeResult(const std::shared_ptr<HypotheticBattle> &,
	std::span<const BattleStackAttacked>, bool completed, bool localOnly, OutcomeReceipt)>;
DLL_LINKAGE float evaluateLocalOutcome(const OutcomeValue &, const std::shared_ptr<HypotheticBattle> &,
	std::span<const BattleStackAttacked>, bool completed, OutcomeReceipt, const OutcomeKernel &);
/// Local family-kernel delta; subtract the same kernel's core-only delta before weighting.
/// This preserves bounded conditional heuristics, not a global stochastic forecast.
float localReceiptMarginal(const std::shared_ptr<HypotheticBattle> &, const OutcomeKernel &);
}
