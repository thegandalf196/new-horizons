/*
 * BraceStagePreparation.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BraceStagePreparation.h"
#include "MovementBonusProvenance.h"
#include "InitialExportBattleSubject.h"
#include "HypotheticBattle.h"
#include "StackWithBonuses.h"
#include "SpellTargetsEvaluator.h"
#include "../../lib/battle/ReachabilityInfo.h"
#include "../../lib/battle/NewHorizonsBattlecraft.h"
#include "../../lib/battle/NewHorizonsShroud.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/bonuses/BonusList.h"

namespace
{
bool nativeHeroBonusIdentitiesMatch(const HypotheticBattle & battle)
{
	const auto subject = battle.initialExportSubject();
	if(!subject)
		return true; // Legacy admission still requires the raw invariant proof.
	subject->synchronizeValues(battle);
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto projected = subject->world().projectedHeroBonuses(side, true);
		const auto * hero = battle.battleGetFightingHero(side);
		if(!hero)
		{
			if(!projected->empty())
				return false;
			continue;
		}
		const auto native = hero->getUnstackedBonuses(Selector::all);
		// Sufficient identity proof under the immutable source lease, NOT value
		// equivalence or a projected CGHeroInstance. Fresh updater copies can
		// conservatively fail this intermediate bridge even when values agree.
		if(!std::ranges::equal(*projected, *native))
			return false;
	}
	return true;
}

bool bonusSourcesSupported(const HypotheticBattle & battle)
{
	// Scan the callback's complete roster, not just currently visible/effective
	// bonus lists: another unit's arrival aura/reaction can precede Brace.
	const auto units = battle.battleGetUnitsIf([](const battle::Unit *) { return true; });
	if(units.size() > 256)
		return false;
	const bool canonical = battle.initialExportSubject() != nullptr;
	for(const auto * unit : units)
	{
		const auto * source = unit ? unit->getBonusBearer() : nullptr;
		const auto * projected = dynamic_cast<const StackWithBonuses *>(source);
		if(canonical ? !projected || !projected->usesCanonicalMovementQuery(battle)
			: !source || !movementBonusProvenance::invariantSources(*source))
			return false;
	}
	if(canonical)
		return nativeHeroBonusIdentitiesMatch(battle);
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		if(const auto * hero = battle.battleGetFightingHero(side))
			if(!movementBonusProvenance::invariantSources(*hero))
				return false;
	return true;
}

bool firstMeleeLoopHasAttack(const HypotheticBattle & battle, const battle::Unit & actor)
{
	const auto side = battle.playerToSide(battle.battleGetActionController(&actor));
	int attacks = actor.getTotalAttacks(false);
	if(battle.battleGetFightingHero(side))
		attacks += battle.battleGetHeroBonuses(side, Selector::typeSubtype(
			BonusType::HERO_GRANTS_ATTACKS, BonusSubtypeID(actor.creatureId())))->totalValue();
	return attacks > 0;
}

bool prefixSupported(const HypotheticBattle & battle, const battle::Unit & actor,
	const battle::Unit & target)
{
	const auto active = battle.battleActiveUnit();
	const auto actorState = actor.acquireState();
	const auto side = battle.playerToSide(battle.battleGetActionController(&actor));
	if(!active || active->unitId() != actor.unitId() || !actor.willMove(0)
		|| (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER) || battle.battleTacticDist()
		|| actorState->pursuitMovementRemaining > 0 || actorState->dreadCavalryState.pending || actorState->rangedFollowUpDamagePercent > 0
		|| actorState->armorerLastStandEndedActivation
		|| battle.battleHasPendingDoubleCommand(side) || battle.battleHasPendingPreCombatOrder(side)
		|| battle.battleGetActionController(&actor) != battle.battleGetOwner(&actor))
		return false;
	if(battle.hasFortifications() || !battle.battleGetAllObstacles().empty()
		|| actor.hasBonusOfType(BonusType::SANCTIFIED)
		|| actor.hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE)
		|| actor.hasBonusOfType(BonusType::SPELL_BEFORE_ATTACK)
		|| actor.hasBonusOfType(BonusType::ON_COMBAT_EVENT)
		|| actor.hasBonusOfType(BonusType::COMBAT_EVENT_TRIGGER)
		|| target.hasBonusOfType(BonusType::FIRST_STRIKE)
		|| newHorizonsShroud::hasNightProwler(battle.battleGetOwnerHero(&actor)))
		return false;
	// Conservative read-only availability guard. Even a currently blocked ready
	// shooter needs a movement-reaction forecast, not an invented no-shot path.
	return !vstd::contains_if(battle.battleAliveUnits(), [&](const battle::Unit * unit)
	{
		return battle.battleOverwatchReady(unit);
	});
}

bool arrivedAtBraceStage(const HypotheticBattle & battle, const battle::Unit & actor,
	const battle::Unit & target)
{
	if(battle.battleResolveHeroOrderTarget(&actor, &target, false) != &target)
		return false;
	if(newHorizonsBattlecraft::preemptiveStrikeDamagePercent(
		battle.battleGetOwnerHero(&target), &target, battle.battleGetRound()) > 0)
		return false;
	return battle.isMeleeAttackPossible(&actor, &target)
		|| battle.isLongWeaponAttack(&actor, &target)
		|| battle.isMeleeAttackPossibleWithLongReach(&actor, &target);
}
}

namespace heroOrderValue
{
std::optional<PreparedBraceStage> prepareBraceStage(const Environment * environment,
	const std::shared_ptr<CBattleInfoCallback> & beforeMovement, const BraceMeleeAction & action)
{
	if(!environment || !beforeMovement || !beforeMovement->getBattle())
		return {};
	auto branch = std::make_shared<HypotheticBattle>(environment, beforeMovement);
	const auto * actor = branch->battleGetUnitByID(action.incomingAttackerId);
	const auto * target = branch->battleGetUnitByID(action.requestedDefenderId);
	if(!actor || !target || !target->coversPos(action.targetHex)
		|| !bonusSourcesSupported(*branch)
		|| !branch->battleCanAttackUnitAction(actor, target) || !prefixSupported(*branch, *actor, *target))
		return {};
	const auto reachability = branch->getReachability(actor);
	if(!vstd::contains(branch->battleGetAvailableHexes(reachability, actor, false), action.attackFrom))
		return {};
	const int distance = SpellTargetEvaluator::physicalTravelDistance(reachability, action.attackFrom);
	if(distance < 0)
		return {};
	const auto movementHits = branch->projectVoluntaryMovement(action.incomingAttackerId, action.attackFrom);
	if(!movementHits.empty() || !nativeHeroBonusIdentitiesMatch(*branch))
		return {}; // A reaction or changed typed-hero view needs its actual continuation owner.
	actor = branch->battleGetUnitByID(action.incomingAttackerId);
	target = branch->battleGetUnitByID(action.requestedDefenderId);
	if(!actor || !target || actor->getPosition() != action.attackFrom || !actor->alive() || !target->alive()
		|| !prefixSupported(*branch, *actor, *target)
		|| !firstMeleeLoopHasAttack(*branch, *actor) || !arrivedAtBraceStage(*branch, *actor, *target))
		return {};
	return PreparedBraceStage{branch, {action.incomingAttackerId, action.requestedDefenderId, distance}};
}
}
