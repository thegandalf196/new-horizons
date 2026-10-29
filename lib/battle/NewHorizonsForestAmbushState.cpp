/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsForestAmbushState.h"
#include "../json/JsonNode.h"
#include <array>
#include <string_view>
namespace battle
{
bool ForestAmbushState::hasState() const
{
	return attacked || waited || concealed || shotPending;
}
void ForestAmbushState::validate() const
{
	if((concealed && shotPending) || ((concealed || shotPending) && !waited)
		|| (concealed && attacked))
		throw std::runtime_error("Invalid Forest Ambush lifecycle");
}
void ForestAmbushState::acceptWait(bool entitled)
{
	validate();
	if(waited)
		return;
	waited = true;
	concealed = entitled && !attacked;
}
void ForestAmbushState::receiveAttack()
{
	attacked = true;
	concealed = false;
}
void ForestAmbushState::beginActivation(bool delayed)
{
	validate();
	shotPending = delayed && concealed;
	concealed = false;
}
void ForestAmbushState::finishActivation()
{
	// Accepted Wait ends its current action, not the later delayed activation.
	shotPending = false;
}
void ForestAmbushState::finishStrike(bool ranged, bool physical)
{
	if(!physical)
		return;
	concealed = false;
	if(ranged)
		shotPending = false;
}
void ForestAmbushState::retire()
{
	concealed = false;
	shotPending = false;
}
JsonNode ForestAmbushState::toJson() const
{
	validate();
	if(!hasState())
		return {};
	JsonNode result;
	result["attacked"] = JsonNode(attacked);
	result["waited"] = JsonNode(waited);
	result["concealed"] = JsonNode(concealed);
	result["shotPending"] = JsonNode(shotPending);
	return result;
}
ForestAmbushState ForestAmbushState::fromJson(const JsonNode & data)
{
	if(data.isNull())
		return {};
	if(!data.isStruct())
		throw std::runtime_error("Invalid Forest Ambush state object");
	constexpr std::array<std::string_view, 4> fields = {"attacked", "waited", "concealed", "shotPending"};
	for(const auto & [field, value] : data.Struct())
		if(std::ranges::none_of(fields, [&field](std::string_view allowed) { return allowed == field; })
			|| !value.isBool())
			throw std::runtime_error("Invalid Forest Ambush state field");
	const auto read = [&data](const char * field)
	{
		return data.Struct().contains(field) && data[field].Bool();
	};
	ForestAmbushState result{read("attacked"), read("waited"), read("concealed"), read("shotPending")};
	result.validate();
	return result;
}
ForestAmbushState forestAmbushStateFromUnitJson(const JsonNode & unit)
{
	if(!unit.isStruct() || !unit.Struct().contains("state"))
		return {};
	const auto & state = unit["state"];
	if(!state.isStruct())
		throw std::runtime_error("Invalid serialized Forest Ambush unit state");
	const auto result = state.Struct().contains("forestAmbush")
		? ForestAmbushState::fromJson(state["forestAmbush"]) : ForestAmbushState{};
	if(result.concealed)
		for(const auto * flag : {"waiting", "waitedThisTurn"})
			if(!state.Struct().contains(flag) || !state[flag].isBool() || !state[flag].Bool())
				throw std::runtime_error("Concealed Forest Ambush requires accepted Wait queue state");
	return result;
}
}
