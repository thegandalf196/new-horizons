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

class CBattleInfoCallback;
namespace battle { class Unit; }

namespace newHorizonsDivineRetribution
{
/// Absent content is optional only for an empty legacy profile without the module.
/// Installed identities retain historical behavior; required missing content throws.
DLL_LINKAGE SpellID spellId(const JsonNode & savedRules);

/// The caller has already applied the raw judgment cap and Retributionist.
/// Applies current recipient defenses once, without caster bonuses or RNG.
/// Captured pre-v3 contexts retain their existing raw payout.
DLL_LINKAGE int64_t recipientDamage(const CBattleInfoCallback & battle,
	const battle::Unit * recipient, int64_t rawPayout);
}
