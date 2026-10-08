/*
 * NewHorizonsConfusionState.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "NewHorizonsConfusionState.h"

#include "../json/JsonNode.h"

#include <array>
#include <algorithm>
#include <limits>
#include <string_view>

namespace
{
bool validCaster(PlayerColor caster)
{
	// Creature spellcasters may belong to a neutral battle army. Spectator,
	// unflaggable objects and unknown provenance never identify a caster.
	return caster.isValidPlayer() || caster == PlayerColor::NEUTRAL;
}

bool readBool(const JsonNode & data, const char * field)
{
	const auto & value = data[field];
	if(!data.Struct().contains(field))
		return false;
	if(value.getType() != JsonNode::JsonType::DATA_BOOL)
		throw std::runtime_error("Invalid Confusion boolean metadata");
	return value.Bool();
}

int64_t readInteger(const JsonNode & data, const char * field, int64_t fallback)
{
	const auto & value = data[field];
	if(!data.Struct().contains(field))
		return fallback;
	if(value.getType() != JsonNode::JsonType::DATA_INTEGER)
		throw std::runtime_error("Invalid Confusion integer metadata");
	return value.Integer();
}
}

namespace battle
{
bool ConfusionState::hasState() const
{
	return pending || previousResolved != ConfusionBehavior::NONE;
}

void ConfusionState::validate() const
{
	if(static_cast<uint8_t>(previousResolved) > static_cast<uint8_t>(ConfusionBehavior::WANDER))
		throw std::runtime_error("Invalid Confusion resolved behavior");
	if(pending ? !validCaster(pendingCaster)
		: pendingCaster != PlayerColor::CANNOT_DETERMINE || pendingConfounder)
		throw std::runtime_error("Invalid Confusion pending caster provenance");
}

void ConfusionState::applyPending(PlayerColor caster, bool confounder)
{
	validate();
	if(!validCaster(caster))
		throw std::runtime_error("Cannot apply Confusion without caster provenance");
	pending = true;
	pendingCaster = caster;
	pendingConfounder = confounder;
}

void ConfusionState::clearPending()
{
	pending = false;
	pendingCaster = PlayerColor::CANNOT_DETERMINE;
	pendingConfounder = false;
}

void ConfusionState::recordResolved(ConfusionBehavior behavior)
{
	validate();
	if(behavior == ConfusionBehavior::NONE
		|| static_cast<uint8_t>(behavior) > static_cast<uint8_t>(ConfusionBehavior::WANDER))
		throw std::runtime_error("Cannot record an unresolved Confusion behavior");
	clearPending();
	previousResolved = behavior;
}

JsonNode ConfusionState::toJson() const
{
	validate();
	JsonNode result;
	result["pending"] = JsonNode(pending);
	result["pendingCaster"] = JsonNode(pendingCaster.getNum());
	result["pendingConfounder"] = JsonNode(pendingConfounder);
	result["previousResolved"] = JsonNode(static_cast<int>(previousResolved));
	return result;
}

ConfusionState ConfusionState::fromJson(const JsonNode & data)
{
	if(data.isNull())
		return {};
	if(!data.isStruct())
		throw std::runtime_error("Invalid Confusion state object");
	constexpr std::array<std::string_view, 4> FIELDS = {
		"pending", "pendingCaster", "pendingConfounder", "previousResolved"
	};
	for(const auto & [field, value] : data.Struct())
	{
		if(std::ranges::none_of(FIELDS, [&](const auto allowed) { return allowed == field; }))
			throw std::runtime_error("Unknown Confusion state metadata");
	}
	ConfusionState result;
	result.pending = readBool(data, "pending");
	result.pendingConfounder = readBool(data, "pendingConfounder");
	const auto caster = readInteger(data, "pendingCaster", PlayerColor::CANNOT_DETERMINE.getNum());
	const auto behavior = readInteger(data, "previousResolved", static_cast<int>(ConfusionBehavior::NONE));
	if(caster < std::numeric_limits<int32_t>::min() || caster > std::numeric_limits<int32_t>::max()
		|| behavior < static_cast<int>(ConfusionBehavior::NONE)
		|| behavior > static_cast<int>(ConfusionBehavior::WANDER))
		throw std::runtime_error("Invalid Confusion caster or behavior identifier");
	result.pendingCaster = PlayerColor(static_cast<int>(caster));
	result.previousResolved = static_cast<ConfusionBehavior>(behavior);
	result.validate();
	return result;
}

ConfusionState confusionStateFromUnitJson(const JsonNode & unit)
{
	return ConfusionState::fromJson(unit["state"]["confusion"]);
}

bool hasConfusionState(const JsonNode & unit)
{
	return confusionStateFromUnitJson(unit).hasState();
}
}
