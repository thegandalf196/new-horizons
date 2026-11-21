/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "UnitShieldState.h"
#include "Unit.h"
#include "PhysicalPoisonSpellOrigin.h"
#include "../json/JsonNode.h"
#include <limits>
#include <stdexcept>
namespace battle
{
bool hasConsecratedUnitState(const JsonNode & unit)
{
	const auto & state = unit["state"];
	if(state.isNull()) return false;
	if(!state.isStruct()) throw std::runtime_error("Consecrated unit state must be an object");
	auto integer = [&state](const char * name) -> int64_t
	{
		const auto & node = state[name];
		if(node.isNull()) return 0;
		if(node.getType() != JsonNode::JsonType::DATA_INTEGER)
			throw std::runtime_error("Consecrated shield field must be an integer");
		return node.Integer();
	};
	auto boolean = [&state](const char * name)
	{
		const auto & node = state[name];
		if(node.isNull()) return false;
		if(!node.isBool()) throw std::runtime_error("Consecrated receipt must be a boolean");
		return node.Bool();
	};
	RedemptionShieldState shield;
	shield.remaining = integer("redemptionShieldHP");
	shield.capacity = integer("redemptionShieldCapacity");
	const auto fraction = integer("redemptionShieldFractionalHP");
	if(fraction < 0 || fraction >= 20000)
		throw std::runtime_error("Invalid Redemption fractional HP");
	shield.fractionalHP = static_cast<int32_t>(fraction);
	shield.initialized = boolean("redemptionShieldInitialized");
	shield.validate();
	const auto poisonDescriptor = physicalPoisonDescriptorFromJson(unit);
	const bool completedAction = boolean("completedActionThisRound");
	bool meaningful = !shield.empty() || completedAction || static_cast<bool>(poisonDescriptor);
	for(const auto * key : {"health", "battleFormOriginalHealth"})
	{
		const auto & health = state[key];
		if(!health.isNull() && !health.isStruct()) throw std::runtime_error("Invalid health snapshot");
		const auto & cohorts = health["casualtyProvenance"];
		if(cohorts.isNull()) continue;
		if(!cohorts.isVector()) throw std::runtime_error("Invalid casualty provenance list");
		for(const auto & cohort : cohorts.Vector())
		{
			if(!cohort.isStruct()) throw std::runtime_error("Invalid casualty provenance entry");
			const auto & kind = cohort["provenance"];
			if(kind.getType() != JsonNode::JsonType::DATA_INTEGER)
				throw std::runtime_error("Casualty provenance must be an integer");
			if(kind.Integer() < 0 || kind.Integer() > static_cast<int64_t>(DamageProvenance::MAGICAL_CREATURE))
				throw std::runtime_error("Unknown casualty provenance");
			meaningful |= kind.Integer() == static_cast<int64_t>(DamageProvenance::MAGICAL_CREATURE);
		}
	}
	return meaningful;
}
bool hasArcaneWardUnitState(const JsonNode & unit)
{
	const auto & state = unit["state"];
	if(!state.isStruct())
		return false;
	for(const auto * key : {"arcaneWardHP", "arcaneWardCapacity"})
	{
		const auto & node = state[key];
		if(!node.isNull() && (node.getType() != JsonNode::JsonType::DATA_INTEGER || node.Integer() != 0))
			return true;
	}
	return false;
}
}
