/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "GnollHarriersProjection.h"
#include "AttackPossibility.h"
#include "HypotheticBattle.h"
#include "StackWithBonuses.h"
#include "../../lib/battle/NewHorizonsGnollHarriers.h"
#include "../../lib/battle/NewHorizonsSylvanTacticsRevision.h"
#include "../../lib/bonuses/BonusList.h"
namespace gnollHarriersAI
{
bool replaysSameApproach(const AttackPossibility & captured, const battle::Unit & originalActor)
{
	return captured.validatedVoluntaryApproach && captured.attack.attacker
		&& captured.attack.attacker->unitId() == originalActor.unitId()
		&& captured.from.isAvailable() && captured.validatedApproachOrigin.isAvailable()
		&& captured.from != captured.validatedApproachOrigin
		&& originalActor.getPosition() == captured.validatedApproachOrigin;
}
void record(HypotheticBattle & battle, uint32_t id, BattleHex before, BattleHex after)
{
	auto mover = battle.getForUpdate(id);
	if(!mover)
		return;
	const auto marker = newHorizonsGnollHarriers::movement(battle, *mover, true, before, after);
	if(marker)
		mover->addUnitBonus({*marker});
	// Sylvan Tactics 4.1 Dappled Cover shares this voluntary-movement receipt.
	// Callers record each committed step, or one flight, so the hex distance is the path.
	const auto dappled = newHorizonsSylvanTacticsRevision::dappledMovement(battle, *mover, true,
		BattleHex::getDistance(before, after));
	if(!dappled.remove.empty())
		mover->removeUnitBonus(dappled.remove);
	if(!dappled.add.empty())
		mover->addUnitBonus(dappled.add);
}
void restoreEffects(HypotheticBattle & battle, const AttackPossibility & captured, bool committedPrefix)
{
	if(!committedPrefix || !captured.effectPreview || !captured.attackerState)
		return;
	const auto id = captured.attackerState->unitId();
	const auto * source = captured.effectPreview->battleGetUnitByID(id);
	const auto * current = battle.battleGetUnitByID(id);
	if(!source || !current)
		return;
	const CSelector dappled(newHorizonsSylvanTacticsRevision::isDappledMovement);
	if(const auto progress = source->getUnstackedBonuses(dappled); !progress->empty())
	{
		auto target = battle.getForUpdate(id);
		target->removeUnitBonus(dappled);
		target->addUnitBonus({*progress->front()});
	}
	const CSelector history(newHorizonsGnollHarriers::isMovementHistory);
	if(current->hasBonus(history))
		return;
	const auto saved = source->getUnstackedBonuses(history);
	if(!saved->empty())
		battle.getForUpdate(id)->addUnitBonus({*saved->front()});
	// Absence is never an expiry receipt. Only the real activation clock clears history.
}
}
