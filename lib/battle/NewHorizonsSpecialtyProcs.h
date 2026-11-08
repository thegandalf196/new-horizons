/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../bonuses/Bonus.h"
#include "../json/JsonNode.h"
#include <optional>
#include <vector>
class CBattleInfoCallback;
struct BattleAttackInfo;
namespace battle { class Unit; }
namespace spells { class Mechanics; }
namespace newHorizonsSpecialtyProcs
{
DLL_LINKAGE std::vector<SpellID> additionalAttackSpells(const CBattleInfoCallback &, const battle::Unit &, bool ranged);
DLL_LINKAGE std::optional<int> attackSpellChance(const CBattleInfoCallback &, const battle::Unit &, SpellID, bool ranged);
DLL_LINKAGE int poisonPotencyMultiplier(const CBattleInfoCallback &, const battle::Unit &);
DLL_LINKAGE bool isStrongWyvernPoison(const Bonus *);
DLL_LINKAGE bool isStrongWyvernPoisonCast(const spells::Mechanics &);
DLL_LINKAGE void scaleNativePoison(JsonMap &, int multiplier);
DLL_LINKAGE void scaleNativePoisonForCast(const spells::Mechanics &, JsonMap &);
DLL_LINKAGE int poisonRampValue(const battle::Unit &, const Bonus & currentHealth);
DLL_LINKAGE std::vector<Bonus> poisonBonusPlan(const CBattleInfoCallback &, const battle::Unit &, int duration);
struct DLL_LINKAGE PoisonRefresh
{
	std::vector<Bonus> remove;
	std::vector<Bonus> add;
};
DLL_LINKAGE PoisonRefresh poisonRefreshPlan(const battle::Unit &, std::vector<Bonus> incoming);
DLL_LINKAGE bool isPositiveTemporaryMagic(const Bonus &);
DLL_LINKAGE std::vector<Bonus> dispelRemovalPlan(const battle::Unit &);
DLL_LINKAGE bool isNullifyingShotCast(const spells::Mechanics &);
DLL_LINKAGE int stunChance(const CBattleInfoCallback &, const BattleAttackInfo &);
}
