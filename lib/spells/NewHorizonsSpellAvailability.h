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

namespace newHorizonsMagic
{
/// Future roster-expansion primitive. Given validated saved rules and an installed
/// spell's scoped identity/kind, decide whether that world includes the spell.
/// Not a replacement for map bans, targeting, mana or authoritative validation.
/// Not yet wired to casting, artifact enumeration, AI or snapshot deserialization.
DLL_LINKAGE bool spellBelongsToRules(const JsonNode & rules, const std::string & scopedIdentity, bool commonHeroSpell);
}
