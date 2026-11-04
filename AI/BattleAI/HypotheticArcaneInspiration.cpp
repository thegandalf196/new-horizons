/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "../../lib/spells/ArcaneInspirationCompletion.h"
#include "../../lib/networkPacks/SetStackEffect.h"

void HypotheticBattle::HypotheticServerCallback::recordArcaneInspirationEffects(const SetStackEffect & packet)
{
	owner->arcaneInspirationEffects.push_back(std::make_shared<const SetStackEffect>(packet));
}

void HypotheticBattle::completeSyntheticInspiration(BattleSide side)
{
	if(!arcaneInspirationEffects.empty())
		throw std::logic_error("Arcane Inspiration completion is already recorded");
	arcaneInspiration::publishCompletedSpell(*getServerCallback(), *this, side, {});
}

void HypotheticBattle::reverseArcaneInspirationEffects(
	std::span<const std::shared_ptr<const SetStackEffect>> receipts)
{
	for(auto it = receipts.rbegin(); it != receipts.rend(); ++it)
	{
		const auto & packet = **it;
		if(!packet.toUpdate.empty())
			throw std::logic_error("Arcane Inspiration utility requires a reversible completion receipt");
		SetStackEffect reverse;
		reverse.battleID = packet.battleID;
		reverse.toRemove = packet.toAdd;
		reverse.toAdd = packet.toRemove;
		getServerCallback()->apply(reverse);
	}
}
