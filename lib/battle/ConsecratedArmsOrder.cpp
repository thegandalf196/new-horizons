/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ConsecratedArmsOrder.h"
#include "CBattleInfoCallback.h"
#include "IBattleState.h"
#include "Unit.h"
#include "../bonuses/BonusList.h"
#include "../networkPacks/SetStackEffect.h"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace consecratedArms
{
namespace
{
std::vector<Bonus> existingBonuses(const battle::Unit & unit, Prayer prayer)
{
	std::vector<Bonus> result;
	const auto bonuses = unit.getUnstackedBonuses(CSelector([prayer](const Bonus * bonus)
	{
		return identity(bonus) == prayer;
	}));
	for(const auto & bonus : *bonuses)
		result.push_back(*bonus);
	return result;
}

void appendRecipient(SetStackEffect & result, const battle::Unit & unit,
	const std::vector<Prayer> & selected, const Policy & policy, int faith, int potency)
{
	auto prayers = activePrayers(unit);
	int count = 0;
	for(size_t index = 0; index < prayers.size(); ++index)
	{
		const auto prayer = static_cast<Prayer>(index);
		const bool selectedPrayer = std::ranges::find(selected, prayer) != selected.end();
		if(selectedPrayer)
			prayers[index] = ActivePrayer{prayer, potency, duration(faith, policy), policy};
		else if(prayers[index] && policy.has(Perk::LIVING_LITURGY))
			prayers[index]->remainingRounds = duration(faith, policy);
		count += prayers[index].has_value();
	}
	std::vector<Bonus> removals;
	std::vector<Bonus> additions;
	for(size_t index = 0; index < prayers.size(); ++index)
	{
		if(!prayers[index])
			continue;
		const auto & prayer = *prayers[index];
		// Rebuild all extant records to apply Sacred Accord against this single
		// final distinct count. Existing non-refreshed snapshots retain potency.
		auto old = existingBonuses(unit, prayer.prayer);
		removals.insert(removals.end(), old.begin(), old.end());
		auto next = prayerBonuses(prayer.prayer, prayer.potencyFaith, prayer.policy,
			prayer.remainingRounds, count);
		additions.insert(additions.end(), next.begin(), next.end());
	}
	if(!removals.empty())
		result.toRemove.emplace_back(unit.unitId(), std::move(removals));
	if(!additions.empty())
		result.toAdd.emplace_back(unit.unitId(), std::move(additions));
}


void validateSelection(const std::vector<Prayer> & selected, const Policy & policy)
{
	policy.validate();
	if(!policy.rank || selected.empty() || selected.size() > 2
		|| (selected.size() == 2 && selected[0] == selected[1]))
		throw std::runtime_error("Invalid Consecrated Arms accepted Order result");
}
}

std::vector<uint32_t> prayerHolders(const CBattleInfoCallback & battle, BattleSide side,
	const std::vector<uint32_t> & candidates)
{
	auto unique = candidates;
	std::sort(unique.begin(), unique.end());
	unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
	std::erase_if(unique, [&battle, side](uint32_t id)
	{
		const auto * unit = battle.battleGetUnitByID(id);
		return !unit || !unit->alive() || unit->isGhost() || unit->isTurret()
			|| unit->hasBonusOfType(BonusType::SIEGE_WEAPON)
			|| battle.battleGetOwner(unit) != battle.sideToPlayer(side);
	});
	return unique;
}

SetStackEffect orderEffects(const CBattleInfoCallback & battle, BattleSide side,
	const std::vector<uint32_t> & recipients, const std::vector<Prayer> & selected,
	const Policy & policy, int faith)
{
	validateSelection(selected, policy);
	SetStackEffect result;
	result.battleID = battle.getBattle()->getBattleID();
	for(const auto id : prayerHolders(battle, side, recipients))
		appendRecipient(result, *battle.battleGetUnitByID(id), selected, policy, faith, potencyFaith(faith, policy));
	return result;
}

SetStackEffect chosenOrderEffects(const CBattleInfoCallback & battle, BattleSide side,
	const std::vector<uint32_t> & recipients, uint32_t chosenRecipient, const std::vector<Prayer> & selected,
	const Policy & policy, int faith)
{
	validateSelection(selected, policy);
	if(!policy.chosen)
		throw std::runtime_error("Chosen prayers require captured Consecrated Arms version 4");
	SetStackEffect result;
	result.battleID = battle.getBattle()->getBattleID();
	for(const auto id : prayerHolders(battle, side, recipients))
	{
		if(id == chosenRecipient)
			appendRecipient(result, *battle.battleGetUnitByID(id), selected, policy, faith, potencyFaith(faith, policy));
		else if(policy.has(Perk::LIVING_LITURGY))
			appendRecipient(result, *battle.battleGetUnitByID(id), {}, policy, faith, potencyFaith(faith, policy));
	}
	return result;
}

SetStackEffect specialtyPrayerEffects(const CBattleInfoCallback & battle, BattleSide side, uint32_t recipient,
	const std::vector<Prayer> & selected, const Policy & policy, int faith, int potency)
{
	validateSelection(selected, policy);
	SetStackEffect result;
	result.battleID = battle.getBattle()->getBattleID();
	for(const auto id : prayerHolders(battle, side, {recipient}))
		appendRecipient(result, *battle.battleGetUnitByID(id), selected, policy, faith, potency);
	return result;
}

void refreshDivinePurpose(IBattleState & state, const battle::Unit & unit)
{
	const auto active = activePrayer(unit, Prayer::DIVINE_PURPOSE);
	if(!active)
		return;
	const auto old = existingBonuses(unit, Prayer::DIVINE_PURPOSE);
	const auto next = prayerBonuses(Prayer::DIVINE_PURPOSE, active->potencyFaith,
		active->policy, active->remainingRounds, activePrayerCount(unit));
	state.removeUnitBonus(unit.unitId(), old);
	state.addUnitBonus(unit.unitId(), next);
}
}
