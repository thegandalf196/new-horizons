/*
	* NewHorizonsSpellcraftPower.cpp, part of VCMI / New Horizons
	* License: GNU General Public License v2.0 or later; see license.txt
	*/
#include "StdInc.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsMagicInternal.h"
#include "NewHorizonsSpellAvailability.h"
#include "CSpell.h"
#include "../entities/hero/NewHorizonsSchoolSpellcraft.h"
#include <array>

namespace newHorizonsMagic
{
using namespace detail;
int ordinarySpellPowerCoefficientBasisPoints(const JsonNode & rules, const CGHeroInstance * hero, SpellID spell,
	const int additionalSpellPowerComponentPercent)
{
	// Legal first-cast/faction/response/concentration products can exceed +100%.
	if(additionalSpellPowerComponentPercent < 0 || additionalSpellPowerComponentPercent > 200)
		throw std::invalid_argument("Invalid additional Spell Power component percentage");

	if(!hero || !rulesActive(rules)
		|| rules["rulesetVersion"].Float() != SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		|| !spell.hasValue() || !spell.toSpell() || !spell.toSpell()->isCommonHeroSpell()
		|| !spellAllowedBySavedRoster(rules, spell) || isAdventureSpell(rules, spell))
		return SPELL_POWER_COEFFICIENT_BASIS_POINTS;

	const int schoolCoefficientPercent = spellPowerCoefficientPercent(rules, hero, spell);
	const int spellcraftCoefficientPercent = spellcraftEfficiencyPercent(rules, registeredSpellcraftRank(hero));
	const int combinedCoefficientBasisPoints = schoolCoefficientPercent * spellcraftCoefficientPercent;
	return combinedCoefficientBasisPoints * (100 + additionalSpellPowerComponentPercent) / 100;
}

bool isImmediateSpecialtyDamageSpell(SpellID spell)
{
	constexpr std::array<std::string_view, 20> immediate{
		"core:armageddon", "core:chainLightning", "new-horizons:masterChainLightning",
		"new-horizons:soulReaper", "new-horizons:handOfFate", "new-horizons:holyWrath",
		"new-horizons:vengefulVines", "new-horizons:lifeDrain", "new-horizons:stormOfDaggers",
		"new-horizons:disintegrate", "core:fireball", "core:frostRing", "core:iceBolt",
		"core:inferno", "core:lightningBolt", "core:magicArrow", "core:meteorShower",
		"core:implosion", "core:earthquake", "new-horizons:naturesWrath"};
	return spell.hasValue() && spell.toSpell()
		&& std::ranges::find(immediate, spell.toSpell()->getJsonKey()) != immediate.end();
}

namespace
{
int coefficient(const JsonNode & rules, const CGHeroInstance * hero, SpellID spell,
	int additional, newHorizonsHeroes::SpellcraftComponent component)
{
	const auto context = newHorizonsHeroes::captureSpellcraftContext(rules, hero, spell, true);
	return context.factors ? newHorizonsHeroes::composeSpellcraftCoefficient(*context.factors, component, additional)
		: ordinarySpellPowerCoefficientBasisPoints(rules, hero, spell, additional);
}
}

int spellPowerCoefficientBasisPoints(const JsonNode & rules, const CGHeroInstance * hero,
	SpellID spell, int additional)
{
	return coefficient(rules, hero, spell, additional, newHorizonsHeroes::SpellcraftComponent::NUMERICAL);
}

int damageSpellPowerCoefficientBasisPoints(const JsonNode & rules, const CGHeroInstance * hero,
	SpellID spell, int additional)
{
	return coefficient(rules, hero, spell, additional, newHorizonsHeroes::SpellcraftComponent::DAMAGE);
}

int directDamageSpellPowerCoefficientBasisPoints(const JsonNode & rules, const CGHeroInstance * hero,
	SpellID spell, int additional)
{
	return coefficient(rules, hero, spell, additional, newHorizonsHeroes::SpellcraftComponent::DIRECT_DAMAGE);
}
}
