/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleInfo.h"
#include "NewHorizonsOffense.h"
#include "../CStack.h"
#include "../mapObjects/CGHeroInstance.h"
void BattleInfo::beginActivationActionState(CStack & stack, uint32_t unitId, BattleUnitTurnReason reason)
{
	CStack * st = &stack;
	st->forestAmbush.beginActivation(reason == BattleUnitTurnReason::TURN_QUEUE && st->waiting);
	if(reason == BattleUnitTurnReason::HERO_COMMAND)
		st->castSpellThisTurn = false; // Only the armed Second Wind target begins a new activation here.
	seizeInitiative.begin(unitId, reason == BattleUnitTurnReason::TURN_QUEUE
		|| reason == BattleUnitTurnReason::AUTOMATIC_ACTION);
	// Last Stand ends the current activation without removing the surviving
	// stack. Keep the marker through all continuations, and clear it only
	// when the stack actually receives a new activation.
	st->armorerLastStandEndedActivation = false;
	st->armorerLastStandDefending = false;
	st->pursuitMovementRemaining = 0;
	st->cleaveUsedThisActivation = false;
	st->dreadCavalryState = {};
	st->luckyOwnAttackSequence = false;
	st->setRangedFollowUpDamagePercent(0);
	const auto side = playerToSide(battleGetOwner(st));
	const bool ordinaryCreature = st->alive() && !st->isGhost() && !st->isTurret()
		&& !st->hasBonusOfType(BonusType::SIEGE_WEAPON)
		&& st->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER;
	const auto * hero = side == BattleSide::ATTACKER || side == BattleSide::DEFENDER
		? battleGetFightingHero(side) : nullptr;
	if(ordinaryCreature && hero
		&& hero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::RELENTLESS_ASSAULT))
		sides.at(side).relentlessAssault.beginActivation();
}
