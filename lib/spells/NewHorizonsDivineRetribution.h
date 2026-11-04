/*
 * NewHorizonsDivineRetribution.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "../constants/EntityIdentifiers.h"
#include "../json/JsonNode.h"
#include <cstdint>
#include <optional>

class CBattleInfoCallback;
struct Bonus;
namespace battle { class Unit; }

namespace newHorizonsDivineRetribution
{
/// Absent content is optional only for an empty legacy profile without the module.
/// Installed identities retain historical behavior; required missing content throws.
DLL_LINKAGE SpellID spellId(const JsonNode & savedRules);
/// Absence is the historical fixed30% tuple. Presence is1155 metadata and
/// remains a captured scalar; readers never consult later offensive marks.
DLL_LINKAGE std::optional<int32_t> capturedPayoutRatioBasisPoints(const Bonus & marker);
DLL_LINKAGE int32_t payoutRatioBasisPoints(const Bonus & marker);
/// Lua producer boundary only: convert a finite integral numeric receipt into
/// canonical integer metadata on a copy. Native/save readers remain strict.
DLL_LINKAGE void canonicalizeScriptPayoutRatio(Bonus & marker);

/// The caller has already applied the raw judgment cap and Retributionist.
/// Applies current recipient defenses once, without caster bonuses or RNG.
/// Captured pre-v3 contexts retain their existing base; current recipient-only defenses remain separate.
DLL_LINKAGE int64_t recipientDamage(const CBattleInfoCallback & battle,
	const battle::Unit * recipient, int64_t rawPayout,
	PlayerColor spellOwner = PlayerColor::CANNOT_DETERMINE);
}
