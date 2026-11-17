/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsConfusionState.h"
#include "DreadCavalryState.h"
class JsonSerializeFormat;
namespace battle
{
/// Strict decoded receipts retained across native health/state rebinding.
/// This is a parser result, never another mutable simulation state owner.
struct UnitStateEffectReceipts
{
	ConfusionState confusion;
	DreadCavalryState dreadCavalry;
	bool demonicGateOpened = false;
	BattleSide physicalPoisonSpellSide = BattleSide::NONE;
};
DLL_LINKAGE UnitStateEffectReceipts parseUnitStateEffectReceipts(const JsonNode & data);
DLL_LINKAGE bool demonicGateOpenedFromUnitJson(const JsonNode & unit);
DLL_LINKAGE bool hasPassiveFactionUnitState(const JsonNode & unit);
DLL_LINKAGE void serializePassiveFactionUnitState(JsonSerializeFormat & handler,
	bool & demonicGateOpened, BattleSide & poisonSpellSide);
}
