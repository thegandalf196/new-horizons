/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../entities/hero/NewHorizonsCommandState.h"
#include "../bonuses/Bonus.h"
#include <vector>

class CStack;
class CGHeroInstance;
namespace battle { class Unit; }
namespace commandArmyBonuses
{
/// Pure stat projection. Each Warding source remains a separate bonus record.
DLL_LINKAGE std::vector<Bonus> project(const newHorizonsHeroes::CommandSlotModifiers & modifiers);
/// A physical slot number alone is never sufficient provenance.
DLL_LINKAGE const CGHeroInstance * originHero(const CStack & stack);
/// Replaces only this adapter's local battle bonuses, before health initialization.
/// Never writes training to a permanent army stack or changes a health ledger.
DLL_LINKAGE void initialize(CStack & stack);
/// Read-only native/AI damage seam, including magical ordinary attacks/retaliations.
DLL_LINKAGE int directAttackPercent(const battle::Unit & unit);
DLL_LINKAGE std::vector<int> directAttackMultipliers(const battle::Unit & unit);
/// Warding and Protective Training remain independent sources in the shared MDR fold.
DLL_LINKAGE std::vector<int> magicalReductionBasisPoints(const battle::Unit & unit);
/// Shared recognition for direct Bonus-only codec admission by the serialization owner.
DLL_LINKAGE bool isProjectedBonus(const Bonus & bonus);
DLL_LINKAGE void validateSerialization(const CStack & stack, bool supported);
}
