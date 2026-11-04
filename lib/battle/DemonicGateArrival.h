/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "BattleSide.h"
#include "../bonuses/Bonus.h"
#include <vector>
class CBattleInfoCallback;
class CGHeroInstance;
namespace battle { class Unit; }
namespace newHorizonsDemonicGating
{
inline constexpr int INFERNAL_ONSLAUGHT_DAMAGE_PERCENT = 15;
inline constexpr int HELLSTRIDE_SPEED = 2;
struct ArrivalPerks
{
	const CGHeroInstance * hero;
	bool swift;
	bool hellfire;
	bool reinforced;
	bool beacon;
	bool discipline;
	bool temporary;
	bool onslaught = false;
	bool hellstride = false;
};
DLL_LINKAGE ArrivalPerks arrivalPerks(const CGHeroInstance *);
DLL_LINKAGE int64_t reinforcedHitPoints(int64_t currentHealth);
DLL_LINKAGE int64_t hellfireRawPerEnemy(int64_t currentHealth, size_t enemyCount);
DLL_LINKAGE bool beaconEligible(const CBattleInfoCallback &, const battle::Unit &, BattleSide);
DLL_LINKAGE std::vector<Bonus> arrivalBonuses(const ArrivalPerks &, bool endPhase, bool beacon);
DLL_LINKAGE std::vector<const battle::Unit *> hellfireTargets(const CBattleInfoCallback &,
	const battle::Unit &, BattleSide, bool temporary);
DLL_LINKAGE int64_t hellfireDamage(const CBattleInfoCallback &, const battle::Unit &, int64_t raw, bool temporary);
}
