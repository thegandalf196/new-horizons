/*
 * NewHorizonsSchoolSpellcraft.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsSchoolSpellcraft.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsRuleValidation.h"
#include "CHero.h"
#include "CHeroHandler.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../spells/CSpell.h"
#include "../../spells/NewHorizonsMagic.h"
#include "../../spells/NewHorizonsMagicInternal.h"
#include "../../spells/NewHorizonsSpellAvailability.h"
#include <array>

namespace newHorizonsHeroes
{
namespace
{
constexpr std::array<std::string_view, 4> keys{"overcaster", "darkSavant", "primalFormula", "mireHexer"};
constexpr std::array<std::string_view, 4> identities{"core:zydar", "core:sandro", "core:gird", "core:styg"};
constexpr std::array<std::array<std::string_view, 2>, 4> pairs{{
	{"new-horizons:chaos", "new-horizons:havoc"}, {"new-horizons:shadow", "new-horizons:sorcery"},
	{"new-horizons:chaos", "new-horizons:nature"}, {"new-horizons:nature", "new-horizons:shadow"}}};

std::optional<size_t> identityIndex(std::string_view identity)
{
	const auto found = std::ranges::find(identities, identity);
	return found == identities.end() ? std::nullopt : std::optional<size_t>(found - identities.begin());
}
}

void validateSchoolSpellcraftSerialization(const JsonNode & development, bool supported)
{
	if(!development.isStruct() || !development.Struct().contains("schoolSpellcraftSpecialties"))
		return;
	if(!supported)
		throw std::runtime_error("School Spellcraft specialties require the new save format");
	const auto & block = development["schoolSpellcraftSpecialties"];
	detail::fields(block, {"version", "overcaster", "darkSavant", "primalFormula", "mireHexer"});
	detail::require(detail::integer(block["version"], 1, 1), "school Spellcraft specialty version");
	for(const auto key : keys)
	{
		const auto & rule = block[std::string(key)];
		detail::fields(rule, {"percentPerLevel", "maximumPercent"});
		detail::require(detail::integer(rule["percentPerLevel"], 0, 1000), "school specialty percentage per level");
		detail::require(detail::integer(rule["maximumPercent"], 0, 200), "school specialty maximum percentage");
	}
}

std::optional<WarlocksFormulaRules> schoolSpellcraftRules(const JsonNode & development, SchoolSpellcraftKind kind)
{
	if(!usesRules(development) || !development.isStruct()
		|| !development.Struct().contains("schoolSpellcraftSpecialties"))
		return std::nullopt;
	validateSchoolSpellcraftSerialization(development, true);
	const auto & rule = development["schoolSpellcraftSpecialties"][std::string(keys.at(static_cast<size_t>(kind)))];
	return WarlocksFormulaRules{static_cast<int>(rule["percentPerLevel"].Integer()),
		static_cast<int>(rule["maximumPercent"].Integer())};
}

SpellcraftCastContext captureSpellcraftContext(const JsonNode & magic, const CGHeroInstance * hero,
	SpellID spell, bool originalHero)
{
	SpellcraftCastContext result{originalHero, std::nullopt};
	if(!originalHero || !hero)
		return result;
	const auto heroType = hero->getHeroTypeID().getNum();
	if(heroType < 0 || !LIBRARY || !LIBRARY->heroh
		|| static_cast<size_t>(heroType) >= LIBRARY->heroh->objects.size()
		|| !LIBRARY->heroh->objects[heroType])
		return result; // Generic hero descriptors retain ordinary coefficient calculation.
	const auto & identity = LIBRARY->heroh->objects[heroType]->getJsonKey();
	const auto index = identityIndex(identity);
	const bool malekith = identity == "core:malekith";
	if(!index && !malekith)
		return result;
	std::optional<WarlocksFormulaRules> rule;
	if(index)
		rule = schoolSpellcraftRules(hero->getPrimaryGrowthRules(), static_cast<SchoolSpellcraftKind>(*index));
	else if(const auto policy = individualSpecialtyRules(hero->getPrimaryGrowthRules()))
		rule = policy->warlocksFormula;
	if(!rule || !newHorizonsMagic::rulesActive(magic)
		|| magic["rulesetVersion"].Integer() != newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		|| !spell.hasValue() || !spell.toSpell() || !spell.toSpell()->isCommonHeroSpell()
		|| !newHorizonsMagic::spellAllowedBySavedRoster(magic, spell)
		|| newHorizonsMagic::isAdventureSpell(magic, spell))
		return result;
	SpellcraftSpecialtyFactors factors;
	factors.schoolPercent = newHorizonsMagic::spellPowerCoefficientPercent(magic, hero, spell);
	factors.ordinaryEfficiencyPercent = newHorizonsMagic::spellcraftEfficiencyPercent(
		magic, newHorizonsMagic::detail::registeredSpellcraftRank(hero));
	const auto increase = cappedSpecialtyContribution(hero->level, rule->percentPerLevel, rule->maximumPercent);
	factors.enhancedEfficiencyBasisPoints = strengthenedSpellcraftBasisPoints(factors.ordinaryEfficiencyPercent, increase);
	if(malekith)
	{
		if(newHorizonsMagic::isImmediateSpecialtyDamageSpell(spell))
			factors.scope = SpellcraftSpecialtyScope::DIRECT_DAMAGE;
	}
	else
	{
		const auto & schools = magic["spells"][spell.toSpell()->getJsonKey()]["schools"].Vector();
		const bool matches = std::ranges::any_of(schools, [&index](const JsonNode & school)
		{
			return std::ranges::find(pairs[*index], school.String()) != pairs[*index].end();
		});
		if(matches)
			factors.scope = *index == 0 ? SpellcraftSpecialtyScope::DAMAGE : SpellcraftSpecialtyScope::NUMERICAL;
	}
	result.factors = factors;
	return result;
}

int composeSpellcraftCoefficient(const SpellcraftSpecialtyFactors & captured, SpellcraftComponent component,
	int additionalSpellPowerComponentPercent)
{
	if(additionalSpellPowerComponentPercent < 0 || additionalSpellPowerComponentPercent > 200)
		throw std::invalid_argument("Invalid additional Spell Power component percentage");
	const bool enhance = captured.scope == SpellcraftSpecialtyScope::NUMERICAL
		|| (captured.scope == SpellcraftSpecialtyScope::DAMAGE && component != SpellcraftComponent::NUMERICAL)
		|| (captured.scope == SpellcraftSpecialtyScope::DIRECT_DAMAGE && component == SpellcraftComponent::DIRECT_DAMAGE);
	const int64_t efficiency = enhance ? captured.enhancedEfficiencyBasisPoints : captured.ordinaryEfficiencyPercent * 100;
	return static_cast<int>(captured.schoolPercent * efficiency * (100 + additionalSpellPowerComponentPercent) / 10000);
}


}
