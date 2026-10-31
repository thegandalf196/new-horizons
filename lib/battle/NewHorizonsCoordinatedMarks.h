/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsOrdinaryStrike.h"
class CBattleInfoCallback;
struct BattleAttackInfo;
namespace battle { class Unit; }
namespace newHorizonsCoordinatedMarks
{
struct DLL_LINKAGE Penetration
{
	int creatureDefensePercent = 0;
	int physicalProtectionPercent = 0;
};
DLL_LINKAGE Penetration penetration(const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
DLL_LINKAGE bool isMark(const Bonus * bonus);
DLL_LINKAGE bool canMarkReturn(const CBattleInfoCallback & battle, const battle::Unit & actor);
DLL_LINKAGE bool hasMark(const battle::Unit & target);
DLL_LINKAGE bool needsProjection(const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
DLL_LINKAGE int creatureDefenseIgnorePercent(const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
DLL_LINKAGE int physicalProtectionIgnorePercent(const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
DLL_LINKAGE std::vector<Bonus> consumed(const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
DLL_LINKAGE std::shared_ptr<Bonus> consecratingShot(const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
DLL_LINKAGE newHorizonsOrdinaryStrike::EffectPlan prepareReturn(const CBattleInfoCallback & battle,
	const battle::Unit & actor, const battle::Unit & victim);
DLL_LINKAGE newHorizonsOrdinaryStrike::EffectPlan completedReturn(const CBattleInfoCallback & battle,
	const battle::Unit & actor, const battle::Unit & victim, bool actuallyReturned);
}
