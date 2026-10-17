/*
 * BattleUnitsChangedInput.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattleUnitsChangedInput.h"
#include "../lib/networkPacks/PacksForClientBattle.h"
#include <bit>
#include <cstdint>

namespace
{
bool sameJsonInput(const JsonNode & first, const JsonNode & second) noexcept
{
	if(first.getType() != second.getType() || first.getModScope() != second.getModScope()
		|| first.getOverrideFlag() != second.getOverrideFlag())
		return false;
	switch(first.getType())
	{
		case JsonNode::JsonType::DATA_NULL: return true;
		case JsonNode::JsonType::DATA_BOOL: return first.Bool() == second.Bool();
		case JsonNode::JsonType::DATA_INTEGER: return first.Integer() == second.Integer();
		case JsonNode::JsonType::DATA_FLOAT:
			return std::bit_cast<uint64_t>(first.Float()) == std::bit_cast<uint64_t>(second.Float());
		case JsonNode::JsonType::DATA_STRING: return first.String() == second.String();
		case JsonNode::JsonType::DATA_VECTOR:
			if(first.Vector().size() != second.Vector().size())
				return false;
			for(size_t i = 0; i < first.Vector().size(); ++i)
				if(!sameJsonInput(first.Vector()[i], second.Vector()[i]))
					return false;
			return true;
		case JsonNode::JsonType::DATA_STRUCT:
			if(first.Struct().size() != second.Struct().size())
				return false;
			for(auto a = first.Struct().begin(), b = second.Struct().begin(); a != first.Struct().end(); ++a, ++b)
				if(a->first != b->first || !sameJsonInput(a->second, b->second))
					return false;
			return true;
	}
	return false; // Unrepresentable/valueless variant: never assume identical.
}

bool sameChain(const std::optional<newHorizonsElementalRebirth::ChainConsumption> & a,
	const std::optional<newHorizonsElementalRebirth::ChainConsumption> & b) noexcept
{
	return a.has_value() == b.has_value() && (!a || (a->side == b->side
		&& a->sourceUnitId == b->sourceUnitId && a->spawnUnitId == b->spawnUnitId && a->rollback == b->rollback));
}

bool samePhoenix(const std::optional<newHorizonsElementalRebirth::PhoenixSparkConsumption> & a,
	const std::optional<newHorizonsElementalRebirth::PhoenixSparkConsumption> & b) noexcept
{
	return a.has_value() == b.has_value() && (!a || (a->side == b->side && a->sourceUnitId == b->sourceUnitId));
}
}

bool sameBattleUnitsChangedInput(const BattleUnitsChanged & first, const BattleUnitsChanged & second) noexcept
{
	if(first.battleID != second.battleID || first.changedStacks.size() != second.changedStacks.size()
		|| !sameChain(first.rebirthChainConsumption, second.rebirthChainConsumption)
		|| !samePhoenix(first.phoenixSparkConsumption, second.phoenixSparkConsumption))
		return false;
	for(size_t i = 0; i < first.changedStacks.size(); ++i)
	{
		const auto & a = first.changedStacks[i];
		const auto & b = second.changedStacks[i];
		if(a.id != b.id || a.operation != b.operation || a.healthDelta != b.healthDelta || !sameJsonInput(a.data, b.data))
			return false;
	}
	return true;
}
