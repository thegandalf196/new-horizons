/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "SylvanAmbushState.h"
#include "../json/JsonNode.h"
#include <array>
#include <limits>
#include <string_view>
namespace battle
{
namespace
{
bool validRound(int32_t round)
{
	return round >= -1;
}
JsonNode hexJson(BattleHex hex)
{
	return JsonNode(static_cast<int64_t>(hex.toInt()));
}
}

bool SylvanAmbushState::hasState() const
{
	return *this != SylvanAmbushState{};
}

void SylvanAmbushState::validate() const
{
	if(source != Source::NONE && source != Source::RANK && source != Source::FOREST_COVER)
		throw std::runtime_error("Invalid Sylvan Ambush slot source");
	if(!validRound(concealedRound) || !validRound(approachRound) || !validRound(huntingPackRound)
		|| startledBy < -1)
		throw std::runtime_error("Invalid Sylvan Ambush round marker");
	if(concealed && (source == Source::NONE || concealedRound < 0 || strikePending))
		throw std::runtime_error("Concealed Sylvan Ambush needs a slot source and no pending strike");
	if(strikePending != strikeOrigin.isValid() || (strikePending && concealedRound < 0))
		throw std::runtime_error("Invalid pending Ambush strike");
	if((approachRound >= 0) != approachStart.isValid() || approachStart.isValid() != approachEnd.isValid())
		throw std::runtime_error("Invalid Sylvan Ambush approach record");
}

void SylvanAmbushState::validateAt(int32_t round) const
{
	validate();
	if((concealed || strikePending) && concealedRound != round)
		throw std::runtime_error("Sylvan Ambush concealment or strike belongs to another round");
	if(concealedRound > round || approachRound > round || huntingPackRound > round)
		throw std::runtime_error("Sylvan Ambush round marker is in the future");
}

void SylvanAmbushState::conceal(Source slot, int32_t round)
{
	if(slot == Source::NONE || round < 0)
		throw std::logic_error("Concealment requires a slot and a round");
	concealed = true;
	source = slot;
	concealedRound = round;
	strikePending = false;
	strikeOrigin = BattleHex();
}

void SylvanAmbushState::reveal()
{
	concealed = false;
}

void SylvanAmbushState::harm()
{
	concealed = false;
	harmedThisRound = true;
}

void SylvanAmbushState::beginActivation(bool delayed, int32_t round, BattleHex position, bool ivorAmbush)
{
	const bool fromConcealment = delayed && concealed;
	const bool fromIvor = delayed && ivorAmbush && concealedRound != round - 1;
	concealed = false;
	strikePending = (fromConcealment || fromIvor) && position.isValid();
	strikeOrigin = strikePending ? position : BattleHex();
	if(fromIvor && strikePending)
	{
		// Ivor's concealment uses no slot; it only spends the alternate round.
		source = Source::NONE;
		concealedRound = round;
	}
}

void SylvanAmbushState::finishActivation()
{
	strikePending = false;
	strikeOrigin = BattleHex();
}

void SylvanAmbushState::finishAttack(bool counter)
{
	concealed = false;
	strikePending = false;
	strikeOrigin = BattleHex();
	if(counter)
		startledBy = -1;
}

void SylvanAmbushState::newRound()
{
	concealed = false;
	strikePending = false;
	strikeOrigin = BattleHex();
	harmedThisRound = false;
	startledBy = -1;
}

void SylvanAmbushState::retire()
{
	concealed = false;
	strikePending = false;
	strikeOrigin = BattleHex();
	startledBy = -1;
}

void SylvanAmbushState::recordApproach(int32_t round, BattleHex before, BattleHex after)
{
	if(round < 0 || !before.isValid() || !after.isValid() || before == after)
		return;
	if(approachRound != round || approachEnd != before)
		approachStart = before;
	approachEnd = after;
	approachRound = round;
}

JsonNode SylvanAmbushState::toJson() const
{
	validate();
	if(!hasState())
		return {};
	JsonNode result;
	result["concealed"] = JsonNode(concealed);
	result["source"] = JsonNode(static_cast<int64_t>(source));
	result["concealedRound"] = JsonNode(static_cast<int64_t>(concealedRound));
	result["strikePending"] = JsonNode(strikePending);
	result["strikeOrigin"] = hexJson(strikeOrigin);
	result["harmedThisRound"] = JsonNode(harmedThisRound);
	result["approachRound"] = JsonNode(static_cast<int64_t>(approachRound));
	result["approachStart"] = hexJson(approachStart);
	result["approachEnd"] = hexJson(approachEnd);
	result["huntingPackRound"] = JsonNode(static_cast<int64_t>(huntingPackRound));
	result["startledBy"] = JsonNode(static_cast<int64_t>(startledBy));
	return result;
}

SylvanAmbushState SylvanAmbushState::fromJson(const JsonNode & data)
{
	if(data.isNull())
		return {};
	if(!data.isStruct())
		throw std::runtime_error("Invalid Sylvan Ambush state object");
	constexpr std::array<std::string_view, 3> flags = {"concealed", "strikePending", "harmedThisRound"};
	constexpr std::array<std::string_view, 8> numbers = {"source", "concealedRound", "strikeOrigin", "approachRound",
		"approachStart", "approachEnd", "huntingPackRound", "startledBy"};
	const auto listed = [](const auto & names, const std::string & field)
	{
		return std::ranges::any_of(names, [&field](std::string_view allowed) { return allowed == field; });
	};
	for(const auto & [field, value] : data.Struct())
	{
		const bool flag = listed(flags, field);
		if((!flag && !listed(numbers, field)) || (flag && !value.isBool())
			|| (!flag && value.getType() != JsonNode::JsonType::DATA_INTEGER))
			throw std::runtime_error("Invalid Sylvan Ambush state field");
	}
	const auto flag = [&data](const char * field)
	{
		return data.Struct().contains(field) && data[field].Bool();
	};
	const auto number = [&data](const char * field) -> int32_t
	{
		if(!data.Struct().contains(field))
			return -1;
		const auto value = data[field].Integer();
		if(value < -1 || value > std::numeric_limits<int32_t>::max())
			throw std::runtime_error("Sylvan Ambush state value is out of range");
		return static_cast<int32_t>(value);
	};
	const auto hex = [&number](const char * field)
	{
		const auto value = number(field);
		if(value >= GameConstants::BFIELD_SIZE)
			throw std::runtime_error("Sylvan Ambush hex is outside the battlefield");
		return value < 0 ? BattleHex() : BattleHex(static_cast<si16>(value));
	};
	SylvanAmbushState result;
	result.concealed = flag("concealed");
	const auto slot = data.Struct().contains("source") ? number("source") : 0;
	if(slot < 0 || slot > static_cast<int32_t>(Source::FOREST_COVER))
		throw std::runtime_error("Invalid Sylvan Ambush slot source");
	result.source = static_cast<Source>(slot);
	result.concealedRound = number("concealedRound");
	result.strikePending = flag("strikePending");
	result.strikeOrigin = hex("strikeOrigin");
	result.harmedThisRound = flag("harmedThisRound");
	result.approachRound = number("approachRound");
	result.approachStart = hex("approachStart");
	result.approachEnd = hex("approachEnd");
	result.huntingPackRound = number("huntingPackRound");
	result.startledBy = number("startledBy");
	result.validate();
	return result;
}

SylvanAmbushState sylvanAmbushStateFromUnitJson(const JsonNode & unit)
{
	if(!unit.isStruct() || !unit.Struct().contains("state"))
		return {};
	const auto & state = unit["state"];
	if(!state.isStruct())
		throw std::runtime_error("Invalid serialized Sylvan Ambush unit state");
	const auto result = state.Struct().contains("sylvanAmbush")
		? SylvanAmbushState::fromJson(state["sylvanAmbush"]) : SylvanAmbushState{};
	if(result.concealed)
		for(const auto * flag : {"waiting", "waitedThisTurn"})
			if(!state.Struct().contains(flag) || !state[flag].isBool() || !state[flag].Bool())
				throw std::runtime_error("Concealed Sylvan Ambush requires accepted Wait queue state");
	return result;
}
}
