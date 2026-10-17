/*
 * FrozenAttackResolution.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "FrozenAttackResolution.h"

#include "../CGameHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"

#include <vstd/RNG.h>

FrozenAttackResolution prepareFrozenAttack(const FrozenAttackContext & context, BattleAttack & bat)
{
	const auto & [battle, attacker, defender, distance, ranged, counterAttack] = context;
	FrozenAttackResolution resolution;
	auto & shatter = resolution.shatter;
	shatter.battleID = battle.getBattle()->getBattleID();
	bool primaryResolved = false;
	resolution.primaryFrozenBeforeHit = defender && newHorizonsFrozen::isFrozen(*defender);
	for(auto & hit : bat.bsa)
	{
		// Reflected/indirect injury entries are not this creature's attack.
		if(hit.attackerID != attacker->unitId())
			continue;
		const auto * target = battle.battleGetUnitByID(hit.stackAttacked);
		if(!target)
			continue;
		BattleAttackInfo frozenHit(attacker, target, distance, ranged);
		frozenHit.physicalDamage = frozenHit.physicalDamage && !bat.spellLike();
		frozenHit.secondaryAttack = hit.isSecondary();
		frozenHit.retaliation = counterAttack;
		if(newHorizonsFrozen::qualifiesForShatter(frozenHit, battle.getBattle()->getMagicRules()))
		{
			hit.flags |= BattleStackAttacked::SHATTER;
			shatter.toRemove.emplace_back(target->unitId(), newHorizonsFrozen::removalPlan(*target));
		}
		if(defender && target->unitId() == defender->unitId() && !hit.isSecondary())
			primaryResolved = frozenHit.physicalDamage;
	}
	resolution.freezingTouch = primaryResolved && !ranged
		&& newHorizonsFrozen::isFreezingTouchAttacker(*attacker, battle.getBattle()->getMagicRules());
	return resolution;
}

void resolveFrozenAttack(CGameHandler & gameHandler, const FrozenAttackContext & context,
	FrozenAttackResolution & resolution)
{
	const auto & [battle, attacker, defender, distance, ranged, counterAttack] = context;
	auto & shatter = resolution.shatter;
	if(!shatter.toRemove.empty())
		gameHandler.sendAndApply(shatter);
	// One roll per resolved primary melee blow/retaliation, after injury and
	// before the caller considers its defender's retaliation. Cleansing does
	// not reset the per-round receipt. An already Frozen target cannot refresh
	// its status through the same blow that Shatters it.
	const bool freezingTouchRolled = resolution.freezingTouch && gameHandler.getRandomGenerator().nextInt(0, 99)
		< newHorizonsFrozen::chancePercent(battle.getBattle()->getMagicRules());
	if(freezingTouchRolled && defender && !resolution.primaryFrozenBeforeHit
		&& newHorizonsFrozen::canApply(*defender->acquireState(), battle.battleGetRound()))
	{
		SetStackEffect frozen;
		frozen.battleID = battle.getBattle()->getBattleID();
		frozen.toAdd.emplace_back(defender->unitId(), std::vector<Bonus>{
			newHorizonsFrozen::makeFrozenMarker(BonusSourceID(attacker->creatureId()), battle.battleGetRound())});
		gameHandler.sendAndApply(frozen);
	}
}
