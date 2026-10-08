/*
 * NewHorizonsConfusionStateTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "../server/battles/BattleTestFixture.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsConfusionState.h"
#include "../../lib/gameState/GameStatePackVisitor.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/mapObjects/army/CStackBasicDescriptor.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "mock/mock_BonusBearer.h"
#include "mock/mock_UnitEnvironment.h"
#include "mock/mock_UnitInfo.h"

using namespace battle;
using namespace testing;

namespace
{
ConfusionState pendingWithHistory()
{
	ConfusionState result;
	result.recordResolved(ConfusionBehavior::WANDER);
	result.applyPending(PlayerColor(1), true);
	return result;
}

class ConfusionStateEnvironment final : public ::Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit ConfusionStateEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsConfusionStateTest : public BattleTestFixture
{
protected:
	NiceMock<UnitInfoMock> info;
	NiceMock<UnitEnvironmentMock> environment;
	BonusBearerMock bonuses;
	CUnitStateDetached state{&info, &bonuses};

	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		ON_CALL(info, unitBaseAmount()).WillByDefault(Return(2));
		ON_CALL(info, unitType()).WillByDefault(Return(CreatureID(CreatureID::IMP).toCreature()));
		ON_CALL(info, unitSide()).WillByDefault(Return(BattleSide::ATTACKER));
		ON_CALL(info, unitOwner()).WillByDefault(Return(PlayerColor(0)));
		ON_CALL(info, unitId()).WillByDefault(Return(1));
		ON_CALL(info, unitSlot()).WillByDefault(Return(SlotID(0)));
		ON_CALL(environment, unitHasAmmoCart(_)).WillByDefault(Return(false));
		ON_CALL(environment, unitEffectiveOwner(_)).WillByDefault(Return(PlayerColor(0)));
		bonuses.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::STACK_HEALTH, BonusSource::CREATURE_ABILITY, 10, CreatureID(0)));
		state.localInit(&environment);
		state.setPosition(BattleHex(4, 5));
	}

	JsonNode snapshot()
	{
		return state.save();
	}

	UnitChanges update()
	{
		UnitChanges result(1, UnitChanges::EOperation::UPDATE);
		result.data = snapshot();
		return result;
	}
};
}

TEST_F(NewHorizonsConfusionStateTest, EmptyStateHasNoProvenanceOrHistory)
{
	EXPECT_FALSE(state.confusionState.hasState());
	EXPECT_NO_THROW(state.confusionState.validate());
	EXPECT_EQ(state.confusionState, ConfusionState{});
}

TEST_F(NewHorizonsConfusionStateTest, ReapplicationReplacesPendingProvenanceWithoutErasingHistory)
{
	state.confusionState = pendingWithHistory();
	state.confusionState.applyPending(PlayerColor(0), false);
	EXPECT_TRUE(state.confusionState.pending);
	EXPECT_EQ(state.confusionState.pendingCaster, PlayerColor(0));
	EXPECT_FALSE(state.confusionState.pendingConfounder);
	EXPECT_EQ(state.confusionState.previousResolved, ConfusionBehavior::WANDER);
}

TEST_F(NewHorizonsConfusionStateTest, ConsumingPendingForMoraleDoesNotRecordAnotherBehavior)
{
	state.confusionState = pendingWithHistory();
	state.afterGetsTurn(BattleUnitTurnReason::MORALE);
	EXPECT_EQ(state.confusionState, pendingWithHistory()) << "a turn hook is not a Confusion resolution";
	state.confusionState.clearPending();
	EXPECT_FALSE(state.confusionState.pending);
	EXPECT_EQ(state.confusionState.pendingCaster, PlayerColor::CANNOT_DETERMINE);
	EXPECT_FALSE(state.confusionState.pendingConfounder);
	EXPECT_EQ(state.confusionState.previousResolved, ConfusionBehavior::WANDER);
	EXPECT_TRUE(state.confusionState.hasState());
}

TEST_F(NewHorizonsConfusionStateTest, ResolutionRecordsEachActualBehaviorAndClearsPending)
{
	for(const auto behavior : {ConfusionBehavior::ATTACK, ConfusionBehavior::DEFEND, ConfusionBehavior::WANDER})
	{
		state.confusionState = pendingWithHistory();
		state.confusionState.recordResolved(behavior);
		EXPECT_FALSE(state.confusionState.pending);
		EXPECT_EQ(state.confusionState.pendingCaster, PlayerColor::CANNOT_DETERMINE);
		EXPECT_FALSE(state.confusionState.pendingConfounder);
		EXPECT_EQ(state.confusionState.previousResolved, behavior);
		EXPECT_NO_THROW(state.confusionState.validate());
	}
	EXPECT_THROW(state.confusionState.recordResolved(ConfusionBehavior::NONE), std::runtime_error);
}

TEST_F(NewHorizonsConfusionStateTest, RoundAndOrdinaryActivationRetainPendingAndHistory)
{
	state.confusionState = pendingWithHistory();
	state.afterNewRound(false);
	EXPECT_EQ(state.confusionState, pendingWithHistory());
	state.afterGetsTurn(BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(state.confusionState, pendingWithHistory());
}

TEST_F(NewHorizonsConfusionStateTest, FullInitializationResetClearsThePreviousBattle)
{
	state.confusionState = pendingWithHistory();
	state.localInit(&environment);
	EXPECT_EQ(state.confusionState, ConfusionState{});
}

TEST_F(NewHorizonsConfusionStateTest, ActualAcquiredStateAndAssignmentPreserveIndependentHistory)
{
	state.confusionState = pendingWithHistory();
	auto copied = state.acquireState();
	EXPECT_EQ(copied->confusionState, state.confusionState);
	copied->confusionState.recordResolved(ConfusionBehavior::ATTACK);
	EXPECT_EQ(state.confusionState, pendingWithHistory());
	CUnitStateDetached assigned(&info, &bonuses);
	assigned.localInit(&environment);
	assigned.CUnitState::operator=(state);
	EXPECT_EQ(assigned.confusionState, state.confusionState);
	assigned.confusionState.clearPending();
	EXPECT_TRUE(state.confusionState.pending);
}

TEST_F(NewHorizonsConfusionStateTest, JsonRoundTripPreservesBothStateAndOrdinaryHealth)
{
	for(const auto behavior : {ConfusionBehavior::ATTACK, ConfusionBehavior::DEFEND, ConfusionBehavior::WANDER})
	{
		state.confusionState.recordResolved(behavior);
		state.confusionState.applyPending(PlayerColor(1), true);
		auto restored = state.acquireState();
		restored->confusionState = {};
		restored->load(snapshot());
		EXPECT_EQ(restored->confusionState, state.confusionState);
		EXPECT_EQ(restored->getAvailableHealth(), state.getAvailableHealth());
		EXPECT_EQ(restored->getPosition(), state.getPosition());
	}
}

TEST_F(NewHorizonsConfusionStateTest, MissingOldJsonDefaultsEvenOnAPreviouslyPopulatedDestination)
{
	state.confusionState = pendingWithHistory();
	auto old = snapshot();
	old["state"].Struct().erase("confusion");
	auto restored = state.acquireState();
	restored->load(old);
	EXPECT_EQ(restored->confusionState, ConfusionState{});
}

TEST_F(NewHorizonsConfusionStateTest, JsonRejectsMalformedTypesEnumAndProvenance)
{
	state.confusionState = pendingWithHistory();
	const auto good = snapshot();
	std::vector<JsonNode> invalid;
	const auto mutate = [&invalid, &good](const std::string & key, JsonNode value)
	{
		auto bad = good;
		bad["state"]["confusion"][key] = std::move(value);
		invalid.push_back(std::move(bad));
	};
	JsonNode text; text.String() = "true";
	mutate("pending", text);
	mutate("pendingConfounder", text);
	mutate("pendingCaster", text);
	mutate("previousResolved", text);
	JsonNode number; number.Integer() = 4;
	mutate("previousResolved", number);
	mutate("pending", number);
	mutate("pendingConfounder", number);
	number.Integer() = -1; mutate("previousResolved", number);
	number.Integer() = PlayerColor::UNFLAGGABLE.getNum();
	mutate("pendingCaster", number);
	number.Float() = 1.5; mutate("previousResolved", number);
	mutate("pendingCaster", number);
	number.Integer() = 256; mutate("pendingCaster", number);
	number.Integer() = 8; mutate("pendingCaster", number);
	number.Integer() = PlayerColor::CANNOT_DETERMINE.getNum(); mutate("pendingCaster", number);
	JsonNode flag; flag.Bool() = false; mutate("pending", flag);
	auto wrongNode = good;
	wrongNode["state"]["confusion"] = text;
	invalid.push_back(wrongNode);
	wrongNode = good;
	wrongNode["state"]["confusion"]["unknown"] = text;
	invalid.push_back(wrongNode);
	for(size_t i = 0; i < invalid.size(); ++i)
	{
		SCOPED_TRACE(i);
		auto restored = state.acquireState();
		EXPECT_THROW(restored->load(invalid[i]), std::runtime_error);
		UnitChanges change(1, UnitChanges::EOperation::UPDATE);
		change.data = invalid[i];
		CMemorySerializer unitBytes;
		EXPECT_THROW(unitBytes.oser & change, std::runtime_error);
		EXPECT_TRUE(unitBytes.extractBuffer().empty());
		BattleUnitsChanged packet;
		packet.battleID = BattleID(0);
		packet.changedStacks.push_back(change);
		CMemorySerializer packetBytes;
		EXPECT_THROW(packetBytes.oser & packet, std::runtime_error);
		EXPECT_TRUE(packetBytes.extractBuffer().empty());
	}
}

TEST_F(NewHorizonsConfusionStateTest, ValueValidationRejectsImpossibleMetadataAndAcceptsNeutralCaster)
{
	ConfusionState invalid;
	invalid.pendingConfounder = true;
	EXPECT_THROW(invalid.validate(), std::runtime_error);
	invalid = {};
	invalid.pendingCaster = PlayerColor(0);
	EXPECT_THROW(invalid.validate(), std::runtime_error);
	invalid = {};
	invalid.pending = true;
	EXPECT_THROW(invalid.validate(), std::runtime_error);
	invalid = {};
	invalid.previousResolved = static_cast<ConfusionBehavior>(255);
	EXPECT_THROW(invalid.validate(), std::runtime_error);
	ConfusionState neutral;
	EXPECT_NO_THROW(neutral.applyPending(PlayerColor::NEUTRAL, false));
	EXPECT_NO_THROW(neutral.validate());
	state.confusionState = neutral;
	auto restored = state.acquireState();
	restored->confusionState = {};
	ASSERT_NO_THROW(restored->load(snapshot()));
	EXPECT_EQ(restored->confusionState, neutral);
	EXPECT_EQ(restored->confusionState.pendingCaster, PlayerColor::NEUTRAL);
}

TEST_F(NewHorizonsConfusionStateTest, CurrentStackDescriptorRoundTripsExplicitStateNotGeneralBattleHealth)
{
	CStackBasicDescriptor base(CreatureID(CreatureID::IMP), 2);
	CStack descriptor(&base, PlayerColor(0), 1, BattleSide::ATTACKER);
	descriptor.confusionState = pendingWithHistory();
	CMemorySerializer bytes;
	bytes.oser & descriptor;
	CStack restored;
	bytes.iser & restored;
	EXPECT_EQ(restored.confusionState, descriptor.confusionState);
	EXPECT_EQ(restored.unitBaseAmount(), 2);
	// This asserts the explicit appended field, not a whole mutable-health save.
	descriptor.confusionState.previousResolved = static_cast<ConfusionBehavior>(255);
	CMemorySerializer invalid;
	EXPECT_THROW(invalid.oser & descriptor, std::runtime_error);
	EXPECT_TRUE(invalid.extractBuffer().empty()) << "validate before the descriptor's bonus-node payload";
}

TEST_F(NewHorizonsConfusionStateTest, OldStackDescriptorDefaultsAndRejectsEitherMeaningfulFieldBeforePayload)
{
	CStackBasicDescriptor base(CreatureID(CreatureID::IMP), 2);
	CStack descriptor(&base, PlayerColor(0), 1, BattleSide::ATTACKER);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_OVERWHELMING_FORMULA;
	old.iser.version = old.oser.version;
	old.oser & descriptor;
	CStack restored;
	restored.confusionState = pendingWithHistory();
	old.iser & restored;
	EXPECT_EQ(restored.confusionState, ConfusionState{});
	for(const bool pending : {false, true})
	{
		descriptor.confusionState = {};
		if(pending) descriptor.confusionState.applyPending(PlayerColor(0), false);
		else descriptor.confusionState.recordResolved(ConfusionBehavior::DEFEND);
		CMemorySerializer rejected;
		rejected.oser.version = old.oser.version;
		EXPECT_THROW(rejected.oser & descriptor, std::runtime_error);
		EXPECT_TRUE(rejected.extractBuffer().empty());
	}
}

TEST_F(NewHorizonsConfusionStateTest, CurrentUnitAndOuterPacketWireRoundTrips)
{
	state.confusionState = pendingWithHistory();
	auto change = update();
	CMemorySerializer unitBytes;
	unitBytes.oser & change;
	UnitChanges restored;
	unitBytes.iser & restored;
	EXPECT_EQ(restored.data["state"]["confusion"], change.data["state"]["confusion"]);
	BattleUnitsChanged packet;
	packet.battleID = BattleID(0);
	packet.changedStacks.push_back(change);
	CMemorySerializer packetBytes;
	packetBytes.oser & packet;
	BattleUnitsChanged restoredPacket;
	packetBytes.iser & restoredPacket;
	ASSERT_EQ(restoredPacket.changedStacks.size(), 1);
	EXPECT_EQ(restoredPacket.battleID, packet.battleID);
	EXPECT_EQ(restoredPacket.changedStacks.front().data, change.data);
}

TEST_F(NewHorizonsConfusionStateTest, OldUnitAndOuterPacketRejectPendingOrHistoryBeforeAnyPayload)
{
	for(const bool pending : {false, true})
	{
		state.confusionState = {};
		if(pending) state.confusionState.applyPending(PlayerColor(0), false);
		else state.confusionState.recordResolved(ConfusionBehavior::ATTACK);
		auto change = update();
		CMemorySerializer unitBytes;
		unitBytes.oser.version = ESerializationVersion::NEW_HORIZONS_OVERWHELMING_FORMULA;
		EXPECT_THROW(unitBytes.oser & change, std::runtime_error);
		EXPECT_TRUE(unitBytes.extractBuffer().empty());
		BattleUnitsChanged packet;
		packet.battleID = BattleID(0);
		packet.changedStacks.push_back(change);
		CMemorySerializer packetBytes;
		packetBytes.oser.version = unitBytes.oser.version;
		EXPECT_THROW(packetBytes.oser & packet, std::runtime_error);
		EXPECT_TRUE(packetBytes.extractBuffer().empty()) << "reject before the enclosing battle ID or vector header";
	}
}

TEST_F(NewHorizonsConfusionStateTest, OldEmptyUnitAndPacketRemainReadable)
{
	auto change = update();
	BattleUnitsChanged packet;
	packet.battleID = BattleID(0);
	packet.changedStacks.push_back(change);
	CMemorySerializer bytes;
	bytes.oser.version = ESerializationVersion::NEW_HORIZONS_OVERWHELMING_FORMULA;
	bytes.iser.version = bytes.oser.version;
	ASSERT_NO_THROW(bytes.oser & packet);
	BattleUnitsChanged restored;
	ASSERT_NO_THROW(bytes.iser & restored);
	ASSERT_EQ(restored.changedStacks.size(), 1);
	auto unit = state.acquireState();
	unit->confusionState = pendingWithHistory();
	unit->load(restored.changedStacks.front().data);
	EXPECT_EQ(unit->confusionState, ConfusionState{});
}

TEST_F(NewHorizonsConfusionStateTest, LoadedStackInitializationPreservesBothExplicitFields)
{
	startGame();
	startBattle();
	CStackBasicDescriptor base(CreatureID(CreatureID::IMP), 2);
	CStack descriptor(&base, PlayerColor(0), 100, BattleSide::ATTACKER);
	descriptor.initialPosition = BattleHex(4, 5);
	descriptor.confusionState = pendingWithHistory();
	CMemorySerializer bytes;
	bytes.oser & descriptor;
	CStack restored;
	bytes.iser.cb = gameState().get();
	bytes.iser & restored;
	restored.localInit(battle());
	EXPECT_EQ(restored.confusionState, pendingWithHistory());
}

TEST_F(NewHorizonsConfusionStateTest, DetachedVisitorUpdateAndNestedCopyPreserveHistoryWithoutMutatingLiveBattle)
{
	startGame();
	startBattle();
	auto * live = addStack(BattleSide::ATTACKER, CreatureID(CreatureID::IMP), BattleHex(4, 5), 2);
	ASSERT_NE(live, nullptr);
	live->confusionState = pendingWithHistory();
	ConfusionStateEnvironment env(gameState());
	auto liveCallback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(&env, liveCallback);
	auto projected = model->getForUpdate(live->unitId());
	EXPECT_EQ(projected->confusionState, live->confusionState);
	auto updateState = projected->acquireState();
	updateState->confusionState.clearPending();
	UnitChanges changed(live->unitId(), UnitChanges::EOperation::UPDATE);
	changed.data = updateState->save();
	BattleUnitsChanged packet;
	packet.battleID = BattleID(0);
	packet.changedStacks.push_back(changed);
	BattleStatePackVisitor visitor(*model);
	packet.visit(visitor);
	EXPECT_EQ(projected->confusionState, updateState->confusionState);
	EXPECT_EQ(live->confusionState, pendingWithHistory());
	HypotheticBattle nested(&env, model);
	auto nestedUnit = nested.getForUpdate(live->unitId());
	EXPECT_EQ(nestedUnit->confusionState, projected->confusionState);
	nestedUnit->confusionState.recordResolved(ConfusionBehavior::DEFEND);
	EXPECT_EQ(projected->confusionState.previousResolved, ConfusionBehavior::WANDER);
	EXPECT_EQ(live->confusionState, pendingWithHistory());
}
