/*
 * NewHorizonsCureHealing.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsCureHealing.h"
#include "ISpellMechanics.h"
#include "../entities/hero/NewHorizonsHeroRules.h"
#include "../mapObjects/CGHeroInstance.h"
namespace newHorizonsMagic
{
int64_t cureHealingBudget(const CGHeroInstance * hero, int32_t rawSpellPower,
	int32_t coefficientBasisPoints, int32_t warcastingPercent, int32_t empowerPercent)
{
	const int specialtyPercent = hero ? hero->getNonDamageSpellSpecialtyBonusPercent(SpellID::CURE) : 0;
	auto powerHealing = spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
		3LL * rawSpellPower, 2, coefficientBasisPoints, warcastingPercent, empowerPercent, specialtyPercent);
	if(const auto specialty = hero
		? hero->getDruidSpecialtyRules(newHorizonsHeroes::DruidSpecialtyKind::RESTORATIVE_SORCERESS) : std::nullopt)
		powerHealing += newHorizonsHeroes::druidSpecialtyExtra(powerHealing,
			newHorizonsHeroes::druidSpecialtyPercent(hero->level, specialty->restorativeSorceress));
	// Rion's distinct full-pool rider preserves each existing component's floor.
	const auto fixedHealing = newHorizonsHeroes::fieldPhysicianHealing(25, hero);
	powerHealing = newHorizonsHeroes::fieldPhysicianHealing(powerHealing, hero);
	if(hero && hero->hasActivePerk("new-horizons:lightMagic", "new-horizons:lightMagic.healer"))
		powerHealing = powerHealing * 120 / 100;
	return fixedHealing + powerHealing;
}
}
