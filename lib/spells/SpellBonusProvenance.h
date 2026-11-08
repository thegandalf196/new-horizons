/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
#include "../constants/EntityIdentifiers.h"
struct Bonus;
namespace spells
{
/// Current accepted effect ownership, shared by packets and numeric previews.
DLL_LINKAGE void stampSpellBonusProvenance(Bonus &, SpellID cast, SpellID effect,
	PlayerColor caster, PlayerColor recipient);
}
