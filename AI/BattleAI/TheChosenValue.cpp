/*
 * TheChosenValue.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "TheChosenValue.h"
#include "AttackPossibility.h"
#include "../../lib/spells/CSpell.h"
#include <set>

namespace theChosenValue
{
bool isTheChosen(const CSpell * spell)
{
	return spell && spell->getJsonKey() == "new-horizons:theChosen";
}

namespace
{
bool carriesEffect(const battle::Unit * unit, SpellID spell)
{
	return unit && unit->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
}

std::optional<bool> contactMode(const CBattleInfoCallback & battle,
	const battle::Unit * attacker, const battle::Unit * target)
{
	if(attacker->isTimeStopped() || attacker->isFrozen() || target->isInvincible()
		|| !battle.battleMatchActionController(attacker, target))
		return {};
	for(const auto & hex : target->getHexes())
		if(hex.isValid() && battle.battleCanShootAction(attacker, hex))
			return true;
	if(attacker->isMeleeAttacker() && battle.isMeleeAttackPossible(attacker, target))
		for(const auto & from : attacker->getHexes())
			for(const auto & to : target->getHexes())
				if(from.isValid() && to.isValid() && BattleHex::getDistance(from, to) == 1)
					return false;
	return {};
}

float exchangeValue(const std::shared_ptr<CBattleInfoCallback> & battle,
	const battle::Unit * attacker, const battle::Unit * target, bool shooting, DamageCache & cache)
{
	return AttackPossibility::evaluate(BattleAttackInfo(attacker, target, 0, shooting),
		attacker->getPosition(), cache, battle).attackValue();
}
}

float marginalValue(BattleSide side, uint32_t activeUnitId,
	const std::shared_ptr<CBattleInfoCallback> & before,
	const std::shared_ptr<HypotheticBattle> & after, SpellID spell,
	DamageCache & beforeCache, DamageCache & afterCache,
	float positiveMultiplier, float negativeMultiplier)
{
	if(!before || !after)
		return 0.0f;
	const auto units = before->battleGetAllUnits(true);
	std::set<uint32_t> recipients;
	for(const auto * unit : units)
		if(unit && (carriesEffect(unit, spell)
			|| carriesEffect(after->battleGetUnitByID(unit->unitId()), spell)))
			recipients.insert(unit->unitId());
	if(recipients.empty())
		return 0.0f;
	float result = 0.0f;
	for(const auto * attacker : units)
	{
		if(!attacker || !attacker->alive() || attacker->isGhost() || attacker->isTurret()
			|| attacker->unitId() == activeUnitId)
			continue;
		const auto * projectedAttacker = after->battleGetUnitByID(attacker->unitId());
		if(!projectedAttacker || !projectedAttacker->alive())
			continue;
		float bestBefore = 0.0f;
		float bestAfter = 0.0f;
		for(const auto * target : units)
		{
			if(!target || !target->alive() || target->isGhost() || target->isTurret()
				|| target->unitId() == activeUnitId
				|| before->battleGetOwner(target) == before->battleGetOwner(attacker))
				continue;
			const auto * projectedTarget = after->battleGetUnitByID(target->unitId());
			if(!projectedTarget || !projectedTarget->alive())
				continue;
			// Include old recipients as well as the new one: retargeting can lose
			// more value than it gains. Native non-stacking decides natural overlap.
			if(!recipients.contains(attacker->unitId()) && !recipients.contains(target->unitId()))
				continue;
			const auto mode = contactMode(*before, attacker, target);
			if(!mode)
				continue;
			bestBefore = std::max(bestBefore, exchangeValue(before, attacker, target, *mode, beforeCache));
			bestAfter = std::max(bestAfter, exchangeValue(after, projectedAttacker, projectedTarget, *mode, afterCache));
		}
		const auto delta = bestAfter - bestBefore;
		const auto signedDelta = before->battleGetOwner(attacker) == before->sideToPlayer(side) ? delta : -delta;
		result += signedDelta * (signedDelta >= 0.0f ? positiveMultiplier : negativeMultiplier);
	}
	return std::isfinite(result) ? result : 0.0f;
}
}
