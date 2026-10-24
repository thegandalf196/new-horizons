/*
 * NewHorizonsQuicksand.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsMagicInternal.h"
#include "NewHorizonsSpellAvailability.h"
#include "../mapObjects/CGHeroInstance.h"
#include <limits>
#include <stdexcept>

namespace newHorizonsMagic
{
namespace detail
{
int32_t quicksandSpellPowerStep(const JsonNode & rules, int32_t casterPowerDivisor)
{
	if(casterPowerDivisor <= 0)
		throw std::invalid_argument("Invalid Quicksand caster power divisor");
	const auto & row = rules["spells"]["core:quicksand"];
	if(row.Struct().contains("rawSpellPowerPerPatch"))
	{
		const auto & step = row["rawSpellPowerPerPatch"];
		if(step.getType() != JsonNode::JsonType::DATA_INTEGER
			|| step.Integer() < 1 || step.Integer() > 1000000)
			throw std::runtime_error("Quicksand rawSpellPowerPerPatch requires a positive integer step");
		return static_cast<int32_t>(step.Integer());
	}
	if(casterPowerDivisor > std::numeric_limits<int32_t>::max() / QUICKSAND_SPELL_POWER_PER_PATCH_V3)
		throw std::invalid_argument("Invalid legacy Quicksand power divisor");
	return casterPowerDivisor * QUICKSAND_SPELL_POWER_PER_PATCH_V3;
}

void validateQuicksandSpellRow(const JsonNode & rules, std::string_view spellIdentity, const JsonNode & row)
{
	const bool currentQuicksand = rules["rulesetVersion"].Integer()
		== SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION && spellIdentity == "core:quicksand";
	if(row.Struct().contains("selectedPlacement")
		&& (!currentQuicksand || !row["selectedPlacement"].isBool() || !row["selectedPlacement"].Bool()))
		throw std::runtime_error("selectedPlacement is only true for core:quicksand in magic rules v3");
	if(row.Struct().contains("rawSpellPowerPerPatch"))
	{
		if(!currentQuicksand)
			throw std::runtime_error("rawSpellPowerPerPatch requires core:quicksand in magic rules v3");
		(void)quicksandSpellPowerStep(rules, 1);
	}
}
}

std::optional<int> quicksandPatchCount(const JsonNode & rules, const CGHeroInstance * hero,
	const SpellID spell, const int32_t spellPower, const int32_t spellPowerDivisor,
	const int warcastingBonusPercent, const int empowerBonusPercent,
	const int additionalSpellPowerComponentPercent,
	const std::optional<int> coefficientOverride)
{
	if(!rulesActive(rules)
		|| rules["rulesetVersion"].Integer() != SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		|| spell != SpellID(SpellID::QUICKSAND)
		|| !spellAllowedBySavedRoster(rules, spell))
		return std::nullopt;

	if(spellPower < 0)
		throw std::invalid_argument("Invalid Quicksand Spell Power inputs");

	const int coefficientBasisPoints = coefficientOverride ? *coefficientOverride : spellPowerCoefficientBasisPoints(
		rules, hero, spell, additionalSpellPowerComponentPercent);
	const int32_t divisor = detail::quicksandSpellPowerStep(rules, spellPowerDivisor);
	const int64_t spellPowerPatches = spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
		spellPower, divisor, coefficientBasisPoints, warcastingBonusPercent, empowerBonusPercent);
	const int baseCount = QUICKSAND_BASE_PATCH_COUNT_V3 + static_cast<int>(std::min<int64_t>(
		QUICKSAND_MAX_PATCH_COUNT_V3 - QUICKSAND_BASE_PATCH_COUNT_V3, spellPowerPatches));
	// The additional patch is independent of Spell Power and follows the
	// ordinary cap, allowing six patches without changing unselected heroes.
	const bool mireShaper = hero && hero->hasActivePerk(
		std::string(NATURE_MAGIC_SKILL), std::string(detail::NATURE_MIRE_SHAPER));
	return baseCount + (mireShaper ? 1 : 0);
}

}
