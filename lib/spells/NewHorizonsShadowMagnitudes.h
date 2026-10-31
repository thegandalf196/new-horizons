/*
 * NewHorizonsShadowMagnitudes.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <cstdint>

class CGHeroInstance;

namespace newHorizonsMagic
{
/// Portion of actual Life Drain damage healed, before recipient wound limits.
DLL_LINKAGE int32_t lifeDrainHealingPercent(const CGHeroInstance * hero);

/// Pure magnitude with the already-composed cast coefficient. Night Feeder is
/// applied after the ordinary base cap. This is a lifesteal rate, not healed HP.
DLL_LINKAGE int32_t vampirismHealBasisPoints(int32_t rawSpellPower, int32_t coefficientBasisPoints,
	int32_t warcastingBonusPercent, int32_t empowerBonusPercent, bool nightFeeder);

/// Pure Doom magnitude. Only its Spell Power term is scaled; fixed35 and cap60
/// remain unchanged. Initiative handling and recipient eligibility stay outside.
DLL_LINKAGE int32_t doomCripplingPenaltyPercent(int32_t rawSpellPower, int32_t coefficientBasisPoints,
	int32_t warcastingBonusPercent, int32_t empowerBonusPercent);
}
