/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeAdapter.h"
#include "ForecastEffectTransitions.h"
#include "AttackPossibility.h"
#include "FocusMagicProjection.h"
#include "ForecastAcceptedSnapshotSeal.h"
#include "AttackPreviewHitContext.h"
#include "GnollHarriersProjection.h"
#include "HeroActionAdmission.h"
#include "HypotheticBattle.h"
#include "PhysicalStrikeProjection.h"
#include "SearchBudget.h"
#include "VisibleCandidateWorld.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/BattleEffectExchange.h"
#include "../../lib/battle/FocusMagicValueState.h"
#include "../../lib/battle/UnitStateValueExchange.h"
#include "../../lib/bonuses/BonusList.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace forecast
{
namespace
{
void require(bool valid)
{
	if(!valid) throw std::logic_error("Accepted attack has stale or unsupported forecast ownership");
}
void breakSanctuary(StackWithBonuses & actor)
{
	const auto markers = actor.getBonusesOfType(BonusType::SANCTIFIED);
	if(!markers || markers->empty()) return;
	std::vector<BonusSourceID> sources;
	for(const auto & marker : *markers)
		if(marker && marker->source == BonusSource::SPELL_EFFECT
			&& std::ranges::find(sources, marker->sid) == sources.end()) sources.push_back(marker->sid);
	for(const auto & source : sources)
		actor.removeUnitBonus(Selector::source(BonusSource::SPELL_EFFECT, source));
	actor.removeUnitBonus(Selector::type()(BonusType::SANCTIFIED));
}
bool sameWorld(const HypotheticBattle & left, const HypotheticBattle & right)
{
	return left.getBattleID() == right.getBattleID()
		&& left.battleGetRound() == right.battleGetRound()
		&& left.getActivationSerial() == right.getActivationSerial()
		&& left.getCapturedMagicRules() == right.getCapturedMagicRules();
}
struct EffectDiff
{
	std::vector<Bonus> removed, added;
};
EffectDiff effectDiff(const forecastEffects::State & before, const forecastEffects::State & after)
{
	EffectDiff result;
	for(const auto & record : before.values()) result.removed.push_back(*record.value);
	for(const auto & record : after.values())
	{
		const auto old = std::ranges::find_if(result.removed, [&](const Bonus & candidate)
		{ return battle::exactBattleEffectEqual(candidate, *record.value); });
		if(old == result.removed.end()) result.added.push_back(*record.value);
		else result.removed.erase(old);
	}
	return result;
}
void validateAcceptedOwnership(const HypotheticBattle & branch, const State & accepted,
	const battle::Unit * actor, const AttackPossibility & action)
{
	require(action.snapshotSeal && action.snapshotSeal->matches(action) && action.overwatchHits.empty());
	require(actor && action.queryWorld && action.attackerState && action.attack.attacker && action.attack.defender);
	const auto & query = action.queryWorld->callback();
	require(query && sameWorld(branch, *query));
	require(query->battleGetUnitByID(action.attack.attacker->unitId()) == action.attack.attacker
		&& query->battleGetUnitByID(action.attack.defender->unitId()) == action.attack.defender);
	heroActionProjection::CanonicalSourceAdmission proof(*action.attack.attacker, *query);
	require(proof.admits(*action.attack.defender));
	require(action.forecastSource && action.forecastSource->sameSnapshot(accepted));
	const auto * targetActor = branch.battleGetUnitByID(actor->unitId());
	require(targetActor == actor && action.attack.attacker->unitId() == actor->unitId()
		&& action.attackerState->unitId() == actor->unitId());
	if(action.effectPreview) require(sameWorld(branch, *action.effectPreview));
	NativeAdapter::validateAcceptedSnapshot(*action.attackerState, action);
	for(const auto & affected : action.affectedUnits)
		if(affected) NativeAdapter::validateAcceptedSnapshot(*affected, action);
}
}

void NativeAdapter::replayAcceptedAttackNative(HypotheticBattle & branch, const battle::Unit * actor,
	const AttackPossibility & action, AcceptedAttackRoute route)
{
	auto attacker = branch.getForUpdate(actor->unitId());
	const bool actorAlive = attacker->alive();
	const bool committedPrefix = gnollHarriersAI::replaysSameApproach(action, *attacker);
	*attacker = *action.attackerState;
	branch.recordBloodrageTransition(attacker, actorAlive);
	if(action.defenderDamageReduce > 0)
	{
		attacker->removeUnitBonus(Bonus::UntilAttack);
		attacker->removeUnitBonus(Bonus::UntilOwnAttack);
	}
	if(action.attackerDamageReduce > 0) attacker->removeUnitBonus(Bonus::UntilBeingAttacked);
	if(route == AcceptedAttackRoute::BestAction) breakSanctuary(*attacker);
	for(const auto & affected : action.affectedUnits)
	{
		if(!affected) continue;
		auto target = branch.getForUpdate(affected->unitId());
		const bool wasAlive = target->alive();
		*target = *affected;
		branch.recordBloodrageTransition(target, wasAlive);
		if(action.defenderDamageReduce > 0) target->removeUnitBonus(Bonus::UntilBeingAttacked);
		if(action.attackerDamageReduce > 0 && action.attack.defender->unitId() == affected->unitId())
			target->removeUnitBonus(Bonus::UntilAttack);
	}
	if(route == AcceptedAttackRoute::HeroQueue)
		AttackPreviewHitContext::replayOrdinaryStrikeEffects(branch, action);
	else
	{
		if(action.effectPreview) PhysicalStrikeProjection::restoreEffects(branch, *action.effectPreview);
		gnollHarriersAI::restoreEffects(branch, action, committedPrefix);
	}
	focusMagicAI::restoreAcceptedRecords(*attacker, *action.attackerState);
	for(const auto & affected : action.affectedUnits)
		if(affected) focusMagicAI::restoreAcceptedRecords(*branch.getForUpdate(affected->unitId()), *affected);
}

bool NativeAdapter::applyAcceptedAttack(HypotheticBattle & branch, const battle::Unit * actor,
	const AttackPossibility & action, AcceptedAttackRoute route)
{
	if(!branch.compactForecast || action.uncertainAfflictionProjection) return false;
	validateAcceptedOwnership(branch, *branch.compactForecast, actor, action);

	// Charge bounded all-peer staging before any native replay prefix. The
	// branch and its stable target objects remain untouched on every failure.
	const auto ids = branch.compactForecast->ids();
	if(branch.searchBudget()) branch.searchBudget()->require(battleSearch::WorkKind::Node,
		static_cast<uint32_t>(ids.size() * (ids.size() + 1)));
	// Map handles retain target identity without another simulation graph.
	// Failure restores registrations by no-throw swap without callbacks.
	auto originalMap = branch.stackStates;
	try
	{
		// Declared before prospective so its journal outlives replay on exceptions.
		std::set<uint32_t> acquired;
		auto borrowed = std::shared_ptr<CBattleInfoCallback>(&branch, [](CBattleInfoCallback *) {});
		HypotheticBattle prospective(branch.env, std::move(borrowed),
			std::shared_ptr<const battle::OwnedBattleFrameBatch>{}, branch.searchBudget());
		// The generic child constructor does not capture the duplicate-death set.
		// Seed the COMPLETE native replay ledger before the first accepted death;
		// publishing a fresh set would otherwise erase earlier casualty receipts.
		prospective.bloodrageDestroyedUnits = branch.bloodrageDestroyedUnits;
		prospective.bloodrageDamagePercents = branch.bloodrageDamagePercents;
		prospective.bloodrageFirstBloodUsed = branch.bloodrageFirstBloodUsed;
		std::vector<std::shared_ptr<StackWithBonuses>> targets, staged;
		targets.reserve(ids.size()); staged.reserve(ids.size());
		for(const auto id : ids)
		{
			auto target = branch.getForUpdateImpl(id);
			require(target && target->usesStateEnvironment(*branch.unitEnvironment()));
			auto next = prepareEffectDraft(*target, &prospective);
			require(target->preparedEffectSourceMatches(*next));
			targets.push_back(target);
			staged.emplace_back(std::move(next));
			prospective.stackStates[id] = staged.back();
		}
		prospective.acceptedReplayAcquisitions = &acquired;
		replayAcceptedAttackNative(prospective, prospective.battleGetUnitByID(actor->unitId()), action, route);
		prospective.acceptedReplayAcquisitions = nullptr;
		for(const auto id : acquired) require(branch.compactForecast->unit(id) != nullptr);
		auto draft = *branch.compactForecast;
		for(size_t index = 0; index < ids.size(); ++index)
		{
			const auto id = ids[index];
			const auto & effects = draft.unit(id)->effects;
			if(!acquired.contains(id) && effects->values().empty()
				&& effects->revision() != std::numeric_limits<uint64_t>::max())
			{
				// Proven effect-free untouched peer; all other peers retain the full path.
				// Preserve the old no-op receipt debit and its roster-order cutoff.
				if(branch.searchBudget()) branch.searchBudget()->require(battleSearch::WorkKind::Node,
					1);
				continue;
			}
			auto & after = *staged[index];
			const auto captured = captureEffects(after, prospective.battleGetRound());
			const auto diff = effectDiff(*draft.unit(id)->effects, *captured);
			const auto receipt = prepareEffectApplication(draft, branch, id, diff.removed, diff.added);
			require(stageEffects(draft, branch, id, receipt, after, captureFocus(after), true));
			auto next = *draft.unit(id);
			// Import the COMPLETE accepted ledger even when HP/count happen to be
			// unchanged: casualty provenance and temporary/cohort capacity can differ.
			next.health.updateDetachedContext(after.getHealth().valueContext());
			next.health = battle::CHealth::detached(after.getHealth(), after.getHealth().valueContext());
			const bool moved = next.position != after.getPosition();
			next.position = after.getPosition();
			next.controller = prospective.battleGetActionController(&after);
			draft.commit(id, std::move(next), Health | Queue | Damage | (moved ? Occupancy | Adjacency : Clean), moved);
		}
		// Full peer refresh/publication also preserves read-only counter-cache observations.
		refreshOrderDerived(prospective, draft);
		for(size_t index = 0; index < ids.size(); ++index)
			require(targets[index]->preparedEffectSourceMatches(*staged[index]));
		static_assert(std::is_nothrow_move_assignable_v<decltype(branch.compactForecast)>);
		// No allocations, native queries, callbacks or budget checks after here.
		for(size_t index = 0; index < ids.size(); ++index)
		{
			branch.invalidateLifeDrainFriendlyObservation(ids[index]);
			targets[index]->publishPreparedEffects(*staged[index]);
			battle::exchangePreparedUnitStateValues(*targets[index], *staged[index]);
		}
		branch.bloodrageDestroyedUnits.swap(prospective.bloodrageDestroyedUnits);
		branch.bloodrageDamagePercents = prospective.bloodrageDamagePercents;
		branch.bloodrageFirstBloodUsed = prospective.bloodrageFirstBloodUsed;
		branch.compactForecast = std::move(draft);
		return true;
	}
	catch(...)
	{
		branch.stackStates.swap(originalMap);
		throw;
	}
}
}
