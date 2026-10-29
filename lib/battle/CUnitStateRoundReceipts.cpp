/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "CUnitState.h"
#include "../serializer/JsonSerializeFormat.h"
#include <limits>
namespace battle
{
int32_t petrifyingBloodRoundFromJson(const JsonNode & unitSnapshot, int32_t absent)
{
	const auto & state = unitSnapshot["state"];
	if(!state.isStruct() || state.Struct().count("petrifyingBloodRound") == 0)
		return absent;
	const auto & round = state["petrifyingBloodRound"];
	if(round.getType() != JsonNode::JsonType::DATA_INTEGER || round.Integer() < -1
		|| round.Integer() > std::numeric_limits<int32_t>::max())
		throw std::runtime_error("Invalid Petrifying Blood round receipt");
	return static_cast<int32_t>(round.Integer());
}
void CUnitState::serializeJsonRoundReceipts(JsonSerializeFormat & handler)
{
	handler.serializeInt("battlecraftOverwatchReadyRound", battlecraftOverwatchReadyRound, -1);
	handler.serializeInt("battlecraftOverwatchUsedRound", battlecraftOverwatchUsedRound, -1);
	if(battlecraftOverwatchReadyRound < -1 || battlecraftOverwatchUsedRound < -1)
		throw std::runtime_error("Invalid Overwatch round marker");
	handler.serializeBool("battlecraftWaitMasteryDoubled", battlecraftWaitMasteryDoubled);
	handler.serializeInt("battlecraftPreemptiveStrikeRound", battlecraftPreemptiveStrikeRound, -1);
	if(battlecraftPreemptiveStrikeRound < -1)
		throw std::runtime_error("Invalid Battlecraft Pre-emptive Strike round marker");
	handler.serializeInt("petrifyingBloodRound", petrifyingBloodRound, -1);
	if(petrifyingBloodRound < -1)
		throw std::runtime_error("Invalid Petrifying Blood round receipt");
}
}
