/*
 * NewHorizonsDamagePower.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "ISpellMechanics.h"
#include "CSpell.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsMagicInternal.h"
#include "NewHorizonsSpellAvailability.h"
#include "../entities/hero/NewHorizonsIndividualSpecialties.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/BattleInfo.h"
#include <array>
#include <limits>

namespace newHorizonsMagic
{
int directDamageSpellPowerCoefficientBasisPoints(const JsonNode & rules, const CGHeroInstance * hero,
	SpellID spell, int additionalSpellPowerComponentPercent)
{
	const int ordinary = spellPowerCoefficientBasisPoints(rules, hero, spell, additionalSpellPowerComponentPercent);
	const int specialty = newHorizonsHeroes::warlocksFormulaPercent(hero);
	if(!specialty || !rulesActive(rules)
		|| rules["rulesetVersion"].Integer() != SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		|| !spell.hasValue() || !spell.toSpell() || !spell.toSpell()->isCommonHeroSpell()
		|| !spellAllowedBySavedRoster(rules, spell) || isAdventureSpell(rules, spell))
		return ordinary;

	// Component whitelist: these have an immediate damage producer. The
	// directDamage JSON label also describes stored Fire Wall/Mine damage,
	// which is deliberately not this contract. Vines' slow/duration still
	// uses the generic coefficient; its immediate Damage uses this one.
	constexpr std::array<std::string_view, 20> immediate{
		"core:armageddon", "core:chainLightning", "new-horizons:masterChainLightning",
		"new-horizons:soulReaper", "new-horizons:handOfFate", "new-horizons:holyWrath",
		"new-horizons:vengefulVines", "new-horizons:lifeDrain", "new-horizons:stormOfDaggers",
		"new-horizons:disintegrate", "core:fireball", "core:frostRing", "core:iceBolt",
		"core:inferno", "core:lightningBolt", "core:magicArrow", "core:meteorShower",
		"core:implosion", "core:earthquake", "new-horizons:naturesWrath"};
	if(std::ranges::find(immediate, spell.toSpell()->getJsonKey()) == immediate.end())
		return ordinary;
	const int efficiency = spellcraftEfficiencyPercent(rules, detail::registeredSpellcraftRank(hero));
	if(efficiency == 100)
		return ordinary;
	const int64_t strengthened = newHorizonsHeroes::strengthenedSpellcraftBasisPoints(efficiency, specialty);
	return static_cast<int>(spellPowerCoefficientPercent(rules, hero, spell)
		* strengthened * (100 + additionalSpellPowerComponentPercent) / 10000);
}
}

namespace spells
{
namespace
{
int64_t checkedStormMultiply(int64_t left, int64_t right)
{
	if(left < 0 || right < 0
		|| (right != 0 && left > std::numeric_limits<int64_t>::max() / right))
		throw std::overflow_error("Storm of Daggers damage pool overflows");
	return left * right;
}

int64_t stormOfDaggersRoundedDamage(int64_t base, int64_t powerCoefficient, int32_t effectPower,
	int32_t powerDivisor, int32_t spellPowerCoefficientBasisPoints, int32_t warcastingBonusPercent,
	int32_t empowerSpellBonusPercent, int32_t selectedTargetCount, bool total)
{
	if(base < 0 || powerCoefficient < 0 || effectPower < 0 || powerDivisor <= 0
		|| spellPowerCoefficientBasisPoints < 0 || spellPowerCoefficientBasisPoints > 100000
		|| warcastingBonusPercent < 0 || warcastingBonusPercent > 1000
		|| empowerSpellBonusPercent < 0 || empowerSpellBonusPercent > 1000
		|| selectedTargetCount < 1
		|| selectedTargetCount > newHorizonsMagic::STORM_OF_DAGGERS_MAX_TARGETS)
		return 0;

	// Keep the authored fixed base and the full rational Spell Power component
	// together until the final displayed-total or equal-target rounding. The
	// generic direct-damage path truncates at the pool boundary, which differs
	// from the Storm table at positive half values (for example, SP=1 yields 47).
	const int64_t powerDenominator = checkedStormMultiply(
		checkedStormMultiply(powerDivisor, 1000000), 100);
	int64_t spellPowerNumerator = checkedStormMultiply(powerCoefficient, effectPower);
	spellPowerNumerator = checkedStormMultiply(spellPowerNumerator, spellPowerCoefficientBasisPoints);
	spellPowerNumerator = checkedStormMultiply(spellPowerNumerator,
		100LL + warcastingBonusPercent);
	spellPowerNumerator = checkedStormMultiply(spellPowerNumerator,
		100LL + empowerSpellBonusPercent);
	const int64_t fixedNumerator = checkedStormMultiply(base, powerDenominator);
	if(spellPowerNumerator > std::numeric_limits<int64_t>::max() - fixedNumerator)
		throw std::overflow_error("Storm of Daggers damage pool overflows");
	const int64_t poolNumerator = fixedNumerator + spellPowerNumerator;

	const int64_t multiplierPercent = 100
		+ newHorizonsMagic::STORM_OF_DAGGERS_EXTRA_TARGET_DAMAGE_PERCENT * (selectedTargetCount - 1);
	const int64_t finalDivisor = (total ? 100LL : 100LL * selectedTargetCount);
	const int64_t denominator = checkedStormMultiply(powerDenominator, finalDivisor);
	const int64_t scaledNumerator = checkedStormMultiply(poolNumerator, multiplierPercent);
	if(scaledNumerator > std::numeric_limits<int64_t>::max() - denominator / 2)
		throw std::overflow_error("Storm of Daggers damage pool overflows");

	// The authored reference values use ordinary positive half-up rounding for
	// both the displayed total and the equal per-stack share, without first
	// truncating a fractional Spell Power term at the pool boundary.
	return (scaledNumerator + denominator / 2) / denominator;
}

}

int32_t Mechanics::getSpellPowerCoefficientBasisPoints() const
{
	const auto * battleCallback = battle();
	const auto * battleState = battleCallback ? battleCallback->getBattle() : nullptr;
	if(!battleState)
		return 10000;

	return newHorizonsMagic::spellPowerCoefficientBasisPoints(
		battleState->getMagicRules(), getHeroCaster(), getSpellId(), getCastSpellPowerComponentBonusPercent());
}

int64_t BaseMechanics::getStormOfDaggersDamagePerTarget(int32_t selectedTargetCount) const
{
	const auto * battleState = cb ? cb->getBattle() : nullptr;
	if(!isNewHorizonsStormOfDaggers() || !battleState)
		return 0;

	const auto formula = newHorizonsMagic::spellDirectDamage(
		battleState->getMagicRules(), owner->getJsonKey());
	if(!formula)
		return 0;
	return stormOfDaggersRoundedDamage(formula->base, formula->powerCoefficient,
		getEffectPower(), getEffectPowerDivisor(), getDirectDamageSpellPowerCoefficientBasisPoints(),
		getWarcastingBonusPercent(), getEmpowerSpellBonusPercent(), selectedTargetCount, false);
}

int64_t BaseMechanics::getStormOfDaggersTotalDamage(int32_t selectedTargetCount) const
{
	const auto * battleState = cb ? cb->getBattle() : nullptr;
	if(!isNewHorizonsStormOfDaggers() || !battleState)
		return 0;

	const auto formula = newHorizonsMagic::spellDirectDamage(
		battleState->getMagicRules(), owner->getJsonKey());
	if(!formula)
		return 0;
	return stormOfDaggersRoundedDamage(formula->base, formula->powerCoefficient,
		getEffectPower(), getEffectPowerDivisor(), getDirectDamageSpellPowerCoefficientBasisPoints(),
		getWarcastingBonusPercent(), getEmpowerSpellBonusPercent(), selectedTargetCount, true);
}

int32_t BaseMechanics::getDirectDamageSpellPowerCoefficientBasisPoints() const
{
	if(mode != Mode::HERO)
		return getSpellPowerCoefficientBasisPoints();
	const auto * battleCallback = battle();
	const auto * state = battleCallback ? battleCallback->getBattle() : nullptr;
	return state ? newHorizonsMagic::directDamageSpellPowerCoefficientBasisPoints(
		state->getMagicRules(), getHeroCaster(), getSpellId(), getCastSpellPowerComponentBonusPercent()) : 10000;
}
}
