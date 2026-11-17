/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "UnitStateEffectReceipts.h"
#include "PhysicalPoisonSpellOrigin.h"
#include "NewHorizonsBloodrage.h"
#include "../json/JsonNode.h"
#include "../serializer/JsonSerializeFormat.h"
namespace battle
{
bool demonicGateOpenedFromUnitJson(const JsonNode & unit)
{
	const auto & state = unit["state"];
	if(!state.isStruct()) return false;
	const auto found = state.Struct().find("demonicGateOpened");
	if(found == state.Struct().end()) return false;
	if(!found->second.isBool())
		throw std::runtime_error("Demonic Gate use must be a boolean");
	return found->second.Bool();
}
bool hasPassiveFactionUnitState(const JsonNode & unit)
{
	const bool gate = demonicGateOpenedFromUnitJson(unit);
	const auto poisonSide = physicalPoisonSpellOriginFromJson(unit);
	return gate || poisonSide != BattleSide::NONE;
}

UnitStateEffectReceipts parseUnitStateEffectReceipts(const JsonNode & data)
{
	const auto confusion = confusionStateFromUnitJson(data);
	const auto dreadCavalry = dreadCavalryStateFromUnitJson(data);
	const auto & savedPainIncrement = data["state"]["personalBloodrageIncrement"];
	if(!savedPainIncrement.isNull())
	{
		if(savedPainIncrement.getType() != JsonNode::JsonType::DATA_INTEGER)
			throw std::runtime_error("Personal Bloodrage increment must be an integer");
		const int64_t rawIncrement = savedPainIncrement.Integer();
		if(rawIncrement < 0 || rawIncrement > 2 * newHorizonsBloodrage::EXPERT_INCREMENT
			|| !newHorizonsBloodrage::isValidPersonalIncrement(static_cast<int>(rawIncrement)))
			throw std::runtime_error("Invalid personal Bloodrage increment");
	}
	return {confusion, dreadCavalry, demonicGateOpenedFromUnitJson(data), physicalPoisonSpellOriginFromJson(data)};
}
void serializePassiveFactionUnitState(JsonSerializeFormat & handler,
	bool & demonicGateOpened, BattleSide & poisonSpellSide)
{
	handler.serializeBool("demonicGateOpened", demonicGateOpened);
	int32_t side = static_cast<int32_t>(poisonSpellSide);
	handler.serializeInt("physicalPoisonSpellSide", side, -1);
	if(side < -1 || side > 1)
		throw std::runtime_error("Invalid physical Poison spell-origin side");
	poisonSpellSide = static_cast<BattleSide>(side);
}
}
