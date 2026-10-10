/*
 * NewHorizonsDivineMandate.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsDivineMandate.h"
#include "CBattleInfoCallback.h"
#include "IBattleState.h"
#include "NewHorizonsEnchantedCommand.h"
#include "Unit.h"
#include "../bonuses/Bonus.h"

#include "../entities/hero/NewHorizonsHeroRules.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../spells/NewHorizonsMagic.h"

namespace newHorizonsDivineMandate
{
namespace
{
bool hasActiveDivineMandate(const CGHeroInstance * hero);
}

bool hasDivineDisciplinePerk(const CGHeroInstance * hero)
{
	return hasActiveDivineMandate(hero) && hero->hasActivePerk("new-horizons:divineMandate",
		"new-horizons:divineMandate.divineDiscipline");
}

bool completeDisciplineOrders(std::vector<HeroOrderState> & orders, int32_t round, uint32_t unitId, bool genuineActivation)
{
	bool changed = false;
	for(auto & order : orders)
		if(!order.ironWillLifetime || genuineActivation)
			changed = order.completeDisciplineActivation(unitId, round) || changed;
	return changed;
}

void completeDisciplineActivation(IBattleState & state, uint32_t unitId, bool genuineActivation)
{
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		auto orders = state.getHeroOrderStates(side);
		if(completeDisciplineOrders(orders, state.getRound(), unitId, genuineActivation))
			state.setHeroOrderStates(side, orders);
	}
}

bool hasCrownAndAltarPerk(const CGHeroInstance * hero)
{
	return hasActiveDivineMandate(hero) && hero->hasActivePerk("new-horizons:divineMandate",
		"new-horizons:divineMandate.crownAndAltar");
}

bool needsRecipientCapture(const CGHeroInstance * hero)
{
	return hasSharedPurposePerk(hero) || hasCrownAndAltarPerk(hero);
}

JsonNode crownOrderFormula(const JsonNode & formula, bool eligible)
{
	auto result = formula;
	if(eligible)
	{
		result["attack"].Float() *= 1.2;
		result["defense"].Float() *= 1.2;
	}
	return result;
}

int32_t crownSecondWindPercent(const CGHeroInstance & hero, const HeroOrderState & order, uint32_t unitId)
{
	return heroCommands::secondWindPercent(hero, order.warcastingBonusPercent,
		order.divineMandateEfficiencyBonusPercent(), order.crownAndAltarAppliesTo(unitId));
}

bool hasSharedPurposePerk(const CGHeroInstance * hero)
{
	return hasActiveDivineMandate(hero) && hero->hasActivePerk("new-horizons:divineMandate",
		"new-horizons:divineMandate.sharedPurpose");
}

std::vector<uint32_t> sharedPurposeFriendlyRecipients(const CBattleInfoCallback & battle,
	BattleSide side, const std::vector<uint32_t> & recipients, bool includeDead)
{
	std::vector<uint32_t> result;
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return result;
	for(const auto id : recipients)
	{
		const auto * unit = battle.battleGetUnitByID(id);
		if(unit && (includeDead || unit->alive()) && !unit->isGhost() && !unit->isTimeStopped()
			&& battle.battleGetOwner(unit) == battle.sideToPlayer(side))
			result.push_back(id);
	}
	std::sort(result.begin(), result.end());
	result.erase(std::unique(result.begin(), result.end()), result.end());
	return result;
}

std::vector<uint32_t> sharedPurposeOrderRecipients(const CBattleInfoCallback & battle,
	BattleSide side, const HeroOrderState & order)
{
	return sharedPurposeFriendlyRecipients(battle, side,
		newHorizonsEnchantedCommand::recipientIds(battle, side, order));
}

Bonus sharedPurposeMoraleBonus()
{
	Bonus bonus(BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION, BonusType::MORALE,
		BonusSource::SECONDARY_SKILL, 1, BonusSourceID(SecondarySkill(
			SecondarySkill::decode("new-horizons:divineMandate"))));
	bonus.stacking = "new-horizons:divineMandate.sharedPurpose";
	bonus.description.appendRawString("Shared Purpose: +1 Morale until the next Creature Activation");
	return bonus;
}

void applySharedPurpose(IBattleState & state, const CBattleInfoCallback & battle,
	BattleSide side, const std::vector<uint32_t> & recipients)
{
	if(!hasSharedPurposePerk(battle.battleGetFightingHero(side)))
		return;
	const auto bonus = sharedPurposeMoraleBonus();
	for(const auto id : sharedPurposeFriendlyRecipients(battle, side, recipients))
	{
		const auto * unit = battle.battleGetUnitByID(id);
		if(!unit->hasBonus(CSelector(isSharedPurposeMoraleBonus)))
			state.addUnitBonus(id, {bonus});
	}
}

bool isSharedPurposeMoraleBonus(const Bonus * bonus)
{
	return bonus && bonus->duration == BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION
		&& bonus->type == BonusType::MORALE && bonus->val == 1
		&& bonus->source == BonusSource::SECONDARY_SKILL
		&& bonus->sid == BonusSourceID(SecondarySkill(SecondarySkill::decode("new-horizons:divineMandate")))
		&& bonus->stacking == "new-horizons:divineMandate.sharedPurpose";
}
namespace
{
bool hasActiveDivineMandate(const CGHeroInstance * hero)
{
	if(!hero || hero->getFactionID() != FactionID::CASTLE
		|| !hero->usesPrimaryGrowth()
		|| !newHorizonsHeroes::usesRules(hero->getPrimaryGrowthRules())
		|| !newHorizonsMagic::spellPointRulesActive(hero->getMagicRules()))
		return false;

	const auto factionSkill = newHorizonsHeroes::factionSkill(
		hero->getPrimaryGrowthRules(), FactionID::CASTLE);
	const auto divineMandateId = SecondarySkill::decode("new-horizons:divineMandate");
	return factionSkill && divineMandateId >= 0 && *factionSkill == SecondarySkill(divineMandateId)
		&& hero->getSecSkillLevel(*factionSkill) != 0;
}
}

int32_t chaplainReserveRecovery(const CGHeroInstance * hero,
	const uint8_t beforeCompletedPairs, const uint8_t afterCompletedPairs)
{
	if(beforeCompletedPairs != 0 || afterCompletedPairs != 1 || !hasActiveDivineMandate(hero))
		return 0;

	return hero->hasActivePerk("new-horizons:divineMandate",
		"new-horizons:divineMandate.chaplainSReserve") ? 3 : 0;
}

int32_t sacredCommandEfficiencyBonusPercent(const CGHeroInstance * hero)
{
	return hasActiveDivineMandate(hero) && hero->hasActivePerk("new-horizons:divineMandate",
		"new-horizons:divineMandate.sacredCommand") ? 10 : 0;
}

int32_t knightlySequenceOrderBonusPercent(const CGHeroInstance * hero)
{
	return hasActiveDivineMandate(hero) && hero->hasActivePerk("new-horizons:divineMandate",
		"new-horizons:divineMandate.knightlySequence") ? 5 : 0;
}

int32_t knightlySequenceSpellCostReduction(const CGHeroInstance * hero)
{
	return hasActiveDivineMandate(hero) && hero->hasActivePerk("new-horizons:divineMandate",
		"new-horizons:divineMandate.knightlySequence") ? 2 : 0;
}

int32_t consecratedCastingBonusPercent(const CGHeroInstance * hero)
{
	return hasActiveDivineMandate(hero) && hero->hasActivePerk("new-horizons:divineMandate",
		"new-horizons:divineMandate.consecratedCasting") ? 10 : 0;
}

bool hasPurifyingMandatePerk(const CGHeroInstance * hero)
{
	return hasActiveDivineMandate(hero) && hero->hasActivePerk("new-horizons:divineMandate",
		"new-horizons:divineMandate.purifyingMandate");
}

bool hasRoyalStandardPerk(const CGHeroInstance * hero)
{
	return hasActiveDivineMandate(hero) && hero->hasActivePerk("new-horizons:divineMandate",
		"new-horizons:divineMandate.royalStandard");
}
}
