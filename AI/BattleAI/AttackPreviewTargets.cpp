/*
 * AttackPreviewTargets.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "AttackPreviewTargets.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/Unit.h"

namespace
{
void primaryFirst(battle::Units & units, const battle::Unit * primary, bool include)
{
	auto position = std::ranges::find_if(units, [primary](const auto * unit)
	{
		return unit->unitId() == primary->unitId();
	});
	if(position != units.end())
		std::rotate(units.begin(), position, std::next(position));
	else if(include)
		units.insert(units.begin(), primary);
}
}

namespace attackPreview
{
Targets targets(const CBattleInfoCallback & battle, const battle::Unit * attacker,
	const battle::Unit * requested, const battle::Unit * resolved,
	BattleHex from, BattleHex target, bool shooting, bool longReach)
{
	Targets result{{}, {}, {attacker}};
	const bool redirected = resolved->unitId() != requested->unitId();
	if(shooting)
		result.resolved = battle.getAttackedBattleUnits(attacker, resolved, target,
			true, from, resolved->getPosition());
	else if(!longReach)
	{
		// Native collectSecondaryTargets retains attack.targetHex after Protect
		// redirects the primary recipient. Derive direction from the Ward.
		result.resolved = battle.getAttackedBattleUnits(attacker, requested, target,
			false, from, requested->getPosition());
		result.requested = result.resolved;
		const auto removeActor = [attacker](const auto * unit)
		{
			return unit->unitId() == attacker->unitId();
		};
		vstd::erase_if(result.resolved, removeActor);
		vstd::erase_if(result.requested, removeActor);
		// Native getAttackedCreatures returns no collateral when a remote
		// Protector counters. Keep that primary counter, without asking the
		// adjacency-only helper to construct a nonexistent attack direction.
		if(!redirected || !battle.meleeAttackHexes(resolved, attacker,
			resolved->getPosition(), from).empty())
			result.counter = battle.getAttackedBattleUnits(resolved, attacker, from,
				false, resolved->getPosition(), from);
	}
	else
	{
		result.resolved = {resolved};
		result.requested = {requested};
	}
	primaryFirst(result.resolved, resolved, true);
	primaryFirst(result.requested, requested, redirected);
	primaryFirst(result.counter, attacker, true);
	return result;
}
}

void attackPreview::consumeProtect(const std::shared_ptr<HypotheticBattle> & model,
	const CBattleInfoCallback & source, BattleSide side, const battle::Unit * requested, const battle::Unit * actual,
	const std::optional<HeroOrderState> & captured, uint8_t & legacyConsumed)
{
	if(model)
	{
		auto order = model->battleGetHeroOrderState(side, HeroCommand::PROTECT);
		if(order && order->secondaryTargetUnitId == requested->unitId() && order->primaryTargetUnitId == actual->unitId()
			&& order->protectInterceptionsConsumed < model->battleHeroOrderProtectInterceptionLimit(side)
			&& !order->protectBroken)
		{
			++order->protectInterceptionsConsumed;
			model->setHeroOrderState(side, order);
		}
	}
	else if(captured && !captured->protectBroken
		&& legacyConsumed < source.battleHeroOrderProtectInterceptionLimit(side))
		++legacyConsumed;
}
