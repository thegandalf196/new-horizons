/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CreatureAbilityApplication.h"
#include "../CGameHandler.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include <algorithm>

namespace creatureAbilityApplication
{
bool resisted(CGameHandler & game, const CBattleInfoCallback & battle,
	const battle::Unit & target, const creatureAbility::Descriptor & descriptor)
{
	if(!descriptor || descriptor->sourceOwner == PlayerColor::CANNOT_DETERMINE
		|| descriptor->sourceOwner == battle.battleGetOwner(&target))
		return false;
	const int chance = creatureAbility::unshakableResistanceBP(*descriptor, target);
	if(chance == 0 || game.getRandomGenerator().nextInt(9999) >= chance)
		return false;
	BattleLogMessage message;
	message.battleID = battle.getBattle()->getBattleID();
	MetaString line;
	line.appendRawString("Unshakable Faith resists ");
	line.appendRawString(descriptor->logicalIdentity());
	line.appendRawString(" on ");
	line.appendRawString(target.getDescription());
	line.appendRawString(".");
	message.lines.push_back(std::move(line));
	game.sendAndApply(message);
	return true;
}

namespace
{
struct Group
{
	uint32_t target;
	creatureAbility::Descriptor descriptor;
	BonusSource source;
	BonusSourceID sid;
	std::string status;
	bool matches(uint32_t recipient, const Bonus & bonus, bool requireDescriptor) const
	{
		if(target != recipient || status != bonus.statusIdentity)
			return false;
		// A resisted replacement must retain the previous logical status even
		// when another supplier granted it. Empty legacy identities still need
		// the original source/SID match to avoid claiming unrelated removals.
		if(!requireDescriptor && !status.empty())
			return true;
		return source == bonus.source && sid == bonus.sid
			&& (!requireDescriptor || (bonus.abilityDescriptor && *descriptor == *bonus.abilityDescriptor));
	}
};
}

void preventDirectAfflictions(CGameHandler & game, SetStackEffect & effects)
{
	std::vector<Group> groups;
	for(const auto * changes : {&effects.toAdd, &effects.toUpdate})
		for(const auto & [target, bonuses] : *changes)
			for(const auto & bonus : bonuses)
			{
				const auto & descriptor = bonus.abilityDescriptor;
				if(!descriptor || descriptor->resolution != creatureAbility::Resolution::DIRECT_APPLICATION
					|| descriptor->family != creatureAbility::Family::CREATURE_ABILITY
					|| !(descriptor->categories & creatureAbility::HOSTILE_AFFLICTION)) continue;
				if(std::ranges::none_of(groups, [&](const Group & group) { return group.matches(target, bonus, true); }))
					groups.push_back({target, descriptor, bonus.source, bonus.sid, bonus.statusIdentity});
			}
	if(groups.empty()) return;
	const auto * battle = game.gameState().getBattle(effects.battleID);
	if(!battle) throw std::runtime_error("Creature Ability application requires a battle");
	for(const auto & group : groups)
	{
		const auto * target = battle->battleGetUnitByID(group.target);
		if(!target || !resisted(game, *battle, *target, group.descriptor)) continue;
		for(auto * changes : {&effects.toAdd, &effects.toUpdate, &effects.toRemove})
		{
			for(auto & [recipient, bonuses] : *changes)
				std::erase_if(bonuses, [&](const Bonus & bonus)
				{
					return group.matches(recipient, bonus, changes != &effects.toRemove);
				});
			std::erase_if(*changes, [](const auto & row) { return row.second.empty(); });
		}
	}
}
}
