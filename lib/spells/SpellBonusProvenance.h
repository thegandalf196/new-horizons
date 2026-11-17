/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
#include "../constants/EntityIdentifiers.h"
struct Bonus;
struct SetStackEffect;
class CBattleInfoCallback;
namespace spells
{
class Mechanics;
DLL_LINKAGE void stampSpellEffectOrigins(const Mechanics &, const CBattleInfoCallback &,
	PlayerColor caster, SpellID cast, SpellID effect, SetStackEffect &);
/// Current accepted effect ownership, shared by packets and numeric previews.
DLL_LINKAGE void stampSpellBonusProvenance(Bonus &, SpellID cast, SpellID effect,
	PlayerColor caster, PlayerColor recipient);
}
