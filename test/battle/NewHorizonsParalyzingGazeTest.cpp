/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../mock/mock_BonusBearer.h"
#include "../mock/mock_UnitEnvironment.h"
#include "../mock/mock_UnitInfo.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/NewHorizonsParalyzingGaze.h"
#include "../../lib/battle/StackEffectRemoval.h"
#include "../../lib/entities/hero/NewHorizonsParalyzingGaze.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/CObstacleInstance.h"
#include <utility>
#include "../../lib/networkPacks/PacksForClientBattle.h"
namespace
{
class NewHorizonsParalyzingGazeTest : public testing::Test
{
protected:
	UnitInfoMock info;
	UnitEnvironmentMock environment;
	BonusBearerMock bonuses;
	std::shared_ptr<battle::CUnitStateDetached> unit;
	void SetUp() override
	{
		using namespace testing;
		EXPECT_CALL(info, unitBaseAmount()).WillRepeatedly(Return(3));
		EXPECT_CALL(info, unitId()).WillRepeatedly(Return(7));
		EXPECT_CALL(info, unitSide()).WillRepeatedly(Return(BattleSide::ATTACKER));
		EXPECT_CALL(info, unitOwner()).WillRepeatedly(Return(PlayerColor(0)));
		EXPECT_CALL(info, unitSlot()).WillRepeatedly(Return(SlotID(0)));
		EXPECT_CALL(info, unitType()).WillRepeatedly(Return(CreatureID(0).toCreature()));
		bonuses.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::STACK_HEALTH, BonusSource::CREATURE_ABILITY, 10, BonusSourceID(CreatureID(0))));
		bonuses.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::STACKS_MOVEMENT_RANGE, BonusSource::OTHER, 6, BonusSourceID()));
		unit = std::make_shared<battle::CUnitStateDetached>(&info, &bonuses);
		unit->localInit(&environment);
		unit->setPosition(BattleHex(40));
	}
};
}

TEST_F(NewHorizonsParalyzingGazeTest, PendingIsInertUntilOtherwiseEligibleOpportunityAndPlanningIsPure)
{
	const auto cold = unit->save();
	EXPECT_TRUE(cold["state"]["counterAttacks"]["totalCache"].isNull());
	EXPECT_FALSE(newHorizonsParalyzingGaze::pending(*unit));
	bonuses.addNewBonus(std::make_shared<Bonus>(newHorizonsParalyzingGaze::marker()));
	ASSERT_TRUE(newHorizonsParalyzingGaze::pending(*unit));
	EXPECT_TRUE(unit->ableToRetaliate()); // Token is not NO_RETALIATION.
	// This existing ordinary-admission query initializes the saved round cap.
	// Characterize that one real field, then test Gaze planning from admission.
	const auto before = unit->save();
	auto expectedWarm = cold;
	expectedWarm["state"]["counterAttacks"]["totalCache"].Integer() = 1;
	EXPECT_EQ(before, expectedWarm)
		<< "cold=" << cold.toCompactString() << "\nwarm=" << before.toCompactString();
	EXPECT_TRUE(newHorizonsParalyzingGaze::canConsume(*unit));
	const auto after = unit->save();
	EXPECT_EQ(after, before) // Whole state, including the meaningful cap latch.
		<< "before=" << before.toCompactString() << "\nafter=" << after.toCompactString();
	bonuses.addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::NO_RETALIATION, BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_FALSE(newHorizonsParalyzingGaze::canConsume(*unit));
	EXPECT_TRUE(newHorizonsParalyzingGaze::pending(*unit));
}
TEST_F(NewHorizonsParalyzingGazeTest, ActivationAndRoundDoNotExpireOnePendingToken)
{
	const auto movement = unit->getMovementRange();
	bonuses.addNewBonus(std::make_shared<Bonus>(newHorizonsParalyzingGaze::marker()));
	unit->afterNewRound(false);
	unit->afterGetsTurn(BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_TRUE(newHorizonsParalyzingGaze::pending(*unit));
	EXPECT_EQ(unit->getMovementRange(), movement); // No full-paralysis movement penalty.
}
TEST(NewHorizonsParalyzingGazeMarker, CurrentRoundtripAndOldPrefixRejection)
{
	auto token = newHorizonsParalyzingGaze::marker();
	CMemorySerializer current;
	token.serialize(current.oser);
	Bonus read;
	read.serialize(current.iser);
	EXPECT_TRUE(newHorizonsParalyzingGaze::validateMarker(read));
	EXPECT_EQ(read.statusIdentity, token.statusIdentity);
	EXPECT_NE(read.source, BonusSource::SPELL_EFFECT);
	CMemorySerializer old, outer;
	old.oser.version = outer.oser.version = ESerializationVersion::NEW_HORIZONS_SERPENTS_EMBRACE;
	EXPECT_THROW(token.serialize(old.oser), std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	SetStackEffect packet;
	packet.toRemove.emplace_back(7, std::vector<Bonus>{token});
	EXPECT_THROW(packet.serialize(outer.oser), std::runtime_error);
	EXPECT_TRUE(outer.extractBuffer().empty());
	CMemorySerializer oldReader(current.extractBuffer());
	oldReader.iser.version = ESerializationVersion::NEW_HORIZONS_SERPENTS_EMBRACE;
	EXPECT_THROW(read.serialize(oldReader.iser), std::runtime_error);
}
TEST(NewHorizonsParalyzingGazeMarker, StructuralPreflightRequiresNoLibraryAndRejectsMalformedBeforePrefix)
{
	auto token = newHorizonsParalyzingGaze::marker(); // Full ID wire requires registry.
	auto * saved = std::exchange(LIBRARY, nullptr);
	struct Restore { GameLibrary * value; ~Restore() { LIBRARY = value; } } restore{saved};
	EXPECT_TRUE(newHorizonsParalyzingGaze::validateMarker(token));
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_SERPENTS_EMBRACE;
	EXPECT_THROW(token.serialize(old.oser), std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	token.duration = BonusDuration::N_TURNS;
	CMemorySerializer malformed;
	EXPECT_THROW(token.serialize(malformed.oser), std::runtime_error);
	EXPECT_TRUE(malformed.extractBuffer().empty());
}
TEST(NewHorizonsParalyzingGazeMarker, StaticRemovalIdentityDoesNotAliasOtherNoneStatuses)
{
	const auto token = newHorizonsParalyzingGaze::marker();
	auto other = token;
	other.sid = BonusSourceID(HeroTypeID(HeroTypeID::decode("core:fafner")));
	EXPECT_FALSE(battle::matchesStackEffectRemoval(token, other));
	other = token;
	other.subtype = BonusSubtypeID();
	EXPECT_FALSE(battle::matchesStackEffectRemoval(token, other));
	EXPECT_TRUE(battle::matchesStackEffectRemoval(token, token));
}
TEST(NewHorizonsParalyzingGazePolicy, CapturedAbsentDisabledAndUnsupportedAreDistinct)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
	rules["additionalSpecialties"]["paralyzingGaze"]["enabled"].Bool() = true;
	EXPECT_EQ(newHorizonsHeroes::paralyzingGazePolicy(rules), std::optional<bool>(true));
	EXPECT_THROW(newHorizonsHeroes::validateParalyzingGazeSerialization(rules, false), std::runtime_error);
	rules["additionalSpecialties"]["paralyzingGaze"]["enabled"].Bool() = false;
	EXPECT_EQ(newHorizonsHeroes::paralyzingGazePolicy(rules), std::optional<bool>(false));
	EXPECT_THROW(newHorizonsHeroes::validateParalyzingGazeSerialization(rules, false), std::runtime_error);
	rules["additionalSpecialties"].Struct().erase("paralyzingGaze");
	EXPECT_FALSE(newHorizonsHeroes::paralyzingGazePolicy(rules));
	EXPECT_NO_THROW(newHorizonsHeroes::validateParalyzingGazeSerialization(rules, false));
}

TEST(NewHorizonsParalyzingGazeMarker, NewDefiantCauseRejectsOldPrefixButHistoricalCausesKeepWireContract)
{
	SetArmorerDefiantState packet;
	packet.battleID = BattleID(0);
	packet.side = BattleSide::DEFENDER;
	packet.attackerId = 1;
	packet.targetId = 2;
	packet.state.consumeAt(0);
	packet.cause = newHorizonsArmorer::DefiantDenialCause::PARALYZING_GAZE;
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_SERPENTS_EMBRACE;
	EXPECT_THROW(packet.serialize(old.oser), std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	packet.cause = newHorizonsArmorer::DefiantDenialCause::INNATE_BLOCK;
	EXPECT_NO_THROW(packet.serialize(old.oser));
}

TEST(NewHorizonsParalyzingGazeMarker, ActiveStackBattleAndStartRejectOldWritersBeforeAnyEnclosingPrefix)
{
	auto stack = std::make_unique<CStack>();
	stack->addNewBonus(std::make_shared<Bonus>(newHorizonsParalyzingGaze::marker()));
	CMemorySerializer stackWire;
	stackWire.oser.version = ESerializationVersion::NEW_HORIZONS_SERPENTS_EMBRACE;
	EXPECT_THROW(stack->serialize(stackWire.oser), std::runtime_error);
	EXPECT_TRUE(stackWire.extractBuffer().empty());
	BattleStart start;
	start.info = std::make_unique<BattleInfo>(nullptr);
	start.info->stacks.push_back(std::move(stack));
	CMemorySerializer battleWire, startWire;
	battleWire.oser.version = startWire.oser.version = ESerializationVersion::NEW_HORIZONS_SERPENTS_EMBRACE;
	EXPECT_THROW(start.info->serialize(battleWire.oser), std::runtime_error);
	EXPECT_TRUE(battleWire.extractBuffer().empty());
	EXPECT_THROW(start.serialize(startWire.oser), std::runtime_error);
	EXPECT_TRUE(startWire.extractBuffer().empty());
}
