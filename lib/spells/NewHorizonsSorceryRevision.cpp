/*
 * NewHorizonsSorceryRevision.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NewHorizonsSorceryRevision.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsSorcery.h"
#include "ISpellMechanics.h"
#include <cmath>

namespace newHorizonsSorceryRevision
{
namespace
{
const JsonNode * profile(const JsonNode & rules, const std::string & spell, const std::string & clause)
{
	const auto & row = rules["spells"][spell];
	if(!row.isStruct() || !row.Struct().contains(clause))
		return nullptr;
	const auto & result = row[clause];
	if(rules["rulesetVersion"].getType() != JsonNode::JsonType::DATA_INTEGER
		|| rules["rulesetVersion"].Integer() != newHorizonsMagic::CURRENT_RULESET_VERSION
		|| !result.isStruct())
		throw std::runtime_error("Sorcery revision requires a captured v3 profile");
	return &result;
}
int32_t integer(const JsonNode & value, int32_t minimum, int32_t maximum)
{
	if(value.getType() != JsonNode::JsonType::DATA_INTEGER || value.Integer() < minimum || value.Integer() > maximum)
		throw std::runtime_error("Invalid captured Sorcery revision integer");
	return static_cast<int32_t>(value.Integer());
}
void version(const JsonNode & value, size_t fields)
{
	if(value.Struct().size() != fields || integer(value["version"], 1, 1) != 1)
		throw std::runtime_error("Unsupported captured Sorcery revision profile");
}
void validateArmor(const JsonNode & rules)
{
	if(const auto * value = profile(rules, "new-horizons:summonLivingArmor", "livingArmor"))
	{
		version(*value, 4);
		integer((*value)["baseHitPoints"], 1, 1000000);
		integer((*value)["spellPowerCoefficient"], 0, 10000);
		integer((*value)["duration"], 1, 1000);
	}
}
}
int32_t slowBaseReduction(int32_t power, int32_t coefficient, int32_t warcasting, int32_t empower)
{
	const auto component = spells::scaleSpellPowerComponentWithCoefficientBasisPoints(power, 5, coefficient, warcasting, empower);
	return static_cast<int32_t>(std::min<int64_t>(50, 20 + component));
}
double scaleSlowVariantValue(double value, int32_t powerPercent)
{
	if(value >= 0 || powerPercent >= 100)
		return value;
	// Preserve the original Lua numeric ceil after all recipient/caster specialties.
	return std::ceil(value * powerPercent / 100.0);
}

int32_t focusMarkBasisPoints(const FocusRules & profile, int32_t power, int32_t coefficient,
	int32_t warcasting, int32_t empower, int32_t specialty)
{
	// Preserve the original proxy's validation domain before profile scaling.
	newHorizonsSorcery::arcaneBreachMarkBasisPoints(power);
	const auto component = spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
		static_cast<int64_t>(power) * profile.powerBasisPoints, 1, coefficient, warcasting, empower, specialty);
	return static_cast<int32_t>(std::min<int64_t>(profile.capBasisPoints, profile.baseBasisPoints + component));
}

int64_t livingArmorHitPoints(int64_t baseHitPoints, int64_t powerCoefficient, int32_t power,
	int32_t divisor, int32_t coefficient, int32_t warcasting, int32_t empower)
{
	if(baseHitPoints <= 0 || powerCoefficient < 0)
		return 0;
	return baseHitPoints + spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
		powerCoefficient * std::max(0, power), divisor, coefficient, warcasting, empower);
}

std::optional<FocusRules> focusRules(const JsonNode & rules)
{
	const auto * value = profile(rules, newHorizonsSorcery::FOCUS_MAGIC_SPELL, "focusMagic");
	if(!value)
		return std::nullopt;
	version(*value, 4);
	FocusRules result{integer((*value)["baseBasisPoints"], 0, 10000),
		integer((*value)["powerBasisPoints"], 0, 10000), integer((*value)["capBasisPoints"], 0, 10000)};
	if(result.baseBasisPoints > result.capBasisPoints)
		throw std::runtime_error("Focus Magic base exceeds its cap");
	return result;
}
bool slowAffectsSpeed(const JsonNode & rules)
{
	const auto * value = profile(rules, "core:slow", "slow");
	if(!value)
		return false;
	version(*value, 2);
	if(!(*value)["affectsSpeed"].isBool() || !(*value)["affectsSpeed"].Bool())
		throw std::runtime_error("Revised Slow must affect Speed and Initiative");
	return true;
}
void validateSpellRow(const JsonNode & rules, std::string_view identity, const JsonNode & row)
{
	for(const auto & [key, owner] : {std::pair{"focusMagic", newHorizonsSorcery::FOCUS_MAGIC_SPELL},
		std::pair{"slow", "core:slow"}, std::pair{"livingArmor", "new-horizons:summonLivingArmor"}})
		if(row.Struct().contains(key) && identity != owner)
			throw std::runtime_error("Sorcery revision clause requires its canonical spell row");
	if(identity == newHorizonsSorcery::FOCUS_MAGIC_SPELL) (void)focusRules(rules);
	if(identity == "core:slow") (void)slowAffectsSpeed(rules);
	if(identity == "new-horizons:summonLivingArmor") validateArmor(rules);
}
void validateSerialization(const JsonNode & rules, bool supported)
{
	if(!rules["spells"].isStruct()) return;
	for(const auto & [identity, row] : rules["spells"].Struct())
	{
		if(!row.isStruct()) continue;
		const bool revised = row.Struct().contains("focusMagic") || row.Struct().contains("slow")
			|| row.Struct().contains("livingArmor");
		if(revised && !supported)
			throw std::runtime_error("Captured Sorcery revision requires the current save format");
		validateSpellRow(rules, identity, row);
	}
}
}
