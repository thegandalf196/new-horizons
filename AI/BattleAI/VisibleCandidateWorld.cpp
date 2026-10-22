/*
 * VisibleCandidateWorld.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "VisibleCandidateWorld.h"
#include "AttackPossibility.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/NewHorizonsArchery.h"
#include <stdexcept>

VisibleCandidateWorld::VisibleCandidateWorld(std::shared_ptr<HypotheticBattle> source)
	: source(std::move(source))
{
	if(!this->source)
		throw std::invalid_argument("Candidate world requires its actual callback source");
}

VisibleHeroPolicy VisibleCandidateWorld::heroPolicy(BattleSide side) const
{
	return source->battleGetVisibleHeroPolicy(side);
}

AttackPossibility VisibleCandidateWorld::evaluate(const std::shared_ptr<const VisibleCandidateWorld> & owner,
	const battle::Unit * attacker, const battle::Unit * defender, int distance,
	bool shooting, const BattleHex & from, DamageCache & damageCache)
{
	if(!owner || !attacker || !defender
		|| owner->source->battleGetUnitByID(attacker->unitId()) != attacker
		|| owner->source->battleGetUnitByID(defender->unitId()) != defender)
		throw std::invalid_argument("Candidate actors must belong to their actual query world");
	auto attack = BattleAttackInfo(attacker, defender, distance, shooting);
	if(shooting && from.isValid() && from != attacker->getPosition()
		&& newHorizonsArchery::canUseSkirmisher(owner->heroPolicy(attacker->unitSide()).legacyHero(), attacker))
	{
		attack.archeryRangedDamageMultiplierPercent = newHorizonsArchery::SKIRMISHER_DAMAGE_PERCENT;
		// Admit movement reactions before the ordinary ranged preview resolves.
		attack.attackerPos = from;
	}
	// Run the same ordinary preview, with the SAME concrete callback object:
	// RTTI/lazy reaction paths, partial/unknown handling and scalar math stay put.
	auto result = AttackPossibility::evaluate(attack, from, damageCache, owner->source);
	result.queryWorld = owner; // Actual borrowed actor/victim/provider lifetime, not shadow evidence.
	return result;
}
