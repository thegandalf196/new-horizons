/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "BattleUnitTurnReason.h"
#include "../bonuses/Bonus.h"
#include <optional>
#include <vector>
class BattleInfo;
class CBattleInfoCallback;
class CGHeroInstance;
struct BattleAttackInfo;
namespace battle { class Unit; }
namespace newHorizonsPrismaticVanguard
{
struct ActivationPlan { std::vector<Bonus> remove; std::vector<Bonus> add; };
DLL_LINKAGE bool isEffect(const Bonus * bonus);
/// Initial deployment or validated fresh body addition; never call from localInit or healing.
DLL_LINKAGE std::optional<Bonus> entryBonus(const CGHeroInstance * hero, CreatureID creature);
DLL_LINKAGE void captureInitialEntries(BattleInfo & battle);
DLL_LINKAGE ActivationPlan activationPlan(const CBattleInfoCallback & battle,
	const battle::Unit & unit, BattleUnitTurnReason reason);
DLL_LINKAGE int activeDamagePercent(const BattleAttackInfo & attack);
}
