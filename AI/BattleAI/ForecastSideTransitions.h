/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "ForecastSideState.h"
#include <functional>
#include <span>
namespace battleSearch { class SearchBudget; }
namespace forecast
{
enum class SideDirty : uint32_t
{
	None = 0, Resources = 1, Actions = 2, Completion = 4, Round = 8,
	UnitStats = 16, HealthCapacity = 32, Retaliation = 64, SpellDefense = 128
};
constexpr SideDirty operator|(SideDirty first, SideDirty second)
{ return static_cast<SideDirty>(static_cast<uint32_t>(first) | static_cast<uint32_t>(second)); }
class SideTransitions;
class DLL_LINKAGE SideReceipt
{
	friend class SideTransitions;
	std::shared_ptr<const SideState> before, next;
	SideDirty dirty = SideDirty::None;
	std::vector<uint32_t> mandateOverlap;
	std::optional<uint64_t> paymentToken;
public:
	const std::shared_ptr<const SideState> & after() const { return next; }
	const std::shared_ptr<const SideState> & expected() const { return before; }
	SideDirty changes() const { return dirty; }
	const std::vector<uint32_t> & sharedPurposeRecipients() const { return mandateOverlap; }
	std::optional<uint64_t> chargedCast() const { return paymentToken; }
};
struct CastCost { SpellID spell; int64_t amount = 0; bool automatic = false; };
/// These transitions use native value ledgers. They never resolve spell effects,
/// Metamagic removal/energy, RNG, utility, or additional cast recursion.
class DLL_LINKAGE SideTransitions
{
	static std::optional<SideReceipt> stage(const std::shared_ptr<const SideState> &, SideDirty,
		const std::function<void(SideState &)> &, battleSearch::SearchBudget *, bool needsHero = true);
public:
	static bool commit(std::shared_ptr<const SideState> &, const SideReceipt &) noexcept;
	static std::optional<SideReceipt> acceptedAction(const std::shared_ptr<const SideState> &,
		const HeroActionAllowanceState::Receipt &, const HeroActionAllowanceState & consumed,
		bool lightSpell, uint8_t maximumPairs, const std::vector<uint32_t> & recipients,
		std::optional<CastCost> cost = {}, battleSearch::SearchBudget * budget = nullptr,
		std::span<const battle::spellPoints::Payment> refunds = {}, int64_t residualLifeDamage = 0);
	static std::optional<SideReceipt> payCast(const std::shared_ptr<const SideState> &, CastCost,
		battleSearch::SearchBudget * budget = nullptr);
	static std::optional<SideReceipt> refundCast(const std::shared_ptr<const SideState> &, uint64_t token,
		battle::spellPoints::Payment, battleSearch::SearchBudget * budget = nullptr);
	static std::optional<SideReceipt> restoreNormal(const std::shared_ptr<const SideState> &, int32_t amount,
		battleSearch::SearchBudget * budget = nullptr);
	static std::optional<SideReceipt> recordResidualLife(const std::shared_ptr<const SideState> &, int64_t damage,
		battleSearch::SearchBudget * budget = nullptr);
	static std::optional<SideReceipt> replaceMana(const std::shared_ptr<const SideState> &,
		const battle::spellPoints::State & expected, battle::spellPoints::State after,
		battleSearch::SearchBudget * budget = nullptr);
	static std::optional<SideReceipt> recordWisdom(const std::shared_ptr<const SideState> &, SpellID, int32_t round,
		battleSearch::SearchBudget * budget = nullptr);
	static std::optional<SideReceipt> recordCompletion(const std::shared_ptr<const SideState> &, int32_t level,
		battleSearch::SearchBudget * budget = nullptr, std::optional<SpellID> automaticSpell = {});
	static std::optional<SideReceipt> setResponse(const std::shared_ptr<const SideState> &, SpellResponseState,
		battleSearch::SearchBudget * budget = nullptr);
	static std::optional<SideReceipt> setOverwhelming(const std::shared_ptr<const SideState> &, OverwhelmingFormulaState,
		battleSearch::SearchBudget * budget = nullptr);
	static std::optional<SideReceipt> setAttunement(const std::shared_ptr<const SideState> &, uint8_t mask,
		battleSearch::SearchBudget * budget = nullptr);
	static std::optional<SideReceipt> advanceRound(const std::shared_ptr<const SideState> &, int32_t round,
		battleSearch::SearchBudget * budget = nullptr);
};
}
