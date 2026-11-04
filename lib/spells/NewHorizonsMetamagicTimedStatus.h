/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsMetamagicMarks.h"
#include "../constants/EntityIdentifiers.h"
#include <optional>
struct Bonus;
namespace newHorizonsMetamagic
{
DLL_LINKAGE bool isChaosShieldMagnitude(const Bonus&, SpellID accepted, SpellID family);
DLL_LINKAGE bool isMisfortuneChanceFactor(const Bonus&, SpellID accepted, SpellID family);
DLL_LINKAGE std::optional<int32_t> lightReductionLimit(const Bonus&, SpellID accepted, SpellID family);
DLL_LINKAGE int32_t chaosShieldMagnitude(int32_t ordinary, const Profile&, int32_t marks, int32_t nativeLimit);
DLL_LINKAGE int32_t misfortuneChanceFactor(int32_t ordinary, const Profile&, int32_t marks);
}
