/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsConfusionState.h"
#include "DreadCavalryState.h"
#include "ArcaneMarkState.h"
class JsonSerializeFormat;
namespace battle
{
/// Strict decoded receipts retained across native health/state rebinding.
/// This is a parser result, never another mutable simulation state owner.
struct UnitStateEffectReceipts
{
	ConfusionState confusion;
	DreadCavalryState dreadCavalry;
	ArcaneMarkState arcaneMarks;
	int64_t arcaneShieldHitPoints = 0;
	bool demonicGateOpened = false;
	BattleSide physicalPoisonSpellSide = BattleSide::NONE;
};
DLL_LINKAGE UnitStateEffectReceipts parseUnitStateEffectReceipts(const JsonNode & data);
DLL_LINKAGE void serializePassiveFactionUnitState(JsonSerializeFormat & handler,
	ArcaneMarkState & marks, bool & demonicGateOpened, BattleSide & poisonSpellSide, int64_t & arcaneShieldHitPoints);
}
