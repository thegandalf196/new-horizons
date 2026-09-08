/*
 * NewHorizonsDirectDamage.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsDirectDamage.h"
#include <cmath>
#include <stdexcept>

namespace newHorizonsMagic
{
namespace
{
int32_t parameter(const JsonNode & node)
{
	if(node.getType() != JsonNode::JsonType::DATA_INTEGER || !std::isfinite(node.Float()) || node.Float() < 0
		|| node.Float() > MAX_DIRECT_DAMAGE_PARAMETER || std::floor(node.Float()) != node.Float())
		throw std::runtime_error("Invalid New Horizons direct damage parameter");
	return static_cast<int32_t>(node.Float());
}
}

int64_t DirectDamageFormula::evaluate(int32_t effectPower, int32_t divisor) const
{
	// Also check directly constructed DTOs. Negative damage is not healing.
	if(base < 0 || base > MAX_DIRECT_DAMAGE_PARAMETER || powerCoefficient < 0
		|| powerCoefficient > MAX_DIRECT_DAMAGE_PARAMETER || effectPower < 0 || divisor <= 0)
		throw std::runtime_error("Invalid New Horizons direct damage evaluation inputs");
	return base + static_cast<int64_t>(powerCoefficient) * effectPower / divisor;
}

std::optional<DirectDamageFormula> directDamageFormula(const JsonNode & spellRecord, int rulesetVersion)
{
	if(rulesetVersion != 1 && rulesetVersion != 2)
		throw std::runtime_error("Unsupported New Horizons direct damage ruleset version");
	if(!spellRecord.isStruct())
		throw std::runtime_error("New Horizons direct damage requires a spell record");
	const auto found = spellRecord.Struct().find("directDamage");
	if(found == spellRecord.Struct().end())
		return std::nullopt;
	if(rulesetVersion != 2)
		throw std::runtime_error("New Horizons direct damage requires ruleset version 2");
	const auto & node = found->second;
	if(!node.isStruct() || node.Struct().size() != 2
		|| !node.Struct().contains("base") || !node.Struct().contains("powerCoefficient"))
		throw std::runtime_error("Invalid New Horizons direct damage fields");
	return DirectDamageFormula{parameter(node["base"]), parameter(node["powerCoefficient"])};
}
}
