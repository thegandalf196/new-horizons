/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeAdapter.h"
#include "ForecastSideTransitions.h"
#include "HypotheticBattle.h"
#include "SearchBudget.h"
#include "../../lib/entities/hero/NewHorizonsLeadership.h"
#include <stdexcept>
namespace forecast
{
std::optional<State> NativeAdapter::prepareSideChange(HypotheticBattle & branch,
	BattleSide side, const SideReceipt & receipt)
{
	const auto dirty = static_cast<uint32_t>(receipt.changes());
	const auto unitDirty = static_cast<uint32_t>(SideDirty::UnitStats | SideDirty::HealthCapacity
		| SideDirty::Retaliation | SideDirty::SpellDefense);
	if(!branch.compactForecast || !(dirty & unitDirty)) return {};
	if(branch.forecastSide(side) != receipt.expected() || !receipt.after())
		throw std::logic_error("Stale compact side derivation receipt");
	if(branch.searchBudget()) branch.searchBudget()->require(battleSearch::WorkKind::Node,
		static_cast<uint32_t>(branch.compactForecast->ids().size()));
	// This non-owning callback exists ONLY within the synchronous adapter call.
	// The prospective world and every borrowed native view die before return;
	// published State contains owned numbers/health and immutable rule owners.
	auto borrowed = std::shared_ptr<CBattleInfoCallback>(&branch, [](CBattleInfoCallback *) {});
	HypotheticBattle prospective(branch.env, std::move(borrowed),
		std::shared_ptr<const battle::OwnedBattleFrameBatch>{}, branch.searchBudget());
	prospective.sideStates.at(side) = receipt.after();
	++prospective.bonusTreeVersion;
	auto draft = *branch.compactForecast;
	for(const auto id : draft.ids())
	{
		if(draft.definition(id)->side != side) continue; // Original source side, never controller.
		const auto evaluated = prospective.getForUpdate(id);
		auto next = *draft.unit(id);
		// Attunement masks change damage/defense/retaliation. Greater Essence's
		// maximum-HP coefficient belongs to the immutable profile, not its mask.
		// Broader capacity-changing plans need their separately shared transition.
		if(evaluated->getMaxHealth() != next.health.valueContext().maximumHealth)
			throw std::logic_error("Unadmitted compact side capacity transition");
		for(size_t mode = 0; mode < 2; ++mode)
		{
			auto facts = nativeDamage::prepareUnit(*evaluated, mode != 0);
			const auto & leadership = draft.definition(id)->leadership;
			if(facts && leadership.known) facts->leadershipEfficiency = leadership.active
				? newHorizonsHeroes::squareRootLeadershipEfficiency(next.health.getCount(), leadership.capacity) : 1.0;
			next.numeric[mode] = facts
				? std::make_shared<const nativeDamage::PreparedUnit>(*facts) : nullptr;
		}
		const auto retal = evaluated->counterAttacks.observeWithoutLatch();
		next.activation.retaliation = {retal.available, retal.total, retal.limited};
		draft.commit(id, std::move(next), Damage | Effects | Queue, false);
		draft.counters.add(&Counters::derivedRefreshes);
	}
	// The side capability is shared by the ordered roster, including recipients
	// whose original side differs or whose current controller was changed.
	for(const auto id : draft.ids())
	{
		const auto unit = prospective.getForUpdate(id);
		stageNativeReadValue(draft, id, *unit, unit->preparedModifierWarfare());
	}
	refreshReadViews(draft, prospective, draft.orderedIds(), true);
	return draft;
}
}
