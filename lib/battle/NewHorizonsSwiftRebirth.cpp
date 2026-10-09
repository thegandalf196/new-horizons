/*
 * NewHorizonsSwiftRebirth.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "NewHorizonsSwiftRebirth.h"
#include "../bonuses/BonusParameters.h"
#include "../bonuses/BonusList.h"
#include <limits>
#include <stdexcept>

namespace newHorizonsSwiftRebirth
{
namespace
{
SecondarySkill skill()
{
	return SecondarySkill(SecondarySkill::decode("new-horizons:elementalRebirth"));
}

Bonus makeMarker(const Lifecycle & state)
{
	if(state.birthRound < 0 || (state.normalCompleted && state.priorityPending) || skill().getNum() < 0)
		throw std::invalid_argument("Invalid Swift Rebirth lifecycle");
	Bonus result(BonusDuration::ONE_BATTLE, BonusType::NONE, BonusSource::SECONDARY_SKILL,
		0, BonusSourceID(skill()));
	result.hidden = true;
	JsonNode parameters;
	parameters["kind"].String() = "swiftRebirth";
	parameters["schemaVersion"].Integer() = 1;
	parameters["birthRound"].Integer() = state.birthRound;
	parameters["priorityPending"].Bool() = state.priorityPending;
	parameters["normalCompleted"].Bool() = state.normalCompleted;
	result.parameters = std::make_shared<BonusParameters>(parameters);
	return result;
}
}

Bonus marker(int32_t birthRound)
{
	return makeMarker({birthRound, true, false});
}

std::optional<Lifecycle> lifecycle(const Bonus & bonus)
{
	if(!bonus.parameters)
		return {};
	const auto data = bonus.parameters->toJsonNode();
	if(!data.isStruct() || !data["kind"].isString() || data["kind"].String() != "swiftRebirth")
		return {};
	if(bonus.type != BonusType::NONE || bonus.source != BonusSource::SECONDARY_SKILL
		|| bonus.sid != BonusSourceID(skill()) || bonus.duration != BonusDuration::ONE_BATTLE
		|| bonus.val != 0 || !bonus.hidden || bonus.hasStatusMetadata()
		|| data.Struct().size() != 5
		|| data["schemaVersion"].getType() != JsonNode::JsonType::DATA_INTEGER
		|| data["schemaVersion"].Integer() != 1
		|| data["birthRound"].getType() != JsonNode::JsonType::DATA_INTEGER
		|| data["birthRound"].Integer() < 0
		|| data["birthRound"].Integer() > std::numeric_limits<int32_t>::max()
		|| data["priorityPending"].getType() != JsonNode::JsonType::DATA_BOOL
		|| data["normalCompleted"].getType() != JsonNode::JsonType::DATA_BOOL)
		throw std::invalid_argument("Invalid Swift Rebirth marker");
	Lifecycle state{static_cast<int32_t>(data["birthRound"].Integer()),
		data["priorityPending"].Bool(), data["normalCompleted"].Bool()};
	if(state.priorityPending && state.normalCompleted)
		throw std::invalid_argument("Completed Swift Rebirth cannot retain queue priority");
	return state;
}

std::optional<Lifecycle> lifecycle(const battle::Unit & unit)
{
	std::optional<Lifecycle> result;
	const auto bonuses = unit.getBonusesOfType(BonusType::NONE);
	for(const auto & bonus : *bonuses)
	{
		if(const auto state = lifecycle(*bonus))
		{
			if(result)
				throw std::invalid_argument("Duplicate Swift Rebirth lifecycle");
			result = state;
		}
	}
	return result;
}

bool isLifecycleMarker(const Bonus & bonus)
{
	return lifecycle(bonus).has_value();
}

void validateTransition(const std::optional<Lifecycle> & previous, const Bonus & bonus, bool adding)
{
	const auto next = lifecycle(bonus);
	if(!next)
		throw std::invalid_argument("Expected a typed Swift Rebirth lifecycle");
	if(adding)
	{
		if(previous || !next->priorityPending || next->normalCompleted)
			throw std::invalid_argument("Swift Rebirth must start with one unspent priority receipt");
	}
	else if(!previous || previous->birthRound != next->birthRound
		|| (!previous->priorityPending && next->priorityPending)
		|| (previous->normalCompleted && !next->normalCompleted))
		throw std::invalid_argument("Swift Rebirth lifecycle cannot regress or change birth round");
}

bool blocksAdditionalActivation(const battle::Unit & unit, int32_t currentRound)
{
	const auto state = lifecycle(unit);
	return state && state->birthRound == currentRound;
}

bool normalActivationCompleted(const battle::Unit & unit, int32_t currentRound)
{
	const auto state = lifecycle(unit);
	return state && state->birthRound == currentRound && state->normalCompleted;
}

bool priorityEligible(const battle::Unit & unit, int32_t currentRound)
{
	const auto state = lifecycle(unit);
	return state && state->birthRound == currentRound && state->priorityPending
		&& !state->normalCompleted && unit.alive() && !unit.isGhost() && !unit.isTurret()
		&& unit.isSummoned() && !unit.isClone()
		&& unit.unitSlot() != SlotID::WAR_MACHINES_SLOT
		&& unit.unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER;
}

std::optional<Bonus> reserveNormalActivationPlan(const battle::Unit & unit, int32_t currentRound)
{
	auto state = lifecycle(unit);
	if(!state || state->birthRound != currentRound || !state->priorityPending || state->normalCompleted)
		return {};
	state->priorityPending = false;
	return makeMarker(*state);
}

std::optional<Bonus> completeNormalActivationPlan(const battle::Unit & unit, int32_t currentRound)
{
	auto state = lifecycle(unit);
	if(!state || state->birthRound != currentRound || state->normalCompleted)
		return {};
	state->priorityPending = false;
	state->normalCompleted = true;
	return makeMarker(*state);
}
}
