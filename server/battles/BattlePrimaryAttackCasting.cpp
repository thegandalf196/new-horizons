/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleActionProcessor.h"
#include "BattleSpecialtyAttackControllers.h"
#include "BattleSpecialtyProcs.h"
#include "BattlePrimaryStrikeAftermath.h"
#include "BattleToxicSting.h"
#include "../CGameHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/gameState/CGameState.h"

bool BattleActionProcessor::resolvePrimaryAttackCasts(const CBattleInfoCallback & battle,
	const CStack *& attacker, const CStack *& defender, bool cleaveFollowup,
	const CombatEventPayload & payload, const battlePrimaryStrikeAftermath::Prepared & primaryEffects)
{
	const auto castBattleId = battle.getBattle()->getBattleID();
	const auto * castBattleOwner = gameHandler->gameState().getBattle(castBattleId);
	const auto castSourceId = attacker->unitId();
	const auto castTargetId = defender ? std::optional{defender->unitId()} : std::nullopt;
	if(defender && !cleaveFollowup)
		handleAfterAttackCasting(battle, attacker, defender, payload, primaryEffects.specialtySpellStrike);
	const auto * currentCastBattle = gameHandler->gameState().getBattle(castBattleId);
	if(!currentCastBattle || currentCastBattle != castBattleOwner) return false;
	attacker = currentCastBattle->battleGetStackByID(castSourceId, false);
	defender = castTargetId ? currentCastBattle->battleGetStackByID(*castTargetId, false) : nullptr;
	if(!attacker || (castTargetId && !defender)) return false;
	if(!battleSpecialtyAttackControllers::apply(*gameHandler, *currentCastBattle, primaryEffects.attackController)) return false;
	currentCastBattle = gameHandler->gameState().getBattle(castBattleId);
	if(!currentCastBattle || currentCastBattle != castBattleOwner) return false;
	attacker = currentCastBattle->battleGetStackByID(castSourceId, false);
	defender = castTargetId ? currentCastBattle->battleGetStackByID(*castTargetId, false) : nullptr;
	if(!attacker || (castTargetId && !defender)) return false;
	if(primaryEffects.stunChance > 0)
		battleSpecialtyProcs::resolveStun(*gameHandler, *owner, battle,
			primaryEffects.source, primaryEffects.target, primaryEffects.stunChance);
	battleToxicSting::apply(*gameHandler, *owner, battle, primaryEffects.toxicSting);
	return true;
}
