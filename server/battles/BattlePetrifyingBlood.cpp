/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "BattleActionProcessor.h"
#include "../CGameHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/NewHorizonsPetrifyingBlood.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
void BattleActionProcessor::handlePetrifyingBlood(const CBattleInfoCallback & battle,
	const CStack * attacker, const BattleAttack & attack)
{
	if(!activePetrifyingBlood || activePetrifyingBlood->round != battle.battleGetRound()
		|| !attacker || attack.shot() || attack.spellLike())
		return;
	for(const auto & hit : attack.bsa)
	{
		if(hit.attackerID != attacker->unitId())
			continue; // Reflected damage is not a second incoming melee hit.
		const auto * recipient = dynamic_cast<const CStack *>(battle.battleGetUnitByID(hit.stackAttacked));
		if(!recipient)
			continue;
		auto state = recipient->acquireState();
		const BattleAttackInfo incoming(attacker, recipient, 0, false);
		if(!newHorizonsPetrifyingBlood::qualifies(battle, attacker, *state,
			hit.damageAmount, false, incoming.physicalDamage))
			continue;
		state->petrifyingBloodRound = battle.battleGetRound();
		BattleUnitsChanged spent;
		spent.battleID = battle.getBattle()->getBattleID();
		UnitChanges update(state->unitId(), UnitChanges::EOperation::UPDATE);
		update.data = state->save();
		spent.changedStacks.push_back(std::move(update));
		gameHandler->sendAndApply(spent);
		activePetrifyingBlood->attempts.push_back({recipient->unitId(), attacker->unitId()});
	}
}

void BattleActionProcessor::resolvePetrifyingBlood(const CBattleInfoCallback & battle,
	const PendingPetrifyingBlood & pending)
{
	if(pending.round != battle.battleGetRound())
		return;
	for(const auto & attempt : pending.attempts)
	{
		const auto * recipient = dynamic_cast<const CStack *>(battle.battleGetUnitByID(attempt.recipient));
		const auto * attacker = dynamic_cast<const CStack *>(battle.battleGetUnitByID(attempt.attacker));
		if(!recipient || !attacker || !recipient->alive() || !attacker->alive()
			|| recipient->petrifyingBloodRound != pending.round
			|| !newHorizonsPetrifyingBlood::eligiblePair(battle, attacker, *recipient)
			|| newHorizonsPetrifyingBlood::isPetrified(*attacker))
			continue;
		// Existing native chance/immunity/reroll/cast. Do not refresh an ordinary
		// petrification already applied by the Basilisk's unchanged own attack.
		attackCasting(battle, false, BonusType::SPELL_AFTER_ATTACK, recipient, attacker,
			SpellID(SpellID::STONE_GAZE));
	}
}

bool BattleActionProcessor::doShootAction(const CBattleInfoCallback & battle, const BattleAction & ba,
	bool allowPursuitContinuation)
{
	PetrifyingBloodSequence bloodSequence(activePetrifyingBlood, battle.battleGetRound());
	const bool result = doShootActionCore(battle, ba, allowPursuitContinuation);
	resolvePetrifyingBlood(battle, bloodSequence.result());
	return result;
}
