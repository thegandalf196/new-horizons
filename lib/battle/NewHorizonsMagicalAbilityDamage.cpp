/*
 * NewHorizonsMagicalAbilityDamage.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsMagicalAbilityDamage.h"

#include "CBattleInfoCallback.h"
#include "Unit.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusSelector.h"
#include "../spells/MagicalDamageReduction.h"

namespace newHorizonsMagicalAbilityDamage
{
int64_t adjustDamage(const CBattleInfoCallback & battle, const battle::Unit & target, int64_t rawDamage)
{
	if(rawDamage <= 0)
		return 0;

	if(!battle.battleUsesNewHorizonsMultiplicativeMDR())
		return rawDamage;

	std::vector<int> reductionSourcesBasisPoints;
	const auto anySchoolReductions = target.getBonuses(
		Selector::typeSubtype(BonusType::SPELL_DAMAGE_REDUCTION, BonusSubtypeID(SpellSchool::ANY)),
		"new_horizons_ability_damage_any_school_reduction");
	for(const auto & bonus : *anySchoolReductions)
	{
		const int reduction = std::clamp(bonus->val, 0, 100);
		if(reduction > 0)
			reductionSourcesBasisPoints.push_back(reduction * 100);
	}

	const auto anySchoolReductionsBasisPoints = target.getBonuses(
		Selector::typeSubtype(BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS,
			BonusSubtypeID(SpellSchool::ANY)),
		"new_horizons_ability_damage_any_school_reduction_basis_points");
	for(const auto & bonus : *anySchoolReductionsBasisPoints)
	{
		const int reduction = std::clamp(bonus->val, 0, 10'000);
		if(reduction > 0)
			reductionSourcesBasisPoints.push_back(reduction);
	}

	const int holdTheLineReduction = std::clamp(
		battle.battleGetHoldTheLineMagicalReductionBasisPoints(&target), 0, 10'000);
	if(holdTheLineReduction > 0)
		reductionSourcesBasisPoints.push_back(holdTheLineReduction);

	const int perkReduction = std::clamp(
		battle.battleGetPerkMagicalReductionBasisPoints(&target), 0, 10'000);
	if(perkReduction > 0)
		reductionSourcesBasisPoints.push_back(perkReduction);

	return spells::calculateMagicalDamageReductionBasisPoints(
		rawDamage, reductionSourcesBasisPoints, 0).damageWithoutPenetration;
}
}
