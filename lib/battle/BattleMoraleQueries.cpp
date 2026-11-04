/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CBattleInfoCallback.h"
#include "IBattleState.h"
#include "Unit.h"
#include "../bonuses/BonusList.h"
#include "HeroCommandRoyalStandard.h"
#include "HeroCommandAdjacency.h"
#include "NewHorizonsDiscipline.h"
#include "NewHorizonsBloodrage.h"
#include "PassiveDivineMandate.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../mapObjects/NewHorizonsGlyphsOfFear.h"
#include <algorithm>
#include <limits>
#include <unordered_map>

BattleMoraleInfo CBattleInfoCallback::battleGetMoraleInfo(const battle::Unit * unit) const
{
	BattleMoraleInfo result;
	if(!unit)
		return result;

	if(!getBattle() || !unit->alive() || unit->isGhost())
	{
		result.real = unit->moraleVal();
		result.effective = result.real;
		return result;
	}

	const int32_t firstRoundMoraleModifier = getBattle()->getRound() == 1
		? getBattle()->getFirstRoundMoraleModifier(unit->unitSide()) : 0;
	result.firstRoundModifier = firstRoundMoraleModifier;
	const auto * hero = battleGetOwnerHero(unit);
	const auto divineRiders = newHorizonsPassiveDivineMandate::activeOrderRiders(*this, unit);
	int32_t additionalMorale = 0;
	if(hero && hero->hasActivePerk("new-horizons:discipline", "new-horizons:discipline.standardBearer"))
	{
		const auto owner = battleGetOwner(unit);
		for(const auto * supporter : battleGetUnitsIf([](const battle::Unit * candidate)
			{
				return candidate->alive() && !candidate->isGhost();
			}))
		{
			if(supporter->unitId() == unit->unitId() || battleGetOwner(supporter) != owner)
				continue;

			if(heroCommands::orderUnitsAdjacent(unit, supporter))
			{
				additionalMorale = 1;
				break;
			}
		}
	}
	result.standardBearerBonus = additionalMorale;
	additionalMorale += divineRiders.morale;
	const auto glyphsBonuses = newHorizonsGlyphsOfFear::moraleBonuses(hero, getBattle()->getLocation());
	const int32_t glyphsMorale = glyphsBonuses->totalValue();

	const auto applyMoraleFloor = [this, unit, hero, &result, divineRiders](int morale)
	{
		result.real = morale;
		if(morale >= 0)
			return morale;
		if(divineRiders.ignoreNegativeMorale)
			return 0;
		if(battleHasCommandingPresence(unit))
		{
			result.commandingPresenceFloorApplied = true;
			return 0;
		}
		const auto side = playerToSide(battleGetOwner(unit));
		if(side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
		{
			if(heroCommands::hasVanguardRoyalStandardEntitlement(unit, side))
				return 0;
			const auto orders = battleGetHeroOrderStates(side);
			if(std::ranges::any_of(orders, [this, unit](const HeroOrderState & order)
			{
				return order.scheduledFor(unit->unitId(), battleGetRound())
					&& std::binary_search(order.royalStandardRecipientUnitIds.begin(),
						order.royalStandardRecipientUnitIds.end(), unit->unitId());
			}))
				return 0;
		}
		if(newHorizonsBloodrage::hasFuryUnbound(hero)
			&& battleGetBloodrageDamagePercent(unit) > 0)
		{
			result.furyUnboundFloorApplied = true;
			return 0;
		}
		return morale;
	};
	const bool steadfast = newHorizonsDiscipline::hasSteadfast(hero);
	const bool espritDeCorps = newHorizonsDiscipline::hasEspritDeCorps(hero);
	if(!steadfast && !espritDeCorps)
	{
		if(additionalMorale == 0 && firstRoundMoraleModifier == 0 && glyphsMorale == 0)
			result.effective = applyMoraleFloor(unit->moraleVal());
		else
		{
			const auto totalAdditionalMorale = static_cast<int64_t>(additionalMorale)
				+ static_cast<int64_t>(firstRoundMoraleModifier) + glyphsMorale;
			const auto boundedAdditionalMorale = static_cast<int32_t>(std::clamp<int64_t>(totalAdditionalMorale,
				std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
			result.effective = applyMoraleFloor(unit->moraleValWithBonus(boundedAdditionalMorale));
		}
		return result;
	}

	const auto currentMoraleBonuses = unit->getBonusesOfType(BonusType::MORALE);
	const auto compositionBonuses = newHorizonsDiscipline::espritDeCorpsMoraleBonuses(hero, *unit);
	const int64_t compositionDelta = static_cast<int64_t>(compositionBonuses->totalValue())
		- currentMoraleBonuses->totalValue();
	result.espritDeCorpsAdjustment = static_cast<int32_t>(std::clamp<int64_t>(compositionDelta,
		std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
	const auto moraleBonuses = newHorizonsGlyphsOfFear::appendMoraleBonuses(
		newHorizonsDiscipline::espritDeCorpsMoraleBonuses(hero, *unit, false), hero, getBattle()->getLocation());
	BonusList adjustedMoraleBonuses;
	std::unordered_map<const Bonus *, std::shared_ptr<Bonus>> adjustedByOriginal;
	const PlayerColor targetAuraOwner = unit->unitOwner() == PlayerColor::UNFLAGGABLE
		? PlayerColor::NEUTRAL : unit->unitOwner();
	for(const auto & bonus : *moraleBonuses)
	{
		const bool hostileCreatureAura = bonus->source == BonusSource::CREATURE_ABILITY
			&& bonus->bonusOwner != PlayerColor::CANNOT_DETERMINE
			&& bonus->bonusOwner != targetAuraOwner;
		if(steadfast && bonus->val < 0 && (bonus->appliedByEnemy || hostileCreatureAura))
		{
			auto & adjusted = adjustedByOriginal[bonus.get()];
			if(!adjusted)
			{
				adjusted = std::make_shared<Bonus>(*bonus);
				++adjusted->val;
			}
			adjustedMoraleBonuses.push_back(adjusted);
		}
		else
			adjustedMoraleBonuses.push_back(bonus);
	}
	adjustedMoraleBonuses.stackBonuses();

	const int64_t moraleDelta = static_cast<int64_t>(adjustedMoraleBonuses.totalValue())
		- static_cast<int64_t>(currentMoraleBonuses->totalValue());
	result.steadfastAdjustment = static_cast<int32_t>(std::clamp<int64_t>(moraleDelta - compositionDelta - glyphsMorale,
		std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
	const int64_t totalAdditionalMorale = moraleDelta + additionalMorale + firstRoundMoraleModifier;
	const auto boundedAdditionalMorale = static_cast<int32_t>(std::clamp<int64_t>(totalAdditionalMorale,
		std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
	result.effective = applyMoraleFloor(unit->moraleValWithBonus(boundedAdditionalMorale));
	return result;
}

int CBattleInfoCallback::battleGetMorale(const battle::Unit * unit) const
{
	return battleGetMoraleInfo(unit).effective;
}
