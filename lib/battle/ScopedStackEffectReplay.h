/* Part of VCMI. Authors: see AUTHORS. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "GraveDustEffectChanges.h"
#include "CUnitState.h"
#include "StackEffectRecordRules.h"
#include "StackEffectVectorRules.h"
#include "StackEffectRefresh.h"
#include "StackEffectRemoval.h"
#include "BattleForm.h"
#include "NewHorizonsSwiftRebirth.h"
#include "NewHorizonsConfusionControl.h"
#include "NewHorizonsFrozen.h"
#include "PhysicalAffliction.h"
#include "../bonuses/BonusList.h"
namespace battle
{
class ScopedStackEffectQueries;
/// Concrete native wrapper; the same template below is reused by the explicit
/// owned-ledger backend. Neither path may import a player view's native graph.
DLL_LINKAGE void replayScopedStackEffectVector(ScopedStackEffectQueries & queries, const IBattleInfo & battle, uint32_t id,
	const std::vector<Bonus> & records, newHorizonsGraveDust::EffectOperation operation);
}
/// Compile-time reuse of one record/vector algorithm for the two concrete
/// private storage owners; no generic port class, callback store or dispatcher.
namespace battle
{
namespace stackEffectReplayDetail
{
template<typename Queries>
void removeRecord(Queries & queries, uint32_t id, const Bonus & value)
{
	const auto & unit = queries.current(id);
	if(stackEffectRecordRules::removalProtected(unit, value)) return;
	auto & state = queries.state(id);
	if(stackEffectRecordRules::selectsCurrentFormMarker(state, value)
		&& !endBattleFormAtNearestLegalPosition(state, queries.accessibility(id))) return;
	const CSelector selected([&value](const Bonus * candidate)
	{
		return matchesStackEffectRemoval(value, *candidate);
	});
	const bool confusion = stackEffectRecordRules::removesConfusion(unit, value);
	queries.removeMatchingExports(id, selected);
	stackEffectRecordRules::finishConfusionRemoval(state, confusion);
	if(value.type == BonusType::GUARDIAN_SPIRIT)
		stackEffectRecordRules::restoreGuardianAfterRemoval(state);
}
template<typename Queries>
void incomingRecord(Queries & queries, uint32_t id, const Bonus & value, bool forceAdd)
{
	const auto & unit = queries.current(id);
	if(newHorizonsSwiftRebirth::isLifecycleMarker(value))
	{
		const auto previous = stackEffectRecordRules::previousSwiftLifecycle(unit, value, forceAdd, queries.exportedRecords(id));
		if(previous) queries.removeExported(id, previous);
		queries.appendExported(id, std::make_shared<Bonus>(value));
		return;
	}
	if(value.type == BonusType::CONFUSION_PENDING) newHorizonsConfusionControl::validateMarker(value);
	if(value.type == BonusType::PHYSICAL_AFFLICTION) physicalAfflictions::markerMetadata(value);
	if(stackEffectRecordRules::incomingProtected(unit, value)) return;
	auto & state = queries.state(id);
	if(value.type == BonusType::CONFUSION_PENDING)
	{
		auto next = state.confusionState;
		next.applyPending(value.spellCasterOwner, value.val == 2);
		queries.removeMatchingExports(id, Selector::type()(BonusType::CONFUSION_PENDING));
		queries.appendExported(id, std::make_shared<Bonus>(value));
		state.confusionState = next;
		return;
	}
	if(value.type == BonusType::PHYSICAL_AFFLICTION)
	{
		const auto replacement = stackEffectRecordRules::physicalMarkerReplacement(value, queries.exportedRecords(id));
		if(replacement.replaceExisting) queries.removeMatchingExports(id, stackEffectRecordRules::physicalMarkerGroup(value));
		queries.appendExported(id, std::make_shared<Bonus>(replacement.marker));
		return;
	}
	if(value.type == BonusType::GUARDIAN_SPIRIT)
	{
		const auto pool = stackEffectRecordRules::guardianPool(value);
		queries.removeMatchingExports(id, Selector::type()(BonusType::GUARDIAN_SPIRIT));
		queries.appendExported(id, std::make_shared<Bonus>(value));
		state.guardianSpiritHitPoints = pool.hitPoints;
		state.guardianSpiritRoundsRemaining = pool.rounds;
		return;
	}
	const bool legacy = unit.hasBonus(stackEffectRefresh::legacyCandidateSelector(value));
	const auto plan = stackEffectRefresh::plan(forceAdd, value, queries.exportedRecords(id), legacy);
	if(plan.append) queries.appendExported(id, std::make_shared<Bonus>(value));
	else queries.refreshMatchingExports(id, value);
}
}
template<typename Queries>
void replayStackEffectVector(Queries & queries, const IBattleInfo & battle, uint32_t id,
	const std::vector<Bonus> & records, newHorizonsGraveDust::EffectOperation operation)
{
	if(operation == newHorizonsGraveDust::EffectOperation::REMOVE)
		stackEffectVectorRules::validateRemovalRecords(records);
	else
		stackEffectVectorRules::validateIncomingRecords(battle, id, records, [&queries](uint32_t source)
		{
			const auto * unit = queries.find(source);
			return unit ? battle::Units{unit} : battle::Units{};
		});
	const auto * unit = queries.find(id);
	if(!unit) return;
	newHorizonsGraveDust::validateMutation(battle, *unit, records, operation);
	if(operation == newHorizonsGraveDust::EffectOperation::REMOVE)
	{
		for(const auto & value : records) stackEffectReplayDetail::removeRecord(queries, id, value);
		return;
	}
	stackEffectVectorRules::validateSwiftRecords(*unit, records, operation == newHorizonsGraveDust::EffectOperation::ADD);
	const auto stamped = physicalAfflictions::stampApplicationOrder(queries.current(id), records);
	const auto frozen = newHorizonsFrozen::prepareApplication(queries.current(id), records);
	for(const auto & value : stamped)
		stackEffectReplayDetail::incomingRecord(queries, id, value, operation == newHorizonsGraveDust::EffectOperation::ADD);
	if(frozen) queries.state(id).commitPreparedFrozenApplication(*frozen);
}
}
