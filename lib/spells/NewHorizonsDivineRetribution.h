/*
 * NewHorizonsDivineRetribution.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include <cstdint>

class CBattleInfoCallback;
namespace battle { class Unit; }

namespace newHorizonsDivineRetribution
{
/// The caller has already applied the raw judgment cap and Retributionist.
/// Applies current recipient defenses once, without caster bonuses or RNG.
/// Captured pre-v3 contexts retain their existing raw payout.
DLL_LINKAGE int64_t recipientDamage(const CBattleInfoCallback & battle,
	const battle::Unit * recipient, int64_t rawPayout);
}
