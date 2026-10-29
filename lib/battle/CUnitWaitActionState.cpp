/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "CUnitState.h"
#include "../serializer/JsonSerializeFormat.h"
namespace battle
{
void CUnitState::serializeWaitActionState(JsonSerializeFormat & handler)
{
	JsonNode forestAmbushData;
	if(handler.saving)
		forestAmbushData = forestAmbush.toJson();
	const JsonNode emptyForestAmbush;
	handler.serializeRaw("forestAmbush", forestAmbushData, std::cref(emptyForestAmbush));
	if(!handler.saving)
		forestAmbush = ForestAmbushState::fromJson(forestAmbushData);
	handler.serializeBool("waiting", waiting);
	handler.serializeBool("waitedThisTurn", waitedThisTurn);
	handler.serializeBool("battlecraftWaitBonusUsed", battlecraftWaitBonusUsed);

}
}
