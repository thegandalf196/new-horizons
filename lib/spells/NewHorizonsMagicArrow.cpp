/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsMagicArrow.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsSpellAvailability.h"
#include "CSpell.h"
#include "../mapObjects/CGHeroInstance.h"
#include <array>
#include <limits>
#include <stdexcept>

namespace newHorizonsMagic
{
namespace
{
constexpr std::array<int32_t, 7> SURCHARGES{0, 8, 26, 46, 71, 101, 136};

const JsonNode & arrowRow(const JsonNode & rules)
{
	return rules["spells"]["core:magicArrow"];
}

bool integer(const JsonNode & value, int32_t expected)
{
	return value.getType() == JsonNode::JsonType::DATA_INTEGER && value.Integer() == expected;
}

bool sorcery(const JsonNode & row)
{
	return row["schools"].isVector() && std::any_of(row["schools"].Vector().begin(),
		row["schools"].Vector().end(), [](const JsonNode & school)
		{ return school.isString() && school.String() == "new-horizons:sorcery"; });
}

bool validModifiers(MagicArrowOverchargeModifiers modifiers)
{
	return modifiers.maximumBonus == 0 || modifiers.maximumBonus == 1;
}
}

bool requiresMagicArrowChargeRules(const JsonNode & rules)
{
	const auto & row = arrowRow(rules);
	return row.isStruct() && row.Struct().contains("magicArrow");
}

void validateMagicArrowSpellRow(const JsonNode & rules, std::string_view identity, const JsonNode & row)
{
	if(identity != "core:magicArrow" && (!row.isStruct() || !row.Struct().contains("magicArrow")))
		return;
	if(!row.isStruct())
		throw std::runtime_error("Magic Arrow requires a saved spell row");
	if(!row.Struct().contains("magicArrow")
		&& rules["rulesetVersion"].Integer() < SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
		return; // Earlier magic schemas do not contain this spell-specific profile.
	const auto & marker = row["magicArrow"];
	const auto formula = directDamageFormula(row, SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION);
	if(identity != "core:magicArrow" || !integer(rules["rulesetVersion"], SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
		|| !marker.isStruct() || marker.Struct().size() != 1 || !integer(marker["version"], 1)
		|| !sorcery(row) || !integer(row["level"], 1) || !formula || formula->base != 20
		|| formula->powerCoefficient != 10 || !row["costs"].isVector() || row["costs"].Vector().size() != 4
		|| !std::all_of(row["costs"].Vector().begin(), row["costs"].Vector().end(),
			[](const JsonNode & cost) { return integer(cost, 4); }))
		throw std::runtime_error("Unsupported New Horizons Magic Arrow charge profile");
}

void validateMagicArrowChargeSerialization(const JsonNode & rules, bool supported)
{
	if(!requiresMagicArrowChargeRules(rules)
		&& !(integer(rules["rulesetVersion"], SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
			&& arrowRow(rules).isStruct()))
		return;
	if(!supported)
		throw std::runtime_error("Magic Arrow charge rules require the native rules redesign save format");
	validateMagicArrowSpellRow(rules, "core:magicArrow", arrowRow(rules));
}

bool magicArrowOverchargeEnabled(const JsonNode & rules, SpellID spell)
{
	if(spell != SpellID(SpellID::MAGIC_ARROW) || !rulesActive(rules)
		|| !requiresMagicArrowChargeRules(rules)
		|| rules["rulesetVersion"].Integer() != SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		|| !spellAllowedBySavedRoster(rules, spell))
		return false;
	return spellDirectDamage(rules, "core:magicArrow").has_value() && sorcery(arrowRow(rules));
}

MagicArrowOverchargeModifiers magicArrowOverchargeModifiers(const CGHeroInstance * hero)
{
	MagicArrowOverchargeModifiers result;
	if(hero && hero->hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.overcharger"))
	{
		result.maximumBonus = 1;
	}
	return result;
}

int magicArrowMaxOvercharge(const JsonNode & rules, SpellID spell, int32_t rawSpellPower,
	MagicArrowOverchargeModifiers modifiers)
{
	if(!magicArrowOverchargeEnabled(rules, spell))
		return 0;
	if(rawSpellPower < 0 || !validModifiers(modifiers))
		throw std::invalid_argument("Invalid Magic Arrow charge capacity");
	return std::min(5, 2 + rawSpellPower / 50) + modifiers.maximumBonus;
}

bool magicArrowSelectionValid(const JsonNode & rules, SpellID spell,
	const CGHeroInstance * hero, int32_t effectPower, int overcharge, bool manualHeroRoute)
{
	if(overcharge < 0 || (overcharge > 0 && (!hero || !manualHeroRoute)))
		return false;
	if(!magicArrowOverchargeEnabled(rules, spell))
		return overcharge == 0;
	const int32_t rawPower = hero ? hero->getPrimSkillLevel(PrimarySkill::SPELL_POWER) : effectPower;
	return rawPower >= 0 && overcharge <= magicArrowMaxOvercharge(rules, spell, rawPower,
		magicArrowOverchargeModifiers(hero));
}

std::optional<int32_t> magicArrowManaCost(const JsonNode & rules, SpellID spell,
	int32_t ordinaryCost, int overcharge)
{
	if(ordinaryCost < 0 || overcharge < 0 || overcharge >= static_cast<int>(SURCHARGES.size()))
		return std::nullopt;
	if(!magicArrowOverchargeEnabled(rules, spell))
		return overcharge == 0 ? std::make_optional(ordinaryCost) : std::nullopt;
	const int32_t surcharge = SURCHARGES[overcharge];
	if(ordinaryCost > std::numeric_limits<int32_t>::max() - surcharge)
		return std::nullopt;
	return ordinaryCost + surcharge;
}

int magicArrowMaximumAffordableOvercharge(const JsonNode & rules, SpellID spell,
	int32_t rawSpellPower, int32_t ordinaryCost, int32_t availableMana,
	MagicArrowOverchargeModifiers modifiers)
{
	for(int charge = magicArrowMaxOvercharge(rules, spell, rawSpellPower, modifiers); charge >= 0; --charge)
		if(const auto cost = magicArrowManaCost(rules, spell, ordinaryCost, charge); cost && *cost <= availableMana)
			return charge;
	return -1;
}

std::optional<int64_t> magicArrowDamageBasisPoints(const JsonNode & rules, SpellID spell,
	int32_t spellPower, int32_t divisor, int overcharge, MagicArrowOverchargeModifiers modifiers,
	int coefficientBasisPoints, int empowerPercent, int damageSpecialtyPercent, int32_t capPower)
{
	if(!magicArrowOverchargeEnabled(rules, spell) || spellPower < 0 || divisor <= 0
		|| coefficientBasisPoints < 0 || coefficientBasisPoints > 100000 || empowerPercent < 0
		|| empowerPercent > 1000 || (damageSpecialtyPercent != 0 && damageSpecialtyPercent != 15)
		|| !validModifiers(modifiers))
		return std::nullopt;
	const int maximum = magicArrowMaxOvercharge(rules, spell, capPower < 0 ? spellPower : capPower, modifiers);
	if(overcharge < 0 || overcharge > maximum)
		return std::nullopt;
	// P is the raw rating. School/Spellcraft and accepted component bonuses
	// scale that term once, while the fixed twenty never enters a multiplier.
	return DirectDamageFormula{20, 1 + overcharge}.evaluateBasisPoints(spellPower, 1,
		coefficientBasisPoints, empowerPercent, damageSpecialtyPercent);
}

std::optional<int64_t> magicArrowDamage(const JsonNode & rules, SpellID spell,
	int32_t spellPower, int32_t divisor, int overcharge, MagicArrowOverchargeModifiers modifiers,
	int coefficientPercent, int empowerPercent)
{
	if(coefficientPercent < 0 || coefficientPercent > 1000)
		return std::nullopt;
	return magicArrowDamageBasisPoints(rules, spell, spellPower, divisor, overcharge, modifiers,
		coefficientPercent * 100, empowerPercent);
}
}
