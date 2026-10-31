/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsOrdinaryStrike.h"
#include "BattleSide.h"
#include <optional>
#include <span>
class CBattleInfoCallback;
class CGHeroInstance;
struct BattleAttackInfo;
namespace battle { class Unit; }
namespace newHorizonsReactiveDefense
{
struct DLL_LINKAGE Destruction
{
	uint32_t unit;
	BattleSide side;
};
/// Caller supplies only a validated combat destruction, excluding successful native Rebirth.
DLL_LINKAGE std::optional<Destruction> captureDestruction(const battle::Unit &);
/// Query only after all damage in the accepted packet has committed.
DLL_LINKAGE std::vector<newHorizonsOrdinaryStrike::EffectPlan> solidarityAftermath(
	const CBattleInfoCallback &, std::span<const Destruction>);
/// Player forecasts must use this visibility-aware original-side policy query.
DLL_LINKAGE int solidarityConfiguredPercent(const CBattleInfoCallback &, BattleSide);
/// Only an actual native-authority or already-authorized ALL_KNOWING captured hero.
/// A hero pointer itself grants no authority; never bypass the callback query above.
DLL_LINKAGE int solidarityCapturedPercent(const CGHeroInstance *);
DLL_LINKAGE newHorizonsOrdinaryStrike::EffectPlan solidarityProtection(const battle::Unit &, int capturedPercent);
DLL_LINKAGE bool isSolidarityProtection(const Bonus *);
DLL_LINKAGE bool isIronHideSpent(const Bonus *);
DLL_LINKAGE int ironHideConfiguredPercent(const CBattleInfoCallback &, const battle::Unit &);
DLL_LINKAGE int ironHidePercent(const CBattleInfoCallback &, const BattleAttackInfo &);
/// Prepare before the hit; publish only after the accepted primary hit commits, even at zero damage.
DLL_LINKAGE std::optional<Bonus> ironHideSpending(const CBattleInfoCallback &, const BattleAttackInfo &);
}
