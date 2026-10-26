/*
 * NewHorizonsTheChosen.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../json/JsonNode.h"
#include <optional>
#include <string_view>

class CBattleInfoCallback;
class CGHeroInstance;
namespace battle { class Unit; }

namespace newHorizonsTheChosen
{
inline constexpr std::string_view SPELL_KEY = "new-horizons:theChosen";
struct Rules
{
	int32_t baseDuration;
	int32_t maximumDuration;
	int32_t spellPowerPerRound;
	int32_t joustingPercent;
	int32_t exaltedChampionDurationBonus;
};
/// Endpoint effects exchange simultaneously; only a third recipient blocks transfer.
DLL_LINKAGE bool canTransferTo(const CBattleInfoCallback & callback, uint32_t originalSourceUnitId,
	const battle::Unit & recipient);
DLL_LINKAGE int32_t exaltedChampionDurationBonus(const Rules & rules, const CGHeroInstance * hero);
DLL_LINKAGE std::optional<Rules> rulesFor(const JsonNode & savedRules);
DLL_LINKAGE void validate(const JsonNode & savedRules);
DLL_LINKAGE void validateSpellRow(const JsonNode & savedRules, std::string_view identity, const JsonNode & row);
DLL_LINKAGE void validateSerialization(const JsonNode & savedRules, bool supported);
/// Raw Spell Power and School only; exceptional duration extensions are separate.
DLL_LINKAGE int32_t nominalDuration(const Rules & rules, int32_t rawSpellPower, int32_t schoolPercent);
}
