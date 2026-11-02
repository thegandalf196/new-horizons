/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedPreparationAssertions.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/battle/OwnedBattleFrameBatch.h"
#include "../../lib/battle/NewHorizonsGraveDust.h"
#include "../../lib/battle/HealingReceipt.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include <stdexcept>
#include <utility>
namespace ownedPreparationIntegration
{
namespace
{
void require(bool value, const char * reason)
{
	if(!value) throw std::runtime_error(reason);
}
template<typename Operation> void rejected(Operation operation)
{
	try { operation(); }
	catch(const std::invalid_argument &) { return; }
	throw std::runtime_error("Partial ordered Dust removal was not rejected as invalid_argument");
}
void orderedRemoval(const std::shared_ptr<HypotheticBattle> & parent, uint32_t target)
{
	const auto unit = parent->getForUpdate(target);
	require(unit != nullptr, "Missing materialized assertion parent");
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	const Bonus stopped(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	unit->addUnitBonus(dust); unit->addUnitBonus({stopped});
	auto sibling = std::make_shared<HypotheticBattle>(parent->env, parent);
	const auto other = sibling->getForUpdate(target);
	require(other != nullptr, "Missing materialized assertion sibling");
	const auto before = unit->save(), siblingBefore = other->save();
	for(const auto & partial : dust)
	{
		rejected([&] { unit->removeUnitBonus(std::vector<Bonus>{stopped, partial}); });
		require(unit->isTimeStopped() && unit->save() == before
			&& battle::healingReceivedReduction(*unit) == 5000,
			"Partial clock-first removal changed state/clock/Dust before rejecting");
	}
	SetStackEffect packet; packet.battleID = parent->getBattleID();
	packet.toRemove.emplace_back(target, std::vector<Bonus>{stopped});
	packet.toRemove.emplace_back(target, std::vector<Bonus>{dust.front()});
	rejected([&] { parent->getServerCallback()->apply(packet); });
	require(unit->isTimeStopped() && unit->save() == before
		&& battle::healingReceivedReduction(*unit) == 5000, "Multi-vector packet changed state before rejecting");
	unit->removeUnitBonus(std::vector<Bonus>{dust.front(), stopped});
	require(!unit->isTimeStopped() && battle::healingReceivedReduction(*unit) == 5000,
		"Dust-first removal lost its canonical protected no-op ordering");
	require(other->isTimeStopped() && battle::healingReceivedReduction(*other) == 5000
		&& other->save() == siblingBefore,
		"Ordered removal mutated the materialized sibling");
}
void incomingClock(const std::shared_ptr<HypotheticBattle> & parent, uint32_t target)
{
	const auto unit = parent->getForUpdate(target);
	require(unit != nullptr, "Missing materialized assertion parent");
	auto sibling = std::make_shared<HypotheticBattle>(parent->env, parent);
	const auto other = sibling->getForUpdate(target);
	require(other != nullptr, "Missing materialized assertion sibling");
	const auto siblingBefore = other->save();
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	const Bonus stopped(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	SetStackEffect incoming; incoming.battleID = parent->getBattleID();
	incoming.toAdd.emplace_back(target, std::vector<Bonus>{stopped, dust.front(), dust.back()});
	parent->getServerCallback()->apply(incoming); // Any exception fails; no expected-failure/skip route.
	require(unit->isTimeStopped() && battle::healingReceivedReduction(*unit) == 0,
		"Incoming clock did not protect the following complete Dust pair");
	const auto current = unit->getUnstackedBonuses(CSelector(newHorizonsGraveDust::isEffect));
	require(current->empty(), "Protected incoming Dust was published");
	require(!other->isTimeStopped() && other->save() == siblingBefore,
		"Incoming packet mutated the materialized sibling");
}
}
Assertions::Assertions(Scenario selected, uint32_t id) : scenario(selected), target(id)
{
	require(scenario == Scenario::ORDERED_REMOVAL || scenario == Scenario::INCOMING_CLOCK, "Unknown assertion scenario");
	observed.target = target;
	observed.coverageNotes.emplace_back("Fixture-only testRandomGenerator oracle remains required in native regressions; no client counterpart exists");
	observed.coverageNotes.emplace_back("Prediction RNGStub has no mutable state; canonical fixed-function source proof, not fixture RNG bytes");
	observed.originalCase = scenario == Scenario::ORDERED_REMOVAL
		? "GraveDustSnapshotTest.MixedTimeStopRemovalPreflightsOrderedVectorBeforeOverlayMutation"
		: "GraveDustSnapshotTest.OrdinaryIncomingClockBeforeCompleteDustIsCanonicalProtectedNoOp";
}
void Assertions::onOwnedRoot(const std::shared_ptr<HypotheticBattle> & current,
	const std::shared_ptr<const battle::OwnedBattleFrameBatch> & source)
{
	std::lock_guard lock(evidenceMutex);
	try
	{
		require(observed.status == Status::NOT_OBSERVED, "Assertion observer delivered more than once");
		require(current && source && current->usesOwnedFrameQueries() && current->retainsOwnedFrameBatch(source),
			"Requires actual current owned preparation and exact retained Batch");
		require(source->observer() == PlayerColor(0) && current->getPlayerID() == PlayerColor(0)
			&& !current->initialExportSubject(), "P0 preparation was promoted or perspective changed");
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			require(current->battleGetVisibleHeroPolicy(side).kind() == source->heroKnowledge(side),
				"Owned preparation changed captured P0 hero knowledge");
		observed.battle = source->battle();
		require(current->getBattleID() == observed.battle, "Prepared battle identity mismatch");
		const auto & captured = source->frame(target);
		require(captured != nullptr, "Missing captured fixture occurrence");
		const auto * type = captured->unit().unitType();
		require(type && captured->unit().unitId() == target && captured->unit().getCount() == 2
			&& captured->unit().unitSide() == BattleSide::ATTACKER && captured->unit().unitOwner() == PlayerColor(0)
			&& type->getJsonKey() == "core:angel", "Requires captured P0 Angel2 fixture occurrence");
		observed.capturedState = captured->save();
		require(observed.capturedState->isStruct() && (*observed.capturedState)["state"].isStruct(),
			"Captured fixture must contain canonical serialized scalar state");
		const auto root = current->getForUpdate(target);
		require(root != nullptr, "Missing materialized assertion root");
		const auto rootBefore = root->save();
		require(!root->isTimeStopped() && battle::healingReceivedReduction(*root) == 0,
			"Fixture root must begin without TimeStop or Dust");
		auto parent = std::make_shared<HypotheticBattle>(current->env, current);
		require(parent->usesOwnedFrameQueries() && parent->retainsOwnedFrameBatch(source)
			&& !parent->initialExportSubject(), "Assertion child lost owned P0 source binding");
		if(scenario == Scenario::ORDERED_REMOVAL) orderedRemoval(parent, target);
		else incomingClock(parent, target);
		require(root->save() == rootBefore
			&& !root->isTimeStopped() && battle::healingReceivedReduction(*root) == 0
			&& captured->save() == *observed.capturedState, "Assertion branch changed its root or immutable Batch");
		observed.status = Status::PROJECTION_VERIFIED;
	}
	catch(const std::exception & error)
	{
		observed.status = Status::FAILED; observed.issues.emplace_back(error.what());
	}
	catch(...)
	{
		observed.status = Status::FAILED; observed.issues.emplace_back("Unknown assertion exception");
	}
}
AssertionEvidence Assertions::evidence() const
{
	std::lock_guard lock(evidenceMutex);
	return observed;
}
AssertionEvidence Assertions::finish(const SourceObservations & before, const SourceObservations & after) const
{
	auto result = evidence();
	if(result.status == Status::FAILED) return result;
	if(result.status != Status::PROJECTION_VERIFIED)
	{
		result.status = Status::INCOMPLETE; result.issues.emplace_back("No verified genuine owned-root observation"); return result;
	}
	if(before.battle != result.battle || after.battle != result.battle || before.target != target || after.target != target)
	{
		result.status = Status::FAILED; result.issues.emplace_back("Source observations belong to another battle/target"); return result;
	}
	result.status = Status::COMPLETE;
	const auto incomplete = [&](const char * reason)
	{
		if(result.status != Status::FAILED) result.status = Status::INCOMPLETE;
		result.issues.emplace_back(reason);
	};
	const auto mismatch = [&](const char * reason)
	{
		result.status = Status::FAILED; result.issues.emplace_back(reason);
	};
	if(!before.nativeState || !after.nativeState) incomplete("Missing genuine native scalar snapshots");
	else if(*before.nativeState != *after.nativeState || *before.nativeState != *result.capturedState)
		mismatch("Native scalar state changed or does not match the captured fixture occurrence");
	if(!before.nativeTimeStopped || !after.nativeTimeStopped) incomplete("Missing genuine native TimeStop observations");
	else if(*before.nativeTimeStopped || *after.nativeTimeStopped) mismatch("Projection changed native TimeStop");
	if(!before.nativeHealingReduction || !after.nativeHealingReduction) incomplete("Missing genuine native healing observations");
	else if(*before.nativeHealingReduction != 0 || *after.nativeHealingReduction != 0) mismatch("Projection changed native Dust");
	if(scenario == Scenario::ORDERED_REMOVAL)
	{
		if(!before.authoritativeSimulationRng || !after.authoritativeSimulationRng)
			incomplete("Missing authoritative simulation RNG before/after bytes");
		else if(before.authoritativeSimulationRng->empty() || *before.authoritativeSimulationRng != *after.authoritativeSimulationRng)
			mismatch("Authoritative simulation RNG observation is empty or changed");
	}
	return result;
}
}
