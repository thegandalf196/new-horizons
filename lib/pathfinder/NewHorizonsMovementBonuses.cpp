/*
 * NewHorizonsMovementBonuses.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NewHorizonsMovementBonuses.h"
#include "NewHorizonsLighthouse.h"
#include "../bonuses/BonusList.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../entities/hero/NewHorizonsCastleAdventureSpecialties.h"
#include <array>
#include <cstdint>
#include <limits>

namespace
{
struct NewHorizonsMovementModifiers
{
	int64_t base = 0;
	int64_t percentageToBase = 0;
	int64_t percentageToAll = 0;
	int64_t additive = 0;
	// BonusList::totalValue has a distinct fallback when the list contains
	// independent bounds only.  Source/target percentage entries count as
	// non-independent entries there, even though they do not add a value by
	// themselves.
	int nonIndependentBonusCount = 0;
	// BonusList calls these independentMin (at most) and independentMax (at
	// least).  Keep the original names so their clamping semantics remain
	// explicit when the final movement value is produced.
	std::optional<int64_t> independentMin;
	std::optional<int64_t> independentMax;
};

int64_t saturatingAdd(const int64_t lhs, const int64_t rhs)
{
	if(rhs > 0 && lhs > std::numeric_limits<int64_t>::max() - rhs)
		return std::numeric_limits<int64_t>::max();
	if(rhs < 0 && lhs < std::numeric_limits<int64_t>::min() - rhs)
		return std::numeric_limits<int64_t>::min();
	return lhs + rhs;
}

int64_t saturatingMultiply(const int64_t lhs, const int64_t rhs)
{
	if(lhs == 0 || rhs == 0)
		return 0;
	if(lhs == -1 && rhs == std::numeric_limits<int64_t>::min())
		return std::numeric_limits<int64_t>::max();
	if(rhs == -1 && lhs == std::numeric_limits<int64_t>::min())
		return std::numeric_limits<int64_t>::max();

	if(lhs > 0)
	{
		if(rhs > 0 && lhs > std::numeric_limits<int64_t>::max() / rhs)
			return std::numeric_limits<int64_t>::max();
		if(rhs < 0 && rhs < std::numeric_limits<int64_t>::min() / lhs)
			return std::numeric_limits<int64_t>::min();
	}
	else
	{
		if(rhs > 0 && lhs < std::numeric_limits<int64_t>::min() / rhs)
			return std::numeric_limits<int64_t>::min();
		if(rhs < 0 && lhs < std::numeric_limits<int64_t>::max() / rhs)
			return std::numeric_limits<int64_t>::max();
	}
	return lhs * rhs;
}

int64_t applyPercentageRoundDown(const int64_t base, const int64_t percentage)
{
	const int64_t factor = saturatingAdd(100, percentage);
	return saturatingMultiply(base, factor) / 100;
}

int64_t applyPercentageRoundUp(const int64_t base, const int64_t percentage)
{
	const int64_t factor = saturatingAdd(100, percentage);
	const int64_t product = saturatingMultiply(base, factor);
	// This mirrors BonusList::totalValue: creature-ability values are the
	// sole existing bonus family that rounds up instead of down.
	return (base >= 0 ? saturatingAdd(product, 99) : saturatingAdd(product, -99)) / 100;
}

bool isCoreLogisticsSourceBonus(const Bonus & bonus)
{
	return bonus.source == BonusSource::SECONDARY_SKILL
		&& bonus.sid == BonusSourceID(SecondarySkill(SecondarySkill::LOGISTICS));
}

bool isNewHorizonsLogisticsSourceBonus(const Bonus & bonus)
{
	static const int newHorizonsLogistics = SecondarySkill::decode("new-horizons:logistics");
	return bonus.source == BonusSource::SECONDARY_SKILL
		&& newHorizonsLogistics >= 0
		&& bonus.sid == BonusSourceID(SecondarySkill(newHorizonsLogistics));
}

bool isLogisticsCoreEffectMovementBonus(const Bonus & bonus)
{
	// The legacy Logistics entity predates the New Horizons split movement
	// selectors and only carries a land bonus. New Horizons maps that skill to
	// its own Logistics entity, whose core effects supply both movement pools.
	const bool landOrSeaMovement = bonus.subtype == BonusSubtypeID(BonusCustomSubtype::heroMovementLand)
		|| bonus.subtype == BonusSubtypeID(BonusCustomSubtype::heroMovementSea);
	return bonus.type == BonusType::MOVEMENT
		&& bonus.valType == BonusValueType::PERCENT_TO_BASE
		&& landOrSeaMovement
		&& (isCoreLogisticsSourceBonus(bonus) || isNewHorizonsLogisticsSourceBonus(bonus));
}

NewHorizonsMovementModifiers getNewHorizonsMovementModifiers(
	const TConstBonusListPtr & bonuses, const CSelector & daySelector, const CGHeroInstance * hero)
{
	NewHorizonsMovementModifiers result;
	std::array<int64_t, vstd::to_underlying(BonusSource::NUM_BONUS_SOURCE)> percentToSource = {};

	// BonusList first gathers source/target-type percentage modifiers, then
	// applies them to each value before dispatching by value type.  Reproduce
	// that ordering here instead of flattening only PERCENT_TO_BASE and
	// ADDITIVE_VALUE entries; movement effects are still ordinary BonusList
	// values and must retain those semantics.
	for(const auto & bonus : *bonuses)
	{
		if((hero && newHorizonsLighthouse::departurePercent(*hero) && newHorizonsLighthouse::isLegacyCastleBonus(*bonus)) || !daySelector(bonus.get()))
			continue;

		switch(bonus->valType)
		{
		case BonusValueType::PERCENT_TO_SOURCE:
			percentToSource[vstd::to_underlying(bonus->source)] = saturatingAdd(
				percentToSource[vstd::to_underlying(bonus->source)], bonus->val);
			break;
		case BonusValueType::PERCENT_TO_TARGET_TYPE:
			percentToSource[vstd::to_underlying(bonus->targetSourceType)] = saturatingAdd(
				percentToSource[vstd::to_underlying(bonus->targetSourceType)], bonus->val);
			break;
		default:
			break;
		}
	}

	for(const auto & bonus : *bonuses)
	{
		if((hero && newHorizonsLighthouse::departurePercent(*hero) && newHorizonsLighthouse::isLegacyCastleBonus(*bonus)) || !daySelector(bonus.get()))
			continue;
		if(bonus->valType != BonusValueType::INDEPENDENT_MIN
			&& bonus->valType != BonusValueType::INDEPENDENT_MAX)
			++result.nonIndependentBonusCount;

		const auto sourceIndex = vstd::to_underlying(bonus->source);
		const auto sourcePercentage = percentToSource[sourceIndex];
		int64_t rawValue = bonus->val;
		if(isLogisticsCoreEffectMovementBonus(*bonus))
			rawValue = applyPercentageRoundDown(rawValue,
				hero->getSkillSpecialtyCoreBonusPercent(SecondarySkill(SecondarySkill::LOGISTICS)));
		const int64_t modifiedValue = bonus->source == BonusSource::CREATURE_ABILITY
			? applyPercentageRoundUp(rawValue, sourcePercentage)
			: applyPercentageRoundDown(rawValue, sourcePercentage);

		switch(bonus->valType)
		{
		case BonusValueType::PERCENT_TO_BASE:
			result.percentageToBase = saturatingAdd(result.percentageToBase, modifiedValue);
			break;
		case BonusValueType::PERCENT_TO_ALL:
			result.percentageToAll = saturatingAdd(result.percentageToAll, modifiedValue);
			break;
		case BonusValueType::BASE_NUMBER:
			result.base = saturatingAdd(result.base, modifiedValue);
			break;
		case BonusValueType::ADDITIVE_VALUE:
			result.additive = saturatingAdd(result.additive, modifiedValue);
			break;
		case BonusValueType::INDEPENDENT_MAX:
			if(!result.independentMax || modifiedValue > *result.independentMax)
				result.independentMax = modifiedValue;
			break;
		case BonusValueType::INDEPENDENT_MIN:
			if(!result.independentMin || modifiedValue < *result.independentMin)
				result.independentMin = modifiedValue;
			break;
		default:
			// Source/target modifiers affect the values above but do not add a
			// value of their own, matching BonusList::totalValue.
			break;
		}
	}
	return result;
}

TConstBonusListPtr newHorizonsWaterBonuses(
	const TConstBonusListPtr & landBonuses, const TConstBonusListPtr & waterBonuses)
{
	const bool waterHasCoreLogistics = std::ranges::any_of(*waterBonuses,
		[](const std::shared_ptr<const Bonus> & bonus)
		{
			return isCoreLogisticsSourceBonus(*bonus);
		});
	if(waterHasCoreLogistics)
		return waterBonuses;

	TBonusListPtr merged = std::make_shared<BonusList>();
	for(const auto & bonus : *waterBonuses)
		merged->push_back(bonus);

	for(const auto & bonus : *landBonuses)
	{
		if(!isCoreLogisticsSourceBonus(*bonus))
			continue;

		auto seaBonus = std::make_shared<Bonus>(*bonus);
		seaBonus->subtype = BonusSubtypeID(BonusCustomSubtype::heroMovementSea);
		merged->push_back(std::move(seaBonus));
	}

	return merged;
}

int applyNewHorizonsMovementBounds(const int movement, const NewHorizonsMovementModifiers & modifiers)
{
	if(modifiers.nonIndependentBonusCount == 0)
	{
		// This is BonusList::totalValue's independent-only fallback.  A sole
		// INDEPENDENT_MIN is an upper-bound value but is returned directly, and
		// likewise a sole INDEPENDENT_MAX is returned directly as the lower-bound
		// value; do not clamp either one against the canonical 200-point base.
		if(modifiers.independentMin && !modifiers.independentMax)
			return static_cast<int>(std::clamp<int64_t>(*modifiers.independentMin, 0, std::numeric_limits<int>::max()));
		if(modifiers.independentMax && !modifiers.independentMin)
			return static_cast<int>(std::clamp<int64_t>(*modifiers.independentMax, 0, std::numeric_limits<int>::max()));
		if(modifiers.independentMin && modifiers.independentMax)
		{
			int64_t lower = *modifiers.independentMax;
			const int64_t upper = *modifiers.independentMin;
			if(upper < lower)
				lower = upper;
			const int64_t fallback = std::clamp<int64_t>(0, lower, upper);
			return static_cast<int>(std::clamp<int64_t>(fallback, 0, std::numeric_limits<int>::max()));
		}
	}

	int64_t lower = modifiers.independentMax.value_or(std::numeric_limits<int64_t>::min());
	int64_t upper = modifiers.independentMin.value_or(std::numeric_limits<int64_t>::max());
	if(modifiers.independentMin && modifiers.independentMax && upper < lower)
		lower = upper;

	const int64_t bounded = std::clamp<int64_t>(movement, lower, upper);
	return static_cast<int>(std::clamp<int64_t>(bounded, 0, std::numeric_limits<int>::max()));
}
}


namespace newHorizonsMovement
{
namespace
{
DailyMovementBreakdown evaluateModifiers(NewHorizonsMovementModifiers modifiers, const int64_t levelGrowth)
{
	modifiers.additive = saturatingAdd(modifiers.additive, levelGrowth);
	const int unboundedMovement = maximumDailyMovement(
		saturatingAdd(BASE_DAILY_MOVEMENT, modifiers.base), modifiers.percentageToBase,
		modifiers.percentageToAll, modifiers.additive);
	DailyMovementBreakdown result;
	result.baseAdjustment = modifiers.base;
	result.percentageToBase = modifiers.percentageToBase;
	result.percentageToAll = modifiers.percentageToAll;
	result.flat = modifiers.additive;
	result.levelGrowth = levelGrowth;
	// Preserve the BonusList meaning of independent minimum/maximum bounds.
	result.lowerBound = modifiers.independentMax;
	result.upperBound = modifiers.independentMin;
	result.limit = applyNewHorizonsMovementBounds(unboundedMovement, modifiers);
	return result;
}
}

DailyMovementLimits dailyMovementLimits(const CGHeroInstance & hero,
	const TConstBonusListPtr & landBonuses, const TConstBonusListPtr & seaBonuses, const int turn)
{
	const CSelector daySelector = Selector::days(turn);
	auto landModifiers = getNewHorizonsMovementModifiers(landBonuses, daySelector, &hero);
	const auto waterBonuses = newHorizonsWaterBonuses(landBonuses, seaBonuses);
	auto waterModifiers = getNewHorizonsMovementModifiers(waterBonuses, daySelector, &hero);
	// Sylvia's Forced March raises her whole daily maximum on the first day of each week.
	if(const int forcedMarch = newHorizonsHeroes::forcedMarchMovementPercent(hero, turn))
	{
		landModifiers.percentageToAll = saturatingAdd(landModifiers.percentageToAll, forcedMarch);
		waterModifiers.percentageToAll = saturatingAdd(waterModifiers.percentageToAll, forcedMarch);
	}
	if(hero.hasActivePerk("new-horizons:logistics", "new-horizons:logistics.navigation"))
		waterModifiers.percentageToBase = saturatingAdd(waterModifiers.percentageToBase, 25);

	const auto growth = hero.compiledDevelopment()->levelMovementBonus(hero.level);
	DailyMovementLimits result;
	result.land = evaluateModifiers(landModifiers, growth);
	result.sea = evaluateModifiers(waterModifiers, growth);
	if(!newHorizonsLighthouse::hasDepartureBonus(hero, turn))
		waterModifiers.percentageToBase = saturatingAdd(waterModifiers.percentageToBase,
			newHorizonsLighthouse::departurePercent(hero));
	result.lighthouseDeparture = evaluateModifiers(waterModifiers, growth);
	return result;
}

std::optional<DailyMovementBreakdown> currentDailyMovementBreakdown(const CGHeroInstance * hero, const bool water)
{
	if(!hero || !hero->usesNewHorizonsMovement())
		return std::nullopt;
	static const CSelector landSelector = Selector::typeSubtype(BonusType::MOVEMENT, BonusCustomSubtype::heroMovementLand);
	static const CSelector waterSelector = Selector::typeSubtype(BonusType::MOVEMENT, BonusCustomSubtype::heroMovementSea);
	const auto limits = dailyMovementLimits(*hero, hero->getBonuses(landSelector), hero->getBonuses(waterSelector), 0);
	return water ? limits.sea : limits.land;
}
}
