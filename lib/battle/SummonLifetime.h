/*
 * SummonLifetime.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "../json/JsonNode.h"
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace battle
{
/// Missing legacy data is combat-long. Present data must be an exact bounded
/// integer: null/fractional/negative input must not silently reset a lifetime.
inline int32_t summonLifetimeFromJson(const JsonNode & owner)
{
	if(!owner.isStruct() || !owner.Struct().contains("summonRoundsRemaining"))
		return 0;
	const auto & value = owner["summonRoundsRemaining"];
	if(value.getType() != JsonNode::JsonType::DATA_INTEGER || value.Integer() < 0
		|| value.Integer() > std::numeric_limits<int32_t>::max())
		throw std::runtime_error("Invalid temporary summon lifetime");
	return static_cast<int32_t>(value.Integer());
}
}
