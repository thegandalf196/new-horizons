/*
 * EnemyBypassReachability.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "EnemyBypassReachability.h"
#include "AttackPossibility.h"
#include "../../lib/battle/NewHorizonsShroud.h"

bool controlledByDifferentPlayer(const CBattleInfoCallback & battle,
	const battle::Unit * first, const battle::Unit * second)
{
	const auto firstOwner = battle.battleGetOwner(first);
	const auto secondOwner = battle.battleGetOwner(second);
	return firstOwner != PlayerColor::CANNOT_DETERMINE
		&& secondOwner != PlayerColor::CANNOT_DETERMINE
		&& firstOwner != secondOwner;
}

ReachabilityInfo getReachabilityWithEnemyBypass(
	const battle::Unit * activeStack,
	DamageCache & damageCache,
	std::shared_ptr<HypotheticBattle> state)
{
	ReachabilityInfo::Parameters params(activeStack, activeStack->getPosition());
	// This path constructs Parameters directly (to account for destructible
	// enemy stacks), so preserve the same Shroud rank gate as the normal
	// callback-created reachability cache. Resolve the current controller through
	// the visibility-aware fighting-hero query rather than reconstructing a hidden
	// opponent hero from its battle side.
	const auto controllerSide = state->playerToSide(state->battleGetOwner(activeStack));
	if(controllerSide == BattleSide::ATTACKER || controllerSide == BattleSide::DEFENDER)
		params.ghostWalk = newHorizonsShroud::rank(state->battleGetFightingHero(controllerSide)) > 0;
	state->configurePassingLines(activeStack, params);

	// Obstacle knowledge belongs to the observer, not the projected mover.
	const auto observer = state->battleGetMySide();
	if(observer != BattleSide::ALL_KNOWING)
		params.perspective = observer;

	if(!params.flying && !params.ghostWalk)
	{
		for(const auto * unit : state->battleAliveUnits())
		{
			if(!controlledByDifferentPlayer(*state, activeStack, unit))
				continue;

			auto dmg = damageCache.getOriginalDamage(activeStack, unit, state);
			auto turnsToKill = unit->getAvailableHealth() / std::max(dmg, (int64_t)1);

			vstd::amin(turnsToKill, 100);

			for(auto & hex : unit->getHexes())
				if(hex.isAvailable()) //towers can have <0 pos; we don't also want to overwrite side columns
					params.destructibleEnemyTurns[hex.toInt()] = turnsToKill * unit->getMovementRange();
		}

		params.bypassEnemyStacks = true;
	}

	return state->getReachability(params);
}
