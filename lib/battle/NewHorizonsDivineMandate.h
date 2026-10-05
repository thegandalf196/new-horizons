/*
 * NewHorizonsDivineMandate.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../Global.h"

#include <cstdint>

class CGHeroInstance;

namespace newHorizonsDivineMandate
{
/// Normal Spell Points recovered by Chaplain's Reserve when the first completed
/// Divine Mandate pair transitions from zero to one in the current combat.
DLL_LINKAGE int32_t chaplainReserveRecovery(const CGHeroInstance * hero,
	uint8_t beforeCompletedPairs, uint8_t afterCompletedPairs);
/// Spell Power-derived percentage captured for an eligible Divine Mandate cast.
DLL_LINKAGE int32_t consecratedCastingBonusPercent(const CGHeroInstance * hero);
}
