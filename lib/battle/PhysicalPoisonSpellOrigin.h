/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
#include <cstdint>
#include "BattleSide.h"
#include <stdexcept>
class JsonNode;
namespace battle
{
class CUnitState;
DLL_LINKAGE void validatePhysicalPoisonSerialization(const CUnitState & state, bool supported);
/// This receipt records the original spell caster side, never a current owner
/// or an inferred other side. NONE preserves unknown historical/creature origin.
DLL_LINKAGE BattleSide physicalPoisonSpellOriginFromJson(const JsonNode & unit);
inline bool validPhysicalPoisonSpellOrigin(BattleSide side)
{
	return side == BattleSide::NONE || side == BattleSide::ATTACKER || side == BattleSide::DEFENDER;
}
}
