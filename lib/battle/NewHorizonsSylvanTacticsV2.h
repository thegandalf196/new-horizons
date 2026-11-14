/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../constants/EntityIdentifiers.h"
#include "BattleHex.h"
#include <string_view>
class CGHeroInstance;
class CBattleInfoCallback;
struct Bonus;
struct DamageAttackInfo;
struct BattleAttackInfo;
struct SetStackEffect;
namespace battle { class Unit; }
namespace spells { class Mechanics; }
namespace newHorizonsSylvanTacticsV2
{
enum class Family { Centaur, Dwarf, Elf, Pegasus, Dendroid, Unicorn, Dragon };
DLL_LINKAGE bool enabled(const CGHeroInstance * hero);
DLL_LINKAGE bool family(const battle::Unit * unit, Family line);
DLL_LINKAGE const CGHeroInstance * commandingHero(const CBattleInfoCallback & battle, const battle::Unit * unit);
DLL_LINKAGE int perkValue(const CGHeroInstance * hero, std::string_view perk, std::string_view field);
DLL_LINKAGE bool hasPerk(const CGHeroInstance * hero, std::string_view perk);
DLL_LINKAGE bool huntingPackConfigured(const CBattleInfoCallback &, const battle::Unit * attacker);
DLL_LINKAGE int huntingPackBonus(const CBattleInfoCallback &, const battle::Unit * attacker, const battle::Unit * target, BattleHex targetPosition = BattleHex::INVALID);
DLL_LINKAGE int briarGripReduction(const CBattleInfoCallback &, const battle::Unit * attacker);
DLL_LINKAGE int disarmingShotRetaliations(const CBattleInfoCallback &, const battle::Unit * attacker,
	const battle::Unit * victim, bool ranged, int64_t actualDamage);
DLL_LINKAGE bool spareFriendlyBreath(const CBattleInfoCallback &, const battle::Unit * attacker, const battle::Unit * victim);
DLL_LINKAGE int extraDamper(const CBattleInfoCallback &, const battle::Unit * provider);
DLL_LINKAGE void finalizeDamage(const CBattleInfoCallback &, const BattleAttackInfo &, DamageAttackInfo &);
DLL_LINKAGE int adjustedTimedRounds(const spells::Mechanics &, const battle::Unit * recipient, int rounds);
DLL_LINKAGE void adjustAppliedBonuses(const spells::Mechanics &, SetStackEffect &);
DLL_LINKAGE bool preservesBuffFromDispel(const spells::Mechanics &, const battle::Unit *, const Bonus &);
DLL_LINKAGE bool ignoresNativeLevelImmunity(const spells::Mechanics &, const battle::Unit *, const Bonus &);
DLL_LINKAGE bool levelImmunityAllows(const spells::Mechanics &, const battle::Unit *, bool absolute);
}
