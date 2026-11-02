/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "HeroDecisionPressure.h"
#include "AttackPossibility.h"
#include "BattleExchangeVariant.h"
#include "BattleDamageValue.h"
#include "CanonicalOrderValue.h"
#include "SecondWindOpportunity.h"
#include "HeroActionAdmission.h"
#include "PossibleSpellcast.h"
#include "HypotheticBattle.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/HeroCommandRevision.h"
#include "../../lib/battle/NewHorizonsDivineMandate.h"
#include "../../lib/battle/NewHorizonsEnchantedCommand.h"
#include "../../lib/battle/NewHorizonsHeroicSpirit.h"
#include "../../lib/combatScripts/IDamageCalculatorScript.h"
#include "../../lib/scripting/ScriptService.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace heroDecisionPressure
{
std::vector<size_t> ambiguousRefinements(const std::vector<Ranked> & ranked)
{
	auto ordered = ranked;
	std::erase_if(ordered, [](const Ranked & item)
	{
		return !std::isfinite(item.marginal) || item.marginal <= 0;
	});
	std::stable_sort(ordered.begin(), ordered.end(), [](const Ranked & a, const Ranked & b)
	{
		return a.marginal > b.marginal;
	});
	// Provisional AI uncertainty band; no game-mechanic coefficient is changed.
	if(ordered.size() < 2 || ordered[0].marginal - ordered[1].marginal > 0.2f * ordered[0].marginal)
		return {};
	std::vector<size_t> result{ordered[0].ordinal, ordered[1].ordinal};
	std::sort(result.begin(), result.end());
	return result;
}

std::vector<size_t> promisingRefinements(const std::vector<Ranked> & ranked)
{
	auto nearTies = ambiguousRefinements(ranked);
	if(!nearTies.empty())
		return nearTies;
	const Ranked * best = nullptr;
	for(const auto & item : ranked)
		if(std::isfinite(item.marginal) && item.marginal > 0
			&& (!best || item.marginal > best->marginal))
			best = &item;
	return best ? std::vector<size_t>{best->ordinal} : std::vector<size_t>{};
}

void Experiment::reset()
{
	const bool enabled = enableShortlist;
	*this = {};
	enableShortlist = enabled;
}

void Experiment::noteContinuation(size_t ordinal)
{
	++continuationCalls;
	if(std::find(refinedOrdinals.begin(), refinedOrdinals.end(), ordinal) != refinedOrdinals.end())
		++refinementContinuationCalls;
}

void prepareShortlist(Experiment & result, const Environment * environment,
	const std::shared_ptr<CBattleInfoCallback> & source, BattleSide side,
	const std::vector<PossibleSpellcast> & candidates, const DamageCache & originalDamage,
	const BattleExchangeEvaluator & utility)
{
	result.declined.assign(candidates.size(), false);
	const auto supportedCommand = [](HeroCommand command)
	{
		return command == HeroCommand::FOCUS_FIRE || command == HeroCommand::RIPOSTE;
	};
	if(std::count_if(candidates.begin(), candidates.end(), [&](const auto & candidate)
		{ return supportedCommand(candidate.command); }) <= 2)
		return; // No saved full forecasts: avoid paying for a redundant scout.
	++result.baselineAttempts;
	auto baseline = Baseline::create(environment, source, side, std::nullopt, true, &originalDamage, &utility);
	if(!baseline)
		return;
	result.baselineReady = true;
	const auto * hero = source->battleGetFightingHero(side);
	for(size_t index = 0; index < candidates.size(); ++index)
	{
		const auto & candidate = candidates[index];
		if(!supportedCommand(candidate.command))
			continue;
		auto state = std::make_shared<HypotheticBattle>(environment, source);
		const auto admission = heroActionProjection::admitCandidate(*state, side, candidate);
		if(!admission.admitted || !admission.orderEffects || !admission.orderAllowance
			|| !state->projectAcceptedHeroOrder(side, *admission.orderEffects, *admission.orderAllowance))
			continue; // Original evaluator, never reinterpret a refusal as a zero score.
		const auto & receipt = admission.orderAllowance->action.receipt;
		const auto & order = admission.orderEffects->order;
		const auto & future = state->getDoubleCommandState(side);
		if(receipt.allowance != HeroActionAllowanceState::AllowanceKind::HERO
			|| receipt.source == HeroActionAllowanceState::GrantSource::BATTLE_PLAN
			|| future.orderPending() || future.secondWindReady()
			|| newHorizonsHeroicSpirit::canRollOrderMorale(hero, receipt)
			|| !order.crownAndAltarRecipientUnitIds.empty()
			|| newHorizonsEnchantedCommand::eligible(state->getMagicRules(), hero, order))
			continue; // Unvalued typed/faction/activation companions stay on the full path.
		const auto marginal = baseline->orderUtility(state, candidate.command);
		if(marginal && std::isfinite(*marginal))
			result.cheapScores.push_back({index, *marginal});
	}
	result.refinedOrdinals = promisingRefinements(result.cheapScores);
	for(const auto & estimate : result.cheapScores)
		result.declined[estimate.ordinal] = std::find(result.refinedOrdinals.begin(),
			result.refinedOrdinals.end(), estimate.ordinal) == result.refinedOrdinals.end();
}

Baseline::Baseline(const Environment * environment,
	std::shared_ptr<CBattleInfoCallback> source, BattleSide side, const DamageCache * originalDamage,
	const BattleExchangeEvaluator * utility)
	: source(std::move(source)), reference(std::make_shared<HypotheticBattle>(environment, this->source)),
	  damage(std::make_unique<DamageCache>(originalDamage)), side(side),
	  positiveMultiplier(utility ? utility->getPositiveEffectMultiplier() : 1.0f),
	  negativeMultiplier(utility ? utility->getNegativeEffectMultiplier() : 1.0f)
{
	nativeWorld = dynamic_cast<const BattleInfo *>(this->source->getBattle());
}
Baseline::~Baseline() = default;

std::unique_ptr<Baseline> Baseline::create(const Environment * environment,
	const std::shared_ptr<CBattleInfoCallback> & source, BattleSide side,
	const std::optional<std::vector<Approach>> & completeExistingApproaches, bool directOnly,
	const DamageCache * originalDamage, const BattleExchangeEvaluator * utility)
{
	if(!environment || !source || typeid(*source) != typeid(CPlayerBattleCallback)
		|| !source->getBattle() || typeid(*source->getBattle()) != typeid(BattleInfo)
		|| !heroCommands::isRevisedRules(source->getBattle()->getHeroCommandRules())
		|| source->battleGetMySide() != side
		|| !source->CBattleInfoCallback::hasCanonicalDamageQuerySources())
		return {};
	const auto * script = LIBRARY->scriptTypes()->getDamageCalculator();
	try
	{
		if(!script || script->forecastImplementation(*source) != DamageForecastImplementation::CANONICAL)
			return {};
	}
	catch(const std::exception &)
	{
		return {}; // Optional identity recognition is not a policy permission.
	}
	auto result = std::unique_ptr<Baseline>(new Baseline(environment, source, side, originalDamage, utility));
	result->hasApproachFacts = completeExistingApproaches.has_value();
	if(!result->prepare(completeExistingApproaches.value_or(std::vector<Approach>{}), directOnly, !originalDamage))
		return {};
	return result;
}

bool Baseline::prepare(const std::vector<Approach> & existingApproaches, bool directOnly, bool buildOriginal)
{
	const auto units = reference->battleGetAllUnits(false);
	if(units.size() < 2)
		return false;
	// Reject foreign getters/modifiers through the existing whole-source raw
	// admission BEFORE any cloning/stat/retaliation preparation queries.
	heroActionProjection::CanonicalSourceAdmission sourceAdmission(*units.front(), *reference);
	if(!sourceAdmission.admits(*units.back()))
		return false;
	const auto * hero = reference->battleGetFightingHero(side);
	if(directOnly && (newHorizonsHeroLuck::hasLuckyDay(hero)
		|| newHorizonsDivineMandate::hasSharedPurposePerk(hero)))
		return false; // Unvalued random/morale companions are not a zero estimate.
	for(const auto * unit : units)
		if(unit->hasBonusOfType(BonusType::COMBAT_EVENT_TRIGGER)
			|| unit->hasBonusOfType(BonusType::ON_COMBAT_EVENT)
			|| unit->hasBonusOfType(BonusType::SPELL_BEFORE_ATTACK)
			|| unit->hasBonusOfType(BonusType::SPELL_AFTER_ATTACK)
			|| (directOnly && (unit->valOfBonuses(BonusType::ADDITIONAL_ATTACK) != 0
				|| unit->hasBonusOfType(BonusType::TWO_HEX_ATTACK_BREATH)
				|| unit->hasBonusOfType(BonusType::ATTACKS_ALL_ADJACENT)
				|| unit->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK)
				|| unit->hasBonusOfType(BonusType::RANGED_RETALIATION))))
			return false; // Event/area/multistrike attacks need their existing full forecast.
	for(const auto * unit : units)
		reference->getForUpdate(unit->unitId()); // Isolate retaliation totalCache latches.
	for(const auto * original : units)
	{
		const auto * actor = reference->battleGetUnitByID(original->unitId());
		if(!actor->alive() || actor->isGhost() || actor->isTurret())
			continue;
		if(reference->battleGetActionController(actor) != reference->battleGetOwner(actor))
			return false; // Control-transfer utility is not modeled by this first prototype.
		heroActionProjection::CanonicalSourceAdmission admission(*actor, *reference);
		for(const auto * enemy : units)
		{
			const auto * victim = reference->battleGetUnitByID(enemy->unitId());
			if(!victim->alive() || victim->isGhost() || victim->isTurret()
				|| reference->battleGetOwner(actor) == reference->battleGetOwner(victim))
				continue;
			if(!admission.admits(*victim))
				return false; // Exact known getter/source domain; custom/unknown stays full.
			const bool shooting = reference->battleCanShoot(actor, victim->getPosition());
			if(!shooting && !actor->isMeleeAttacker())
				continue;
			bool adjacent = false;
			for(const auto & from : actor->getHexes())
				for(const auto & to : victim->getHexes())
					adjacent |= from.isValid() && to.isValid() && BattleHex::getDistance(from, to) == 1;
			if(actor->willMove(0) && (!shooting && !adjacent))
				directTopology = false;
			int distance = 0;
			for(const auto & fact : existingApproaches)
				if(fact.attacker == actor->unitId() && fact.victim == victim->unitId()
					&& fact.shooting == shooting && fact.voluntaryDistance >= 3)
				{
					distance = fact.voluntaryDistance;
					break;
				}
			pairs.push_back({actor->unitId(), victim->unitId(), shooting, distance, 0});
		}
	}
	if(directOnly && !directTopology)
		return false; // Unknown topology pays no damage-cache/scout payload work.
	if(buildOriginal)
		damage->buildDamageCache(reference, side);
	for(auto & pair : pairs)
		pair.referenceValue = pairValue(*reference, pair);
	referencePressure = pressure(*reference);
	return true;
}

float Baseline::damageValue(uint32_t attacker, uint32_t victim, int64_t amount)
{
	const auto * a = reference->battleGetUnitByID(attacker);
	const auto * d = reference->battleGetUnitByID(victim);
	return AttackPossibility::calculateDamageReduce(a, d,
		static_cast<uint64_t>(std::max<int64_t>(0, amount)), *damage, reference);
}

float Baseline::pairValue(const CBattleInfoCallback & view, const Pair & pair, bool grantedActivation)
{
	const auto * a = view.battleGetUnitByID(pair.attacker);
	const auto * d = view.battleGetUnitByID(pair.victim);
	if(!a || !d || !a->alive() || !d->alive() || a->isTimeStopped() || d->isInvincible()
		|| !a->canMove() || (!grantedActivation && !a->willMove(0)))
		return 0;
	// Fixed opportunities only; no new paths, movement/attack sequence or target
	// enumeration. Native estimate supplies retaliation immunity and reductions.
	BattleAttackInfo attack(a, d, pair.voluntaryDistance, pair.shooting);
	DamageEstimation counter;
	const auto estimate = view.battleEstimateDamage(attack, &counter);
	const bool ours = reference->battleGetOwner(reference->battleGetUnitByID(pair.attacker))
		== reference->sideToPlayer(side);
	return damageValue(pair.attacker, pair.victim, battleDamageValue::averageOrderDamage(estimate))
			* (ours ? positiveMultiplier : negativeMultiplier)
		- damageValue(pair.victim, pair.attacker, battleDamageValue::averageOrderDamage(counter))
			* (ours ? negativeMultiplier : positiveMultiplier);
}

float Baseline::pressure(const CBattleInfoCallback & view)
{
	std::map<uint32_t, float> best;
	for(const auto & pair : pairs)
	{
		const float value = &view == reference.get() ? pair.referenceValue : pairValue(view, pair);
		auto & maximum = best[pair.attacker];
		maximum = std::max(maximum, value);
	}
	float result = 0;
	const auto own = reference->sideToPlayer(side);
	for(const auto & [id, value] : best)
		result += reference->battleGetOwner(reference->battleGetUnitByID(id)) == own ? value : -value;
	return result;
}

std::optional<float> Baseline::extraActivationUtility(const HypotheticBattle & accepted)
{
	const auto order = accepted.getHeroOrderState(side);
	if(!order || order->command != HeroCommand::SECOND_WIND)
		return {};
	const auto activation = heroOrderValue::projectSecondWindActivation(*source,
		reference->env, source, side, order->primaryTargetUnitId);
	if(!activation)
		return {}; // Keep obstacle, Double Command and incomplete activation fallback.
	auto isolated = std::make_shared<HypotheticBattle>(reference->env,
		std::const_pointer_cast<CBattleInfoCallback>(activation));
	const auto units = isolated->battleGetAllUnits(false);
	for(const auto * unit : units)
		isolated->getForUpdate(unit->unitId());
	float best = 0;
	for(auto pair : pairs)
	{
		if(pair.attacker != order->primaryTargetUnitId
			|| !heroOrderValue::hasSecondWindAttackOpportunity(*isolated, pair.attacker, pair.victim))
			continue;
		const auto * actor = isolated->battleGetUnitByID(pair.attacker);
		const auto * victim = isolated->battleGetUnitByID(pair.victim);
		bool shooting = false;
		for(const auto & hex : victim->getHexes())
			shooting |= isolated->battleCanShootAction(actor, hex);
		bool adjacent = false;
		for(const auto & from : actor->getHexes())
			for(const auto & to : victim->getHexes())
				adjacent |= from.isValid() && to.isValid() && BattleHex::getDistance(from, to) == 1;
		if(!shooting && !adjacent)
			return {}; // Movement/Skirmisher geometry is not a direct-contact fact.
		pair.shooting = shooting; // Activation expiry can change the legal mode.
		pair.voluntaryDistance = 0; // Never inherit an earlier activation's approach.
		// The existing helper above admitted THIS recipient's actual grant. It
		// intentionally retains movedThisRound; ordinary pair readiness stays strict.
		best = std::max(best, pairValue(*isolated, pair, true));
	}
	return best; // One granted activation; never multiply the army's other turns.
}

std::optional<float> Baseline::orderUtility(const std::shared_ptr<HypotheticBattle> & accepted,
	HeroCommand command)
{
	if(!accepted || !accepted->hasCanonicalDamageQuerySources(512, nativeWorld)
		|| !heroCommands::isRevisedRules(accepted->getHeroCommandRules()))
		return {};
	if(heroOrderValue::usesRevisedArmyHeuristic(*source, command))
		return heroOrderValue::revisedArmyUtility(source, accepted, side, command);
	switch(command)
	{
		case HeroCommand::CHARGE:
			if(!hasApproachFacts)
				return {}; // Do not invent a voluntary three-hex route.
			break;
		case HeroCommand::BRACE:
			return {}; // Pre-emptive strike/casualty coupling awaits a bounded scorer.
		case HeroCommand::FOCUS_FIRE:
		case HeroCommand::HOLD_THE_LINE:
		case HeroCommand::RIPOSTE:
			break;
		case HeroCommand::SECOND_WIND:
			return extraActivationUtility(*accepted);
		default:
			return {}; // Historical Protect/Flank/foreign command retain full path.
	}
	const auto units = accepted->battleGetAllUnits(false);
	for(const auto * unit : units)
		accepted->getForUpdate(unit->unitId());
	if(!directTopology)
		return {}; // No invented route or retained Hold anchor for moving opportunities.
	for(const auto & pair : pairs)
	{
		const auto * actor = accepted->battleGetUnitByID(pair.attacker);
		const auto * victim = accepted->battleGetUnitByID(pair.victim);
		if(accepted->battleCanShoot(actor, victim->getPosition()) != pair.shooting)
			return {}; // Activation/status changes invalidate the fixed attack mode.
	}
	return pressure(*accepted) - referencePressure;
}
}
