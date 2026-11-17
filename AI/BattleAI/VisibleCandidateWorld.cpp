/*
 * VisibleCandidateWorld.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "VisibleCandidateWorld.h"
#include "AttackPossibility.h"
#include "HypotheticBattle.h"
#include "ForecastState.h"
#include "ForecastAcceptedSnapshotSeal.h"
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
	bool shooting, const BattleHex & from, DamageCache & damageCache, int validatedVoluntaryFlightDistance, bool validatedVoluntaryApproach)
{
	if(!owner || !attacker || !defender
		|| owner->source->battleGetUnitByID(attacker->unitId()) != attacker
		|| owner->source->battleGetUnitByID(defender->unitId()) != defender)
		throw std::invalid_argument("Candidate actors must belong to their actual query world");
	auto attack = BattleAttackInfo(attacker, defender, distance, shooting);
	if(validatedVoluntaryFlightDistance < 0 || validatedVoluntaryFlightDistance > GameConstants::BFIELD_SIZE
		|| (validatedVoluntaryFlightDistance > 0 && (shooting || !from.isValid()
			|| !attacker->hasBonusOfType(BonusType::FLYING) || validatedVoluntaryFlightDistance != distance)))
		throw std::invalid_argument("Invalid accepted candidate flight fact");
	attack.voluntaryFlightDistance = validatedVoluntaryFlightDistance;
	attack.voluntaryTravelDistance = validatedVoluntaryApproach && !shooting
		&& from.isAvailable() && from != attacker->getPosition() ? distance : 0;
	if(shooting && from.isValid() && from != attacker->getPosition()
		&& newHorizonsArchery::canUseSkirmisher(owner->heroPolicy(attacker->unitSide()).legacyHero(), attacker))
	{
		attack.archeryRangedDamageMultiplierPercent = newHorizonsArchery::SKIRMISHER_DAMAGE_PERCENT;
		// Admit movement reactions before the ordinary ranged preview resolves.
		attack.attackerPos = from;
	}
	// Run the same ordinary preview, with the SAME concrete callback object:
	// RTTI/lazy reaction paths, partial/unknown handling and scalar math stay put.
	const auto before = owner->source->forecastState();
	auto stamp = before ? std::make_shared<const forecast::State>(*before) : nullptr;
	auto result = AttackPossibility::evaluate(attack, from, damageCache, owner->source, false, std::nullopt, nullptr,
		validatedVoluntaryApproach && from.isAvailable() && from != attacker->getPosition(), attacker->getPosition());
	finishProduced(result, owner, std::move(stamp));
	return result;
}

void VisibleCandidateWorld::finishProduced(AttackPossibility & result,
	const std::shared_ptr<const VisibleCandidateWorld> & owner, std::shared_ptr<const forecast::State> stamp)
{
	result.forecastSource = std::move(stamp);
	result.queryWorld = owner; // Actual source interval; not a claim of immutable snapshot contents.
	if(result.forecastSource)
		result.snapshotSeal = std::shared_ptr<const forecast::AcceptedSnapshotSeal>(new forecast::AcceptedSnapshotSeal(result));
}
AttackPossibility VisibleCandidateWorld::evaluateRefreshed(const std::shared_ptr<const VisibleCandidateWorld> & owner,
	const BattleAttackInfo & attack, const BattleHex & from, DamageCache & cache,
	bool validatedApproach, BattleHex approachOrigin)
{
	if(!owner || !attack.attacker || !attack.defender
		|| owner->source->battleGetUnitByID(attack.attacker->unitId()) != attack.attacker
		|| owner->source->battleGetUnitByID(attack.defender->unitId()) != attack.defender)
		throw std::invalid_argument("Refreshed actors must belong to their actual query world");
	const auto before = owner->source->forecastState();
	auto stamp = before ? std::make_shared<const forecast::State>(*before) : nullptr;
	// EXACT previous Forest Ambush re-evaluation arguments and attack metadata.
	auto result = AttackPossibility::evaluate(attack, from, cache, owner->source, false, std::nullopt, nullptr,
		validatedApproach, approachOrigin);
	finishProduced(result, owner, std::move(stamp));
	return result;
}
