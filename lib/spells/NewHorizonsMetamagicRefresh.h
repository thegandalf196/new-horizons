/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsMetamagicMarks.h"
#include "../constants/EntityIdentifiers.h"
#include <set>
class CBattleInfoCallback;
struct SetStackEffect;
namespace newHorizonsMetamagic
{
/// Current-policy producer replacement only. Native historical refresh rules,
/// inherited auras, explicit removals and cumulative families remain untouched.
DLL_LINKAGE void replaceRecalculatedSpellBonuses(const CastProfile * cast,
	const CBattleInfoCallback & battle, SpellID acceptedSpell, SpellID effectSpell,
	const std::set<uint32_t> & preexistingRecipientIds, SetStackEffect & packet);
}
