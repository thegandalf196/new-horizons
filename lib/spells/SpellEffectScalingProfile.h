/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
#include "../battle/BattleSide.h"
#include <cstdint>
class CGHeroInstance;
namespace newHorizonsMetamagic
{
/// Immutable ordinary spell output facts; no Metamagic resource or passive rules.
struct DLL_LINKAGE Profile
{
	int32_t wisdomEffectivenessBonus = 0;
};
struct DLL_LINKAGE CastProfile
{
	Profile perks;
	const CGHeroInstance * hero = nullptr;
	BattleSide heroSide = BattleSide::NONE;
	int32_t factionSpellDamageBonus = 0;
	int32_t chaosShieldLimitBasisPoints = 8000;
};
/// Ordinary integer floors and saturating effect magnitudes.
DLL_LINKAGE int64_t scaleMagnitude(int64_t ordinary, const Profile & profile);
DLL_LINKAGE int64_t spellDamageOutput(int64_t ordinary, const Profile & profile,
	int64_t offensiveDamageCap, int32_t additionalBonusPercent = 0);
DLL_LINKAGE int32_t scaleSignedBonus(int32_t ordinary, const Profile & profile);
DLL_LINKAGE int32_t scaleDuration(int32_t ordinary, const Profile & profile);
}
