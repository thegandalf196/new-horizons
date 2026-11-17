/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeAdapter.h"
#include "ForecastNativePreparation.h"
#include "ForecastNativeReadView.h"
#include "ForecastAcceptedSnapshotSeal.h"
#include "SelectedAttackReceipt.h"
#include "FocusMagicProjection.h"
#include "AttackPossibility.h"
#include "HeroActionAdmission.h"
#include "HypotheticBattle.h"
#include "SearchBudget.h"
#include "VisibleCandidateWorld.h"
#include "../../lib/battle/BattleEffectExchange.h"
#include "../../lib/battle/UnitStateValueExchange.h"
#include <algorithm>
#include <stdexcept>
#include <type_traits>
namespace forecast
{
namespace
{
void require(bool valid)
{
	if(!valid) throw std::logic_error("Selected receipt has stale or unsupported forecast ownership");
}
bool sameWorld(const HypotheticBattle & left, const HypotheticBattle & right)
{
	return left.getBattleID() == right.getBattleID() && left.battleGetRound() == right.battleGetRound()
		&& left.getActivationSerial() == right.getActivationSerial()
		&& left.getCapturedMagicRules() == right.getCapturedMagicRules();
}
void validateSource(const HypotheticBattle & branch, const AttackPossibility & action,
	const battle::CUnitState & target, const battle::CUnitState * captured, UnitId originalActor)
{
	require(action.snapshotSeal && action.snapshotSeal->matches(action) && action.overwatchHits.empty());
	require(action.queryWorld && action.queryWorld->callback() && action.forecastSource
		&& action.attackerState && action.attack.attacker && action.attack.defender);
	const auto & query = *action.queryWorld->callback();
	require(sameWorld(branch, query));
	if(!captured) require(query.forecastState() && action.forecastSource->sameSnapshot(*query.forecastState()));
	require(query.battleGetUnitByID(action.attack.attacker->unitId()) == action.attack.attacker
		&& query.battleGetUnitByID(action.attack.defender->unitId()) == action.attack.defender);
	heroActionProjection::CanonicalSourceAdmission proof(*action.attack.attacker, query);
	require(proof.admits(*action.attack.defender));
	require(branch.battleGetUnitByID(target.unitId()) == &target
		&& target.usesStateEnvironment(*branch.unitEnvironment()) && originalActor == action.attack.attacker->unitId());
	const auto * oldDefinition = action.forecastSource->definition(target.unitId());
	const auto * definition = branch.forecastState()->definition(target.unitId());
	require(oldDefinition && definition && oldDefinition->readDescriptor && definition->readDescriptor
		&& oldDefinition->readDescriptor->identity == definition->readDescriptor->identity
		&& oldDefinition->readDescriptor->creatures == definition->readDescriptor->creatures);
	if(action.effectPreview) require(sameWorld(branch, *action.effectPreview));
	if(captured)
	{
		const bool member = action.attackerState.get() == captured || std::ranges::any_of(action.affectedUnits,
			[&](const auto & value) { return value.get() == captured; });
		require(member && captured->unitId() == target.unitId());
		NativeAdapter::validateAcceptedSnapshot(*captured, action);
	}
	else
	{
		require(target.unitId() == originalActor && action.forecastSource->sameSnapshot(*branch.forecastState()));

	}
}
std::pair<std::vector<Bonus>, std::vector<Bonus>> difference(
	const forecastEffects::State & before, const forecastEffects::State & after)
{
	std::vector<Bonus> removed, added;
	for(const auto & record : before.values()) removed.push_back(*record.value);
	for(const auto & record : after.values())
	{
		const auto old = std::ranges::find_if(removed, [&](const Bonus & value)
		{ return battle::exactBattleEffectEqual(value, *record.value); });
		if(old == removed.end()) added.push_back(*record.value);
		else removed.erase(old);
	}
	return {std::move(removed), std::move(added)};
}
}
std::optional<BattleHex> NativeAdapter::selectedReceiptMovement(HypotheticBattle & branch,
	const AttackPossibility & action, battle::CUnitState & actor)
{
	if(!branch.compactForecast) return std::nullopt;
	return commitSelectedReceipt(branch, action, actor, nullptr, actor.unitId());
}
bool NativeAdapter::selectedReceiptResources(HypotheticBattle & branch, const AttackPossibility & action,
	const battle::CUnitState & captured, const std::shared_ptr<StackWithBonuses> & target,
	const std::shared_ptr<StackWithBonuses> & originalActor)
{
	if(!branch.compactForecast) return false;
	require(target && originalActor && branch.battleGetUnitByID(originalActor->unitId()) == originalActor.get());
	commitSelectedReceipt(branch, action, *target, &captured, originalActor->unitId());
	return true;
}
BattleHex NativeAdapter::commitSelectedReceipt(HypotheticBattle & branch, const AttackPossibility & action,
	battle::CUnitState & target, const battle::CUnitState * captured, UnitId originalActor)
{
	validateSource(branch, action, target, captured, originalActor); // BEFORE lazy native map preparation.
	const auto id = target.unitId();
	const auto ids = branch.compactForecast->orderedIds();
	const auto originalPosition = target.getPosition();
	if(branch.searchBudget()) branch.searchBudget()->require(battleSearch::WorkKind::Node,
		static_cast<uint32_t>(ids.size() * (ids.size() + 1)));
	NativePreparation preparation(branch); // Restores map/observer on ANY later failure.
	auto borrowed = std::shared_ptr<CBattleInfoCallback>(&branch, [](CBattleInfoCallback *) {});
	HypotheticBattle prospective(branch.env, std::move(borrowed),
		std::shared_ptr<const battle::OwnedBattleFrameBatch>{}, branch.searchBudget());
	prospective.bloodrageDestroyedUnits = branch.bloodrageDestroyedUnits;
	prospective.bloodrageDamagePercents = branch.bloodrageDamagePercents;
	prospective.bloodrageFirstBloodUsed = branch.bloodrageFirstBloodUsed;
	std::vector<std::shared_ptr<StackWithBonuses>> targets, staged;
	targets.reserve(ids.size()); staged.reserve(ids.size());
	for(const auto peer : ids)
	{
		auto current = preparation.unit(peer);
		require(current && current->usesStateEnvironment(*branch.unitEnvironment()));
		auto future = prepareEffectDraft(*current, &prospective);
		require(current->preparedEffectSourceMatches(*future));
		targets.push_back(current); staged.emplace_back(std::move(future));
		prospective.stackStates[peer] = staged.back();
	}
	auto futureTarget = prospective.getForUpdate(id);
	if(captured)
	{
		focusMagicAI::restoreAcceptedRecords(*futureTarget, *captured); // No second charge/ammo payment.
		selectedAttackReceipt::replayResourcesNative(action, *captured, futureTarget,
			prospective.getForUpdate(originalActor), prospective);
	}
	else selectedAttackReceipt::replayMovementNative(action, *futureTarget, prospective);
	require(prospective.bloodrageDestroyedUnits == branch.bloodrageDestroyedUnits
		&& std::ranges::equal(prospective.bloodrageDamagePercents, branch.bloodrageDamagePercents)
		&& prospective.bloodrageFirstBloodUsed == branch.bloodrageFirstBloodUsed);
	auto draft = *branch.compactForecast;
	const auto affected = captured ? std::span<const UnitId>(&id, 1) : ids;
	for(const auto peer : affected)
	{
		auto & after = *prospective.stackStates.at(peer);
		const auto effects = captureEffects(after, prospective.battleGetRound());
		const auto [removed, added] = difference(*draft.unit(peer)->effects, *effects);
		const auto receipt = prepareEffectApplication(draft, branch, peer, removed, added);
		require(stageEffects(draft, branch, peer, receipt, after, captureFocus(after), true));
		auto next = *draft.unit(peer);
		const bool moved = next.position != after.getPosition();
		next.position = after.getPosition();
		next.controller = prospective.battleGetActionController(&after);
		draft.commit(peer, std::move(next), Queue | (moved ? Occupancy | Adjacency | Damage : Clean), moved);
	}
	if(captured) refreshReadViews(draft, prospective, affected, false);
	else refreshOrderDerived(prospective, draft); // Complete88+all-peer current context, BEFORE publication.
	for(size_t index = 0; index < ids.size(); ++index)
		require(targets[index]->preparedEffectSourceMatches(*staged[index]));
	static_assert(std::is_nothrow_move_assignable_v<decltype(branch.compactForecast)>);
	preparation.accept(affected);
	// No queries, allocation, callbacks or budget checks below this point.
	for(size_t index = 0; index < ids.size(); ++index)
		if(!captured || ids[index] == id)
		{
			targets[index]->publishPreparedEffects(*staged[index]);
			battle::exchangePreparedUnitStateValues(*targets[index], *staged[index]);
		}
	branch.compactForecast = std::move(draft);
	return originalPosition;
}
}
