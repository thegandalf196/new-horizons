/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "PhysicalPoisonSpellOrigin.h"
#include "CUnitState.h"
#include "../json/JsonNode.h"
namespace battle
{
void validatePhysicalPoisonSerialization(const CUnitState & state, bool supported)
{
	if(!validPhysicalPoisonSpellOrigin(state.physicalPoisonSpellSide)
		|| state.physicalPoisonBaseDamage < 0 || state.physicalPoisonActivationsRemaining < 0
		|| state.physicalPoisonActivationsRemaining > 3
		|| ((state.physicalPoisonBaseDamage == 0) != (state.physicalPoisonActivationsRemaining == 0))
		|| (state.physicalPoisonSpellSide != BattleSide::NONE && state.physicalPoisonBaseDamage == 0))
		throw std::runtime_error("Invalid physical Poison payload or spell origin");
	if(!supported && (state.physicalPoisonBaseDamage != 0 || state.physicalPoisonActivationsRemaining != 0
		|| state.physicalPoisonSourceStackId != -1 || state.physicalPoisonSpellSide != BattleSide::NONE))
		throw std::runtime_error("Cannot discard physical Poison payload or spell origin in an older binary descriptor");
}
BattleSide physicalPoisonSpellOriginFromJson(const JsonNode & unit)
{
	if(!unit.isStruct())
		return BattleSide::NONE;
	const auto state = unit.Struct().find("state");
	if(state == unit.Struct().end() || !state->second.isStruct())
		return BattleSide::NONE;
	const auto field = state->second.Struct().find("physicalPoisonSpellSide");
	if(field == state->second.Struct().end())
		return BattleSide::NONE;
	const auto & value = field->second;
	if(value.getType() != JsonNode::JsonType::DATA_INTEGER || value.Integer() < -1 || value.Integer() > 1)
		throw std::runtime_error("Physical Poison spell origin must be integer NONE, attacker or defender");
	const auto side = static_cast<BattleSide>(value.Integer());
	if(side != BattleSide::NONE)
	{
		const auto & base = state->second["physicalPoisonBaseDamage"];
		const auto & ticks = state->second["physicalPoisonActivationsRemaining"];
		if(base.getType() != JsonNode::JsonType::DATA_INTEGER || base.Integer() <= 0
			|| ticks.getType() != JsonNode::JsonType::DATA_INTEGER || ticks.Integer() < 1 || ticks.Integer() > 3)
			throw std::runtime_error("Physical Poison spell origin requires an active Poison payload");
	}
	return side;
}
}
