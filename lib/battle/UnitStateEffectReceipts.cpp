/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "UnitStateEffectReceipts.h"
#include "PhysicalPoisonSpellOrigin.h"
#include "NewHorizonsBloodrage.h"
#include "../json/JsonNode.h"
#include "../serializer/JsonSerializeFormat.h"
namespace battle
{
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
	const auto & shield = data["state"]["arcaneShieldHitPoints"];
	if(!shield.isNull() && (shield.getType() != JsonNode::JsonType::DATA_INTEGER || shield.Integer() < 0))
		throw std::runtime_error("Invalid Arcane Shield hit points");
	return {confusion, dreadCavalry, arcaneMarksFromUnitJson(data), shield.isNull() ? 0 : shield.Integer(),
		demonicGateOpenedFromUnitJson(data), physicalPoisonSpellOriginFromJson(data)};
}
void serializePassiveFactionUnitState(JsonSerializeFormat & handler,
	ArcaneMarkState & marks, bool & demonicGateOpened, BattleSide & poisonSpellSide, int64_t & arcaneShieldHitPoints)
{
	handler.serializeInt("arcaneShieldHitPoints", arcaneShieldHitPoints, 0);
	if(arcaneShieldHitPoints < 0)
		throw std::runtime_error("Invalid Arcane Shield hit points");
	JsonNode encoded;
	if(handler.saving)
		encoded = marks.toJson();
	handler.serializeRaw("arcaneMarks", encoded, std::nullopt);
	if(!handler.saving)
		marks = encoded.isNull() ? ArcaneMarkState{} : ArcaneMarkState::fromJson(encoded);
	handler.serializeBool("demonicGateOpened", demonicGateOpened);
	int32_t side = static_cast<int32_t>(poisonSpellSide);
	handler.serializeInt("physicalPoisonSpellSide", side, -1);
	if(side < -1 || side > 1)
		throw std::runtime_error("Invalid physical Poison spell-origin side");
	poisonSpellSide = static_cast<BattleSide>(side);
}
}
