/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "BattleSearchPolicy.h"
#include "BattleExchangeVariant.h"
#include "VisibleCandidateWorld.h"
#include "CreatureCandidateScreening.h"
#include <algorithm>
#include <cmath>
#include "../../lib/GameLibrary.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/HeroCommandRevision.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/bonuses/OwnedBonusPayloads.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/combatScripts/IDamageCalculatorScript.h"
#include "../../lib/scripting/ScriptService.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include <chrono>

namespace
{
class MovementExchangeScope
{
	using Clock = std::chrono::steady_clock;
	BattleMovementTiming::StageTiming * timing;
	Clock::time_point started;
public:
	explicit MovementExchangeScope(BattleMovementTiming::StageTiming * timing)
		: timing(timing), started(timing ? Clock::now() : Clock::time_point{})
	{}
	~MovementExchangeScope()
	{
		finish();
	}
	void finish()
	{
		if(timing)
		{
			++timing->calls;
			timing->totalMicroseconds += std::chrono::duration_cast<std::chrono::microseconds>(
				Clock::now() - started).count();
			timing = nullptr;
		}
	}
};

}

namespace
{
// Hero effects remain owned by the retained AP/full exchange. Native identity,
// skill and perk choices affect search quality, not this input provenance gate.
bool supportedHero(const VisibleHeroPolicy & policy, bool actorSide)
{
	using Knowledge = VisibleHeroPolicy::Knowledge;
	// Hidden stays hidden. The opposing policy is not declared absent/static:
	// the SAME visible full evaluator also receives no native Hero address.
	// Only its observable units/APs and public action states are screened here.
	if(policy.kind() == Knowledge::HIDDEN_UNKNOWN)
		return !actorSide;
	if(policy.kind() == Knowledge::KNOWN_ABSENT)
		return true;
	if(policy.kind() != Knowledge::VISIBLE_HERO)
		return false;
	const auto * hero = policy.legacyHero();
	if(!hero || !hero->getHeroType())
		return false;
	// Native init/load owns captured-rule validation. Valid saved policies may
	// predate installed defaults; retained AP/full scoring reads those captures.
	// Preserve malformed native skill-reference rejection independently of policy.
	if(std::ranges::any_of(hero->secSkills, [](const auto & entry)
		{ return entry.second != 0 && !entry.first.toSkill(); }))
		return false;
	return true;
}

bool supportedBonus(const Bonus & bonus)
{
	// Whole-battle bonuses and native Vanguard grants retain their full queries.
	// Screening does not replace or memoize limiter decisions; existing bonus
	// and full queries retain ownership. Other payload/expiry guards remain.
	if((bonus.duration != BonusDuration::PERMANENT && bonus.duration != BonusDuration::ONE_BATTLE
			&& !(heroCommands::isVanguardBonus(&bonus) && bonus.turnsRemain > 0))
		|| bonus.updater || bonus.propagationUpdater
		|| !bonus.statusIdentity.empty() || !bonus.statusTags.empty() || bonus.parameters)
		return false;
	return true;
}

bool supportedOrdinaryState(const battle::Unit * actor, const HypotheticBattle & state)
{
	if(!actor
		|| state.usesOwnedFrameQueries() || !state.hasCanonicalDamageQuerySources()
		|| !state.getBattle() || !heroCommands::isRevisedRules(state.getBattle()->getHeroCommandRules())
		|| state.getDefendedTown() != nullptr)
		return false;
	const auto & crisis = state.getBattle()->getCrisisCommandState();
	if(!crisis.pending.empty() || !crisis.returns.empty())
		return false;
	// Native fixed rocks/terrain use existing reachability and have no lifecycle.
	// Spell-created, moat and custom obstacle behavior stays on the full path.
	for(const auto & obstacle : state.battleGetAllObstacles())
		if(!obstacle || typeid(*obstacle) != typeid(CObstacleInstance)
			|| (obstacle->obstacleType != CObstacleInstance::USUAL
				&& obstacle->obstacleType != CObstacleInstance::ABSOLUTE_OBSTACLE))
			return false;
	const auto * current = state.battleGetUnitByID(actor->unitId());
	if(!current || !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(current)
		|| current->hasBonusOfType(BonusType::FLYING)
		|| state.battleGetActionController(current) != state.battleGetOwner(current)
		|| state.battleGetActivationOutputPercent(current) != 100
		|| state.battleIsMoraleExtraActivation(current)
		|| current->acquireState()->pursuitMovementRemaining > 0
		|| current->acquireState()->rangedFollowUpDamagePercent > 0)
		return false;
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		if(!supportedHero(state.battleGetVisibleHeroPolicy(side),
			state.playerToSide(state.battleGetActionController(current)) == side)
			|| std::ranges::any_of(state.getBattle()->getHeroOrderStates(side),
				[](const HeroOrderState & order) { return order.secondWindActive; })
			|| state.battleHasPendingDoubleCommand(side) || state.battleHasPendingPreCombatOrder(side)
			|| state.getBattle()->getDoubleCommandState(side).secondWindReady()
			|| state.getBattle()->getDoubleCommandState(side).deferredSecondWindTargetUnitId != DoubleCommandState::INVALID_UNIT_ID
			|| state.battleMetamagicPendingCount(side) != 0)
			return false;
	for(const auto * unit : state.battleAliveUnits())
	{
		const auto unitState = unit->acquireState();
		if(unit->isClone() || unit->isGhost() || unitState->summoned
			|| (unitState->physicalPoisonBaseDamage > 0 && unitState->physicalPoisonActivationsRemaining > 0)
			|| unit->unitSlot() == SlotID::WAR_MACHINES_SLOT
			|| !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(unit)
			|| state.battleGetActionController(unit) != state.battleGetOwner(unit))
			return false;
		const auto observedBonuses = unit->getAllBonuses(Selector::all);
		for(const auto & bonus : *observedBonuses)
			if(!bonus || !supportedBonus(*bonus))
				return false;
		try
		{
			const std::vector<std::shared_ptr<const Bonus>> modifiers(observedBonuses->begin(), observedBonuses->end());
			ownedBonusPayloads::validateTypesAndTopology(modifiers);
		}
		catch(const std::invalid_argument &)
		{
			return false;
		}
	}
	const auto * script = LIBRARY->scriptTypes()->getDamageCalculator();
	try
	{
		return script && script->forecastImplementation(state) == DamageForecastImplementation::CANONICAL;
	}
	catch(const std::exception &)
	{
		return false;
	}
}

bool supportedNormalMelee(const battle::Unit * actor, const PotentialTargets & targets,
	const HypotheticBattle & state, bool siegeDefense)
{
	if(siegeDefense || targets.berserk || targets.possibleAttacks.empty()
		|| !supportedOrdinaryState(actor, state))
		return false;
	const auto * current = state.battleGetUnitByID(actor->unitId());
	for(const auto & ap : targets.possibleAttacks)
		if(!ap.queryWorld || ap.queryWorld->callback().get() != &state
			|| ap.attack.attacker != current || ap.attack.shooting || ap.perfectMoment
			|| ap.attack.voluntaryFlightDistance != 0 || ap.coordinatedReturnHandled
			|| ap.completedReturnMarkTarget || ap.completedReturnDestination
			|| !ap.returnLandingHealthLoss.empty() || !ap.overwatchHits.empty()
			|| !ap.tacticsSecondaryHits.empty() || !ap.vampirismHealingByUnit.empty()
			|| !ap.deferredBloodAttempts.empty() || ap.soulHarvestHealthGained != 0
			|| ap.frozenControlValue != 0 || ap.petrifyingBloodControlValue != 0
			|| ap.bulwarkMireGripTriggered)
			return false;
	return true;
}
}

namespace creatureCandidateScreening
{
bool supportsForecastBeam(const battle::Unit * actor, const HypotheticBattle & state,
	bool forced, bool siegeDefense)
{
	if(!battleSearch::forecastBeamOne()) return false;
	bool supported = actor && !forced && !siegeDefense && !state.getDefendedTown()
		&& state.hasCanonicalDamageQuerySources()
		&& newHorizonsCombatSkills::isOrdinaryCreatureAttacker(actor);
	if(supported)
	{
		const auto * script = LIBRARY->scriptTypes()->getDamageCalculator();
		try
		{
			supported = script && script->forecastImplementation(state) == DamageForecastImplementation::CANONICAL;
		}
		catch(const std::exception &) { supported = false; }
	}
	return supported;
}

unsigned admittedFutureActorLimit(const battle::Unit * actor, const HypotheticBattle & state, bool forced)
{
	const auto limit = battleSearch::forecastFutureActors();
	return limit && !supportsForecastBeam(actor, state, forced, false) ? 0 : limit;
}

std::optional<size_t> forecastBeamIndex(std::span<const float> immediate)
{
	std::optional<size_t> best;
	for(size_t index = 0; index < immediate.size(); ++index)
	{
		if(!std::isfinite(immediate[index])) return {};
		if(!best || immediate[index] > immediate[*best]) best = index;
	}
	return best;
}

bool supportsVisibleOrdinaryMelee(const battle::Unit * actor, const HypotheticBattle & state)
{
	return supportedOrdinaryState(actor, state);
}

std::vector<bool> promisingNormalMelee(std::span<const float> immediate,
	std::optional<size_t> waitedIncumbent)
{
	std::vector<bool> retained(immediate.size(), true);
	if(immediate.empty() || (waitedIncumbent && *waitedIncumbent >= immediate.size())
		|| !std::ranges::all_of(immediate, [](float value) { return std::isfinite(value); }))
		return retained;
	const float best = *std::ranges::max_element(immediate);
	for(size_t i = 0; i < immediate.size(); ++i)
		retained[i] = i == 0 || (waitedIncumbent && i == *waitedIncumbent)
			|| immediate[i] == best || vstd::isAlmostEqual(immediate[i], best);
	return retained;
}

std::vector<bool> retainLocalMeleeAlternatives(std::span<const MeleeCandidatePosition> positions,
	const std::vector<bool> & originalAnchors)
{
	if(positions.size() != originalAnchors.size()
		|| std::ranges::any_of(positions, [](const auto & position) { return !position.from.isValid(); })
		|| (!positions.empty() && std::ranges::none_of(originalAnchors, [](bool retained) { return retained; })))
		return std::vector<bool>(positions.size(), true);
	auto retained = originalAnchors;
	for(size_t i = 0; i < positions.size(); ++i)
		for(size_t anchor = 0; anchor < positions.size(); ++anchor)
			if(originalAnchors[anchor] && positions[i].defenderId == positions[anchor].defenderId
				&& BattleHex::getDistance(positions[i].from, positions[anchor].from) <= 1)
			{
				retained[i] = true;
				break;
			}
	return retained;
}

std::vector<bool> normalMeleeForecasts(const battle::Unit * actor,
	const PotentialTargets & targets, const HypotheticBattle & state,
	bool siegeDefense, float positiveMultiplier, float negativeMultiplier,
	std::optional<size_t> waitedIncumbent)
{
	if(!supportedNormalMelee(actor, targets, state, siegeDefense))
		return std::vector<bool>(targets.possibleAttacks.size(), true);
	std::vector<float> immediate;
	immediate.reserve(targets.possibleAttacks.size());
	for(const auto & ap : targets.possibleAttacks)
		immediate.push_back(ap.damageDiff(positiveMultiplier, negativeMultiplier));
	std::vector<MeleeCandidatePosition> positions;
	positions.reserve(targets.possibleAttacks.size());
	for(const auto & ap : targets.possibleAttacks)
		positions.push_back({ap.attack.defender->unitId(), ap.from});
	return retainLocalMeleeAlternatives(positions, promisingNormalMelee(immediate, waitedIncumbent));
}
}

EvaluationResult BattleExchangeEvaluator::findBestTarget(
	const battle::Unit * activeStack,
	PotentialTargets & targets,
	DamageCache & damageCache,
	std::shared_ptr<HypotheticBattle> hb,
	bool siegeDefense,
	BattleBestTargetTiming * timing)
{
	EvaluationResult result(targets.bestAction());
	std::optional<size_t> waitedIncumbent;
	std::optional<size_t> beam;
	if(creatureCandidateScreening::supportsForecastBeam(activeStack, *hb, targets.berserk, siegeDefense))
	{
		std::vector<float> immediate;
		for(const auto & ap : targets.possibleAttacks)
			immediate.push_back(ap.damageDiff(getPositiveEffectMultiplier(), getNegativeEffectMultiplier()));
		beam = creatureCandidateScreening::forecastBeamIndex(immediate);
		if(beam)
			result = EvaluationResult(targets.possibleAttacks[*beam]);
	}


	if(!activeStack->acquireState()->waitedThisTurn && !activeStack->acquireState()->hadMorale)
	{
#if BATTLE_TRACE_LEVEL>=1
		logAi->trace("Evaluating waited attack for %s", activeStack->getDescription());
#endif

		auto hbWaited = std::make_shared<HypotheticBattle>(env.get(), hb);

		hbWaited->makeWait(activeStack);

		updateReachabilityMap(hbWaited);

		for(auto & ap : targets.possibleAttacks)
		{
			if(beam && static_cast<size_t>(&ap - targets.possibleAttacks.data()) != *beam)
				continue;
			if (siegeDefense && !hb->battleIsInsideWalls(ap.from))
				continue;

			float score;
			{
				MovementExchangeScope measurement(timing ? &timing->waitedExchange : nullptr);
				score = evaluateExchange(ap, 0, targets, damageCache, hbWaited,
					timing ? &timing->waitedDetails : nullptr);
			}

			if(score > result.score)
			{
				result.score = score;
				result.bestAttack = ap;
				result.wait = true;
				waitedIncumbent = static_cast<size_t>(&ap - targets.possibleAttacks.data());

#if BATTLE_TRACE_LEVEL >= 1
				logAi->trace("New high score %2f", result.score);
#endif
			}
		}
	}

#if BATTLE_TRACE_LEVEL>=1
	logAi->trace("Evaluating normal attack for %s", activeStack->getDescription());
#endif

	updateReachabilityMap(hb);

	if(result.bestAttack.attack.shooting
		&& !result.bestAttack.defenderDead
		&& !activeStack->acquireState()->waitedThisTurn
		&& hb->battleHasShootingPenalty(activeStack, result.bestAttack.dest))
	{
		if(!canBeHitThisTurn(result.bestAttack))
			return result; // lets wait
	}

	const auto retained = beam ? std::vector<bool>(targets.possibleAttacks.size(), true)
		: creatureCandidateScreening::normalMeleeForecasts(activeStack, targets, *hb,
		siegeDefense, getPositiveEffectMultiplier(), getNegativeEffectMultiplier(), waitedIncumbent);
	for(auto & ap : targets.possibleAttacks)
	{
		if(beam && static_cast<size_t>(&ap - targets.possibleAttacks.data()) != *beam)
			continue;
		if(!retained[static_cast<size_t>(&ap - targets.possibleAttacks.data())])
			continue;
		if (siegeDefense && !hb->battleIsInsideWalls(ap.from))
			continue;

		float score;
		{
			MovementExchangeScope measurement(timing ? &timing->normalExchange : nullptr);
			score = evaluateExchange(ap, 0, targets, damageCache, hb,
				timing ? &timing->normalDetails : nullptr);
		}
		bool sameScoreButWaited = vstd::isAlmostEqual(score, result.score) && result.wait;

		if(score > result.score || sameScoreButWaited)
		{
			result.score = score;
			result.bestAttack = ap;
			result.wait = false;

#if BATTLE_TRACE_LEVEL >= 1
			logAi->trace("New high score %2f", result.score);
#endif
		}
	}

	return result;
}
