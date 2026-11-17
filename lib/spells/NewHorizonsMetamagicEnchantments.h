/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
#include "../constants/EntityIdentifiers.h"
class CBattleInfoCallback;
class CGHeroInstance;
struct SetStackEffect;
namespace newHorizonsMetamagic
{
/// Additional components share their accepted beneficial enchantment's source,
/// owner, identity and exact lifetime. No seed means no invented duration.
DLL_LINKAGE void enchantmentCapabilities(const CGHeroInstance *, const CBattleInfoCallback &,
	SpellID accepted, SpellID family, SetStackEffect &);
}
