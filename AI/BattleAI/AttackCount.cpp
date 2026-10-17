/*
 * AttackCount.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "AttackCount.h"
#include "AttackPossibility.h"
#include "../../lib/bonuses/BonusList.h"

bool attackCount::hasPendingRangedFollowUp(const battle::Unit * unit)
{
	const auto * state = unit ? dynamic_cast<const battle::CUnitState *>(unit) : nullptr;
	return unit && unit->isBallista() && state && state->rangedFollowUpDamagePercent > 0;
}

int AttackPossibility::getAttackCount(const battle::Unit & attacker, bool shooting, const CBattleInfoCallback & state)
{
	const bool rangedFollowUp = shooting && attackCount::hasPendingRangedFollowUp(&attacker);
	int result = rangedFollowUp ? 1 : attacker.getTotalAttacks(shooting);
	// BattleAction uses the unit's battle side, including when estimating an
	// opponent's action. A player-scoped callback must not probe that opponent's
	// private hero object; use only the attacks already exposed by the unit.
	const auto policy = state.battleGetVisibleHeroPolicy(attacker.unitSide());
	const auto * hero = policy.legacyHero(); // Preserve the existing hidden-policy result.
	if(hero && !rangedFollowUp)
		result += state.battleGetHeroBonuses(attacker.unitSide(), Selector::typeSubtype(
			BonusType::HERO_GRANTS_ATTACKS, BonusSubtypeID(attacker.creatureId())))->totalValue();
	if(shooting)
		if(const auto * unitState = dynamic_cast<const battle::CUnitState *>(&attacker);
			unitState && unitState->shots.isLimited())
			result = std::min(result, unitState->shots.available());
	return result;
}
