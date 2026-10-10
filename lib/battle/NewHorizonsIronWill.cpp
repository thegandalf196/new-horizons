/*
 * NewHorizonsIronWill.cpp, part of VCMI engine
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsIronWill.h"
#include "CBattleInfoCallback.h"
#include "Unit.h"
#include "../mapObjects/CGHeroInstance.h"
#include "NewHorizonsArchery.h"

namespace newHorizonsIronWill
{
bool hasPerk(const CGHeroInstance * hero)
{
	return hero && hero->getPerkSkillRank(SKILL) >= MasteryLevel::BASIC
		&& hero->hasActivePerk(SKILL, PERK);
}

std::vector<uint32_t> recipients(const CBattleInfoCallback & battle,
	BattleSide side, const HeroOrderState & order)
{
	std::vector<uint32_t> result;
	if((side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| order.command == HeroCommand::SECOND_WIND
		|| !vstd::contains(heroCommands::CANONICAL_COMMANDS, order.command)
		|| !hasPerk(battle.battleGetFightingHero(side)))
		return result;
	const auto * hero = battle.battleGetFightingHero(side);
	for(const auto * unit : battle.battleAliveUnits())
	{
		if(!unit || unit->isGhost() || unit->isTurret()
			|| unit->hasBonusOfType(BonusType::SIEGE_WEAPON)
			|| unit->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER
			|| battle.battleGetOwner(unit) != battle.sideToPlayer(side))
			continue;
		bool eligible = true;
		switch(order.command)
		{
		case HeroCommand::FOCUS_FIRE:
			eligible = battle.battleIsFocusFireRecipient(unit, side)
				|| (heroCommands::hasCombinedArms(hero) && unit->isMeleeAttacker()
					&& !unit->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));
			break;
		case HeroCommand::FLANK:
			eligible = unit->isMeleeAttacker() || (heroCommands::hasCombinedArms(hero)
				&& newHorizonsArchery::isOrdinaryPhysicalShooter(unit));
			break;
		case HeroCommand::PROTECT:
			eligible = unit->unitId() == order.primaryTargetUnitId || unit->unitId() == order.secondaryTargetUnitId;
			break;
		default:
			break;
		}
		if(eligible)
			result.push_back(unit->unitId());
	}
	std::sort(result.begin(), result.end());
	return result;
}
}
