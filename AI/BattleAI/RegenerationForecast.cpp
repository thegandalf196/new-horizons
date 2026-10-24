/* RegenerationForecast.cpp, VCMI / New Horizons, GPL v2.0 or later. */
#include "StdInc.h"
#include "RegenerationForecast.h"
#include "ProjectedBestAction.h"
#include "BattleEvaluatorProjection.h"
#include "PotentialTargets.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/NewHorizonsDivineMandate.h"
int regenerationEffectRounds(const battle::Unit * unit, SpellID spell)
{
	if(!unit)
		return 0;

	int result = 0;
	const auto effects = unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
	for(const auto & effect : *effects)
		if(effect)
			result = std::max<int>(result, effect->turnsRemain);
	return std::max(0, result);
}

std::set<uint32_t> improvedRegenerationTargets(
	const HypotheticBattle & projectedBattle,
	const CBattleInfoCallback & originalBattle,
	SpellID spell)
{
	std::set<uint32_t> result;
	for(const auto * projected : projectedBattle.battleGetAllUnits())
	{
		const auto * projectedState = dynamic_cast<const battle::CUnitState *>(projected);
		if(!projectedState || !projectedState->alive() || projectedState->regenerationRateMillionths <= 0)
			continue;

		const auto * original = originalBattle.battleGetUnitByID(projected->unitId());
		const auto * originalState = dynamic_cast<const battle::CUnitState *>(original);
		const int originalRate = originalState ? originalState->regenerationRateMillionths : 0;
		const int originalRounds = regenerationEffectRounds(original, spell);
		if(projectedState->regenerationRateMillionths > originalRate
			|| regenerationEffectRounds(projected, spell) > originalRounds)
			result.insert(projected->unitId());
	}
	return result;
}

float projectedRegenerationValue(
	const Environment * environment,
	const std::shared_ptr<CBattleInfoCallback> & beforeCast,
	const std::shared_ptr<HypotheticBattle> & afterCast,
	const std::vector<battle::Units> & turnOrder,
	SpellID spell,
	DamageCache & damageCache,
	BattleSide ourSide,
	PlayerColor ourPlayer,
	float positiveEffectMultiplier, RegenerationBaselineReuse * reuse)
{
	const auto targets = improvedRegenerationTargets(*afterCast, *beforeCast, spell);
	if(targets.empty())
		return 0.0f;

	auto session = reuse ? reuse->prepare(environment, *beforeCast, turnOrder, damageCache, ourSide, ourPlayer)
		: RegenerationBaselineReuse::Session{};
	auto forecast = std::make_shared<HypotheticBattle>(environment, afterCast);
	auto baseline = session.observations ? nullptr : std::make_shared<HypotheticBattle>(environment, beforeCast);
	DamageCache forecastDamage(&damageCache);
	DamageCache baselineDamage(&damageCache);
	forecastDamage.buildDamageCache(forecast, ourSide);
	if(baseline)
		baselineDamage.buildDamageCache(baseline, ourSide);
	std::vector<RegenerationBaselineReuse::Observation> observations;
	size_t occurrence = 0;
	float score = 0.0f;
	bool firstRound = true;

	for(const auto & round : turnOrder)
	{
		if(!firstRound)
		{
			forecast->nextRound();
			if(baseline)
				baseline->nextRound();
		}
		firstRound = false;

		for(const auto * queuedUnit : round)
		{
			const auto * unit = forecast->battleGetUnitByID(queuedUnit->unitId());
			const auto * baselineUnit = baseline ? baseline->battleGetUnitByID(queuedUnit->unitId()) : nullptr;
			const auto * unitState = dynamic_cast<const battle::CUnitState *>(unit);
			const auto * baselineState = dynamic_cast<const battle::CUnitState *>(baselineUnit);
			int64_t baselineHeal = 0;
			if(session.observations)
				baselineHeal = session.observations->at(occurrence).heal;
			else if(session.recording)
			{
				baselineHeal = baselineUnit && baselineUnit->alive() && baselineState
					? baselineState->regenerationProjectedHeal() : 0;
				observations.push_back({queuedUnit->unitId(), baselineHeal});
			}
			++occurrence;
			const bool startsActivation = unit
				&& forecast->battleBeginsActivation(unit, BattleUnitTurnReason::TURN_QUEUE);
			if(startsActivation && unit->alive() && targets.contains(unit->unitId())
				&& forecast->battleGetOwner(unit) == ourPlayer)
			{
				const auto projectedHeal = unitState ? unitState->regenerationProjectedHeal() : 0;
				if(!session.observations && !session.recording)
					baselineHeal = baselineUnit && baselineUnit->alive() && baselineState
						? baselineState->regenerationProjectedHeal() : 0;
				const auto additionalHeal = std::max<int64_t>(0, projectedHeal - baselineHeal);
				if(additionalHeal > 0)
				{
					const auto healedValue = AttackPossibility::calculateDamageReduce(
						nullptr, unit, static_cast<uint64_t>(additionalHeal), forecastDamage, forecast);
					score += healedValue * positiveEffectMultiplier;
				}
			}

			const bool forecastForfeits = unit && newHorizonsFrozen::forfeitsNormalActivation(*unit, BattleUnitTurnReason::TURN_QUEUE);
			const bool baselineForfeits = baselineUnit && newHorizonsFrozen::forfeitsNormalActivation(*baselineUnit, BattleUnitTurnReason::TURN_QUEUE);
			if(unit && unit->alive())
				forecast->nextTurn(unit->unitId(), BattleUnitTurnReason::TURN_QUEUE);
			if(baselineUnit && baselineUnit->alive())
				baseline->nextTurn(baselineUnit->unitId(), BattleUnitTurnReason::TURN_QUEUE);

			const auto * currentForecastUnit = unit
				? forecast->battleGetUnitByID(unit->unitId()) : nullptr;
			const auto * currentBaselineUnit = baselineUnit
				? baseline->battleGetUnitByID(baselineUnit->unitId()) : nullptr;
			if(!forecastForfeits && currentForecastUnit && currentForecastUnit->alive())
			{
				PotentialTargets potentialTargets(currentForecastUnit, forecastDamage, forecast);
				if(!potentialTargets.possibleAttacks.empty())
					applyProjectedBestAction(*forecast, currentForecastUnit, potentialTargets.bestAction());
				if(potentialTargets.berserk)
					expireCompletedProjectedBerserkActivation(*forecast,
						forecast->battleGetUnitByID(queuedUnit->unitId()));
			}
			if(currentForecastUnit)
				{
				forecast->getForUpdate(queuedUnit->unitId())->removeUnitBonus(Bonus::UntilActivationEnds);
				newHorizonsDivineMandate::completeDisciplineActivation(*forecast, queuedUnit->unitId());
			}
			if(!baselineForfeits && currentBaselineUnit && currentBaselineUnit->alive())
			{
				PotentialTargets potentialTargets(currentBaselineUnit, baselineDamage, baseline);
				if(!potentialTargets.possibleAttacks.empty())
					applyProjectedBestAction(*baseline, currentBaselineUnit, potentialTargets.bestAction());
				if(potentialTargets.berserk)
					expireCompletedProjectedBerserkActivation(*baseline,
						baseline->battleGetUnitByID(queuedUnit->unitId()));
			}
			if(currentBaselineUnit)
				{
				baseline->getForUpdate(queuedUnit->unitId())->removeUnitBonus(Bonus::UntilActivationEnds);
				newHorizonsDivineMandate::completeDisciplineActivation(*baseline, queuedUnit->unitId());
			}
		}
	}

	if(reuse)
		reuse->complete(std::move(session), std::move(observations));
	return score;
}
