/*
 * NewHorizonsEnchantedCommand.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsEnchantedCommand.h"

#include "CBattleInfoCallback.h"
#include "HeroCommand.h"
#include "IBattleState.h"
#include "NewHorizonsCombatSkills.h"
#include "NewHorizonsWarcasting.h"
#include "Unit.h"
#include "../mapObjects/CGHeroInstance.h"

#include <algorithm>

namespace
{
BonusSourceID warcastingSkillSource()
{
	static const BonusSourceID source{SecondarySkill{SecondarySkill::decode(
		newHorizonsEnchantedCommand::SKILL)}};
	return source;
}
}

namespace newHorizonsEnchantedCommand
{
bool eligible(const JsonNode & magicRules, const CGHeroInstance * hero,
	const HeroOrderState & order)
{
	return newHorizonsWarcasting::enabled(magicRules) && hero
		&& hero->hasActivePerk(SKILL, PERK) && heroCommands::isActive(order.command)
		&& order.warcastingBonusPercent > 0 && order.warcastingBonusPercent <= 100;
}

std::vector<uint32_t> recipientIds(const CBattleInfoCallback & battle,
	BattleSide side, const HeroOrderState & order)
{
	std::vector<uint32_t> result;
	if((side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| !battle.getBattle()
		|| !heroCommands::isCanonicalRules(battle.getBattle()->getHeroCommandRules())
		|| !heroCommands::isActive(order.command)
		|| order.issuedRound != battle.battleGetRound())
		return result;
	const auto owner = battle.sideToPlayer(side);
	for(const auto * unit : battle.battleGetAllUnits(false))
	{
		if(!unit || !unit->alive() || unit->isGhost()
			|| battle.battleGetOwner(unit) != owner
			|| !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(unit))
			continue;
		bool covered = false;
		switch(order.command)
		{
			case HeroCommand::PROTECT:
				covered = unit->unitId() == order.primaryTargetUnitId
					|| unit->unitId() == order.secondaryTargetUnitId;
				break;
			case HeroCommand::SECOND_WIND:
				covered = unit->unitId() == order.primaryTargetUnitId;
				break;
			case HeroCommand::FOCUS_FIRE:
				covered = unit->isShooter() || (unit->isMeleeAttacker()
					&& heroCommands::hasCombinedArms(battle.battleGetFightingHero(side)));
				break;
			case HeroCommand::HOLD_THE_LINE:
				covered = std::any_of(order.anchors.begin(), order.anchors.end(),
					[unit](const HeroOrderAnchor & anchor) { return anchor.unitId == unit->unitId(); });
				break;
			case HeroCommand::CHARGE:
			case HeroCommand::FLANK:
			case HeroCommand::RIPOSTE:
			case HeroCommand::BRACE:
				// Shooters can make ordinary melee attacks too. Do not restrict
				// coverage by future charge travel or an attack specialization.
				covered = true;
				break;
			default:
				break;
		}
		if(covered)
			result.push_back(unit->unitId());
	}
	std::sort(result.begin(), result.end());
	result.erase(std::unique(result.begin(), result.end()), result.end());
	return result;
}

Bonus moraleBonus()
{
	Bonus bonus(BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION, BonusType::MORALE,
		BonusSource::SECONDARY_SKILL, MORALE_BONUS, warcastingSkillSource());
	bonus.stacking = PERK;
	bonus.description.appendRawString("Enchanted Command: +1 Morale until the next Creature Activation");
	return bonus;
}

bool isMoraleBonus(const Bonus * bonus)
{
	return bonus && bonus->duration == BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION
		&& bonus->type == BonusType::MORALE && bonus->val == MORALE_BONUS
		&& bonus->source == BonusSource::SECONDARY_SKILL
		&& bonus->sid == warcastingSkillSource() && bonus->stacking == PERK;
}

CSelector moraleBonusSelector()
{
	return CSelector([](const Bonus * bonus) { return isMoraleBonus(bonus); });
}
}
