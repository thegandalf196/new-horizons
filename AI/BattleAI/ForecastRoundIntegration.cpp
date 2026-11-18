/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeAdapter.h"
#include "HypotheticBattle.h"
#include "SearchBudget.h"
#include "../../lib/battle/UnitStateValueExchange.h"
#include "../../lib/battle/CObstacleInstance.h"
#include <limits>
#include <stdexcept>
void HypotheticBattle::advanceRoundSideFacts(BattleSide side, int32_t nextRound)
{
	fortuneStates.at(side).nextRound();
	luckSerendipityStates.at(side).nextRound(nextRound);
	moraleSuppressionStates.at(side).nextRound();
	rapidResponseStates.at(side).clearPending();
}
void HypotheticBattle::advanceRoundObstacles()
{
	// Unlike opening unit enchantments, obstacle timers tick on every round
	// transition. Authoritative BattleFlowProcessor then removes zero timers.
	for(auto & obstacle : projectedObstacles)
	{
		if(const auto spell = std::dynamic_pointer_cast<const SpellCreatedObstacle>(obstacle))
		{
			auto aged = std::make_shared<SpellCreatedObstacle>(*spell);
			aged->battleTurnPassed();
			obstacle = aged;
		}
	}
	const auto previousSize = projectedObstacles.size();
	vstd::erase_if(projectedObstacles, [](const auto & obstacle)
	{
		const auto spell = std::dynamic_pointer_cast<const SpellCreatedObstacle>(obstacle);
		return spell && spell->turnsRemaining == 0;
	});
	obstacleChanges |= previousSize != projectedObstacles.size();
}
namespace forecast
{
std::optional<PreparedEffectRound> NativeAdapter::prepareRound(HypotheticBattle & branch,
	const BattleSideArray<std::optional<SideReceipt>> & sides)
{
	if(!branch.compactForecast) return {};
	if(branch.projectedRound == std::numeric_limits<int32_t>::max())
		throw std::overflow_error("Forecast round exhausted");
	PreparedEffectRound prepared(*branch.compactForecast);
	prepared.expectedLayer = branch.compactForecast->layer;
	const auto count = prepared.state.ids().size();
	if(count > (std::numeric_limits<uint32_t>::max() - 1) / 3)
		throw battleSearch::BudgetExhausted(battleSearch::WorkKind::Node);
	if(branch.searchBudget())
	{
		battleSearch::WorkAmounts quota{};
		// Native child capture 1+N, lazy writable preparation N, effect-clock preparation N.
		quota[static_cast<size_t>(battleSearch::WorkKind::Node)] = static_cast<uint32_t>(1 + 3 * count);
		auto reserved = branch.searchBudget()->reserve(quota);
		if(!reserved) throw battleSearch::BudgetExhausted(battleSearch::WorkKind::Node);
		prepared.work = std::make_shared<battleSearch::SearchBudget>(std::move(*reserved));
	}
	prepared.preparation = std::make_unique<NativePreparation>(branch);
	auto borrowed = std::shared_ptr<CBattleInfoCallback>(&branch, [](CBattleInfoCallback *) {});
	prepared.future = std::make_shared<HypotheticBattle>(branch.env, std::move(borrowed),
		std::shared_ptr<const battle::OwnedBattleFrameBatch>{}, prepared.work);
	auto & future = *prepared.future;
	const bool opening = branch.projectedRound == 0;
	const auto nextRound = branch.projectedRound + 1;
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		if(!sides.at(side) || branch.forecastSide(side) != sides.at(side)->expected())
			throw std::logic_error("Stale prepared round side owner");
		future.sideStates.at(side) = sides.at(side)->after();
		future.advanceRoundSideFacts(side, nextRound);
		const auto & old = future.localHeroOrderStates(side);
		auto remaining = old;
		std::erase_if(remaining, [nextRound](const HeroOrderState & order) { return !order.hasScheduledRecipients(nextRound); });
		if(remaining != old) future.heroOrderStates.at(side) = std::make_shared<const std::vector<HeroOrderState>>(std::move(remaining));
		if(!future.getHeroOrderState(side, HeroCommand::FOCUS_FIRE)) future.focusFireStates[side].reset();
	}
	future.projectedRound = nextRound;
	future.forfeitedActivations.clear();
	future.seizeInitiative.nextRound(nextRound);
	++future.bonusTreeVersion;
	prepared.peers.reserve(count);
	// Register ALL corresponding future units before any peer-aware ammo,
	// membership, updater, limiter or lifecycle query runs on a scratch unit.
	for(const auto id : prepared.state.ids())
	{
		auto target = prepared.preparation->unit(id);
		auto staged = std::shared_ptr<StackWithBonuses>(prepareEffectDraft(*target, &future));
		future.stackStates[id] = staged;
		prepared.peers.emplace_back(std::move(target), std::move(staged));
	}
	for(const auto & [target, staged] : prepared.peers)
	{
		const auto receipt = prepareRoundEffects(prepared.state, target->unitId(), opening, nextRound, prepared.work.get());
		if(!stageRoundEffects(prepared.state, branch, target->unitId(), receipt, *staged))
			throw std::logic_error("Admitted compact round effects rejected");
	}
	future.obstacleChanges = branch.obstacleChanges;
	future.advanceRoundObstacles();
	prepared.state.round = nextRound;
	refreshReadViews(prepared.state, future, prepared.state.orderedIds(), true);
	return prepared;
}
void NativeAdapter::commitRound(HypotheticBattle & branch, std::optional<PreparedEffectRound> prepared)
{
	if(!prepared) return;
	if(!branch.compactForecast || branch.compactForecast->layer != prepared->expectedLayer
		|| prepared->state.currentRound() != branch.projectedRound + 1)
		throw std::logic_error("Stale prepared round state");
	for(const auto & [target, staged] : prepared->peers)
		if(branch.battleGetUnitByID(target->unitId()) != target.get() || !target->preparedEffectSourceMatches(*staged))
			throw std::logic_error("Stale prepared round unit source");
	prepared->preparation->accept(prepared->state.ids());
	// Everything below is non-allocating and preserves registered addresses,
	// descriptors, provider/cache closures and receiving ammo/health owners.
	for(const auto & [target, staged] : prepared->peers)
	{
		target->publishPreparedEffects(*staged);
		battle::exchangePreparedUnitStateValues(*target, *staged);
	}
#define NH_SWAP_ROUND(field) static_assert(std::is_nothrow_swappable_v<decltype(branch.field)>); std::swap(branch.field, prepared->future->field)
	NH_SWAP_ROUND(sideStates);
	NH_SWAP_ROUND(heroOrderStates);
	NH_SWAP_ROUND(focusFireStates);
	exchangeSylvanLuckSides(branch.fortuneStates, prepared->future->fortuneStates);
	NH_SWAP_ROUND(luckSerendipityStates);
	NH_SWAP_ROUND(moraleSuppressionStates);
	NH_SWAP_ROUND(rapidResponseStates);
	NH_SWAP_ROUND(forfeitedActivations);
	NH_SWAP_ROUND(seizeInitiative);
	NH_SWAP_ROUND(projectedRound);
	NH_SWAP_ROUND(projectedObstacles);
	NH_SWAP_ROUND(obstacleChanges);
#undef NH_SWAP_ROUND
	++branch.bonusTreeVersion;
	branch.compactForecast = std::move(prepared->state);
}
}
