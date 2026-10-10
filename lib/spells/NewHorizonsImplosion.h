/*
 * NewHorizonsImplosion.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include <optional>
#include <vector>
#include "../battle/BattleHex.h"
#include "../constants/EntityIdentifiers.h"
#include "../json/JsonNode.h"

class CBattleInfoCallback;
namespace battle { class Unit; }

namespace newHorizonsImplosion
{
struct DLL_LINKAGE Rules
{
	int32_t baseBasisPoints;
	int32_t powerBasisPointsPerSpellPower;
	int32_t maximumBasisPoints;
	int32_t pullRadius;
	int32_t pullDistance;
};
DLL_LINKAGE bool hasRules(const JsonNode & rules);
DLL_LINKAGE std::optional<Rules> rulesFor(const JsonNode & rules, SpellID spell);
DLL_LINKAGE void validate(const JsonNode & rules);
DLL_LINKAGE int64_t damage(const Rules & rules, int64_t currentHealth, int32_t spellPower,
	int32_t coefficientBasisPoints, int32_t warcastingPercent, int32_t empowerPercent);
/// Snapshot recipient IDs once; each endpoint is subsequently checked on the changed board.
DLL_LINKAGE std::vector<uint32_t> pullOrder(const CBattleInfoCallback & battle,
	uint32_t primaryID, BattleHex origin, int32_t radius);
DLL_LINKAGE std::optional<BattleHex> pullDestination(const CBattleInfoCallback & battle,
	const battle::Unit & unit, BattleHex origin);
}
