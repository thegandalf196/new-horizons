/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsOrdinaryStrike.h"
#include <string>
#include <string_view>
#include <vector>
class CGHeroInstance;
class CBattleInfoCallback;
struct BattleAttackInfo;
namespace battle { class Unit; }

/// Sylvan Tactics policy 4: the revised Dappled Cover, Quick Redeployment and
/// Hunting Pack slots. Their state lives in recipient bonuses that expire at
/// the recipient's next genuine Creature Activation, so policy-3 captures and
/// their saved SylvanTacticsState histories keep their original meaning.
namespace newHorizonsSylvanTacticsRevision
{
inline constexpr int DAPPLED_COVER_MINIMUM_HEXES = 3;
inline constexpr int DAPPLED_COVER_REDUCTION_PERCENT = 20;
inline constexpr int QUICK_REDEPLOYMENT_INITIATIVE = 2;
DLL_LINKAGE bool revised(const CGHeroInstance * hero);
/// True only for a policy-4 hero with that selected Sylvan Tactics perk slot.
DLL_LINKAGE bool active(const CGHeroInstance * hero, std::string_view perk);

DLL_LINKAGE bool isDappledMovement(const Bonus * bonus);
/// Accumulate the hexes a mover voluntarily crossed in its current activation.
/// Only call with validated voluntary provenance and committed movement.
DLL_LINKAGE newHorizonsOrdinaryStrike::EffectPlan dappledMovement(const CBattleInfoCallback & battle,
	const battle::Unit & unit, bool voluntary, int hexes);
DLL_LINKAGE int dappledCoverPercent(const battle::Unit * unit);

DLL_LINKAGE bool isQuickRedeployment(const Bonus * bonus);
/// Ordinary retaliation by a friendly creature; refreshes without stacking.
DLL_LINKAGE newHorizonsOrdinaryStrike::EffectPlan quickRedeployment(const CBattleInfoCallback & battle,
	const BattleAttackInfo & attack);
DLL_LINKAGE int quickRedeploymentInitiative(const battle::Unit * unit);

DLL_LINKAGE bool huntingPackConfigured(const CBattleInfoCallback & battle, const battle::Unit * attacker);
/// Enemy targets that have not completed an action this round; Waiting is not an action.
DLL_LINKAGE int huntingPackPercent(const CBattleInfoCallback & battle, const battle::Unit * attacker,
	const battle::Unit * target);

/// Recipient-observed inspector rows for the revised effects.
DLL_LINKAGE std::vector<std::string> statusRows(const battle::Unit * unit);
}
