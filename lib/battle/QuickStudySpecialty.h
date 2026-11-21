/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "BattleSide.h"
#include "../constants/EntityIdentifiers.h"
#include <set>

class CBattleInfoCallback;
class CGHeroInstance;

/// Sanya's Quick Study (captured revisedSpecialties, format 1166). Its state is
/// derived from each side's serialized completed hero cast history, so seen
/// spells and spent copies survive save/load without battle-state fields.
namespace quickStudySpecialty
{
/// Spells any hero has cast in this combat, both sides.
DLL_LINKAGE std::set<SpellID> seenSpells(const CBattleInfoCallback & battle);
/// The hero cannot cast this spell from her own book, schools or scrolls.
DLL_LINKAGE bool isCopy(const CGHeroInstance & hero, SpellID spell);
/// Completed copies of `hero`'s side this combat.
DLL_LINKAGE int spentCopies(const CBattleInfoCallback & battle, const CGHeroInstance & hero);
/// The hero may cast `spell` now as a Quick Study copy (Mana and targets checked elsewhere).
DLL_LINKAGE bool canCopy(const CBattleInfoCallback & battle, const CGHeroInstance & hero, SpellID spell);
/// Spell Power of a copy: the captured level percentage of the ordinary power.
DLL_LINKAGE int copyEffectPower(const CGHeroInstance & hero, SpellID spell, int ordinaryPower);
}
