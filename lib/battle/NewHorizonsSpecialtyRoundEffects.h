/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <cstdint>
class CBattleInfoCallback;
class JsonNode;
namespace battle { class CUnitState; class Unit; }
namespace newHorizonsSpecialtyEvents
{
DLL_LINKAGE int32_t roundReceiptFromJson(const JsonNode & unitState, int32_t absent);
DLL_LINKAGE bool needsRoundEffects(const CBattleInfoCallback & battle, const battle::Unit & unit, int32_t round);
/// Mutate only the caller-owned live/detached state. Real authorities publish its ordinary UnitChanges.
DLL_LINKAGE bool applyRound(const CBattleInfoCallback & battle, battle::CUnitState & unit, int32_t round);
}
