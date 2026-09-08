/*
 * NewHorizonsSpellAvailability.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../json/JsonNode.h"
#include "../constants/EntityIdentifiers.h"

class IGameInfoCallback;
class CBattleInfoCallback;

namespace newHorizonsMagic
{
/// Roster-expansion primitive. Given validated saved rules and an installed
/// spell's scoped identity/kind, decide whether that world includes the spell.
/// Not a replacement for map bans, targeting, mana or authoritative validation.
DLL_LINKAGE bool spellBelongsToRules(const JsonNode & rules, const std::string & scopedIdentity, bool commonHeroSpell);
/// Copied admission only: no map bans, possession, mana or targeting decisions.
/// Invalid/out-of-range/null definitions fail before dereference. Callers supply
/// validated snapshots; registry content is used only for identity and spell kind.
DLL_LINKAGE bool spellAllowedBySavedRoster(const JsonNode & rules, SpellID spell);
DLL_LINKAGE bool spellAllowedByWorldRoster(const IGameInfoCallback & world, SpellID spell);
/// No actual battle returns false. An actual absent battle roster uses legacy
/// admission, never the world or installed rules.
DLL_LINKAGE bool spellAllowedByBattleRoster(const CBattleInfoCallback & battle, SpellID spell);
}
