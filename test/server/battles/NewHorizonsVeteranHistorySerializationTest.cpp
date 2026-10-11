/*
 * NewHorizonsVeteranHistorySerializationTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"

#include "../../../lib/CSkillHandler.h"
#include "../../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../../lib/serializer/CMemorySerializer.h"

namespace
{
constexpr auto snapshotVersion = ESerializationVersion::NEW_HORIZONS_COMBAT_ACTIVATION_SNAPSHOT;
constexpr auto previousVersion = static_cast<ESerializationVersion>(static_cast<int>(snapshotVersion) - 1);

class NewHorizonsVeteranHistorySerializationTest : public HeroCommandFixture
{
protected:
	CStack * source = nullptr;
	CStack * recipient = nullptr;

	void prepare(bool veteran = false)
	{
		startGame();
		if(veteran)
		{
			const SecondarySkill armorer(SecondarySkill::decode("new-horizons:armorer"));
			ASSERT_TRUE(armorer.hasValue());
			defenderSideHero->setSecSkillLevel(armorer, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			defenderSideHero->applyPerkSelection({"new-horizons:armorer", "new-horizons:armorer.pavise"});
			defenderSideHero->setSecSkillLevel(armorer, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
			defenderSideHero->applyPerkSelection({"new-horizons:armorer", "new-horizons:armorer.veteran"});
			ASSERT_TRUE(defenderSideHero->hasActivePerk("new-horizons:armorer", "new-horizons:armorer.veteran"));
		}
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = battle()->getBattleID();
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		source = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(92), 20);
		recipient = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(98), 10);
		// A slower third unit keeps the hit interval open before the recipient's
		// next genuine activation; do not suppress Veteran's start-of-turn producer.
		ASSERT_NE(addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(110), 10), nullptr);
		ASSERT_NE(source, nullptr);
		ASSERT_NE(recipient, nullptr);
		beginCombat();
	}

	void shoot()
	{
		ASSERT_EQ(battle()->battleActiveUnit(), recipient);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(), PlayerColor(1),
			BattleAction::makeDefend(recipient)));
		BattleSetActiveStack active;
		active.battleID = battle()->getBattleID();
		active.stack = source->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(), PlayerColor(0),
			BattleAction::makeShotAttack(source, recipient)));
		ASSERT_TRUE(recipient->alive());
		ASSERT_GT(recipient->veteranPhysicalDamageSinceActivation, 0);
	}

	std::unique_ptr<CStack> roundtrip(CStack & stack, ESerializationVersion version)
	{
		CMemorySerializer memory;
		memory.oser.version = version;
		memory.iser.version = version;
		memory.iser.cb = gameState().get();
		memory.oser & stack;
		auto restored = std::make_unique<CStack>();
		memory.iser & *restored;
		return restored;
	}
};
}

TEST_F(NewHorizonsVeteranHistorySerializationTest, AuthenticShotHistorySurvivesStackRebindAndConsumesOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	ASSERT_NO_FATAL_FAILURE(shoot());
	const auto history = recipient->veteranPhysicalDamageSinceActivation;
	const auto originalState = recipient->acquireState()->save();
	auto restored = roundtrip(*recipient, snapshotVersion);
	EXPECT_EQ(restored->veteranPhysicalDamageSinceActivation, history);
	ASSERT_NO_THROW(restored->localInit(battle()));
	EXPECT_EQ(restored->veteranPhysicalDamageSinceActivation, history);
	EXPECT_EQ(recipient->veteranPhysicalDamageSinceActivation, history);

	// The binary descriptor does not carry ordinary health. Replay the existing
	// authoritative JSON health carrier explicitly; do not claim full HP persistence.
	auto activationState = restored->acquireState();
	activationState->load(originalState);
	ASSERT_EQ(activationState->veteranPhysicalDamageSinceActivation, history);
	const auto woundedHealth = activationState->getAvailableHealth();
	const auto recovery = (history / 100) * 15 + (history % 100) * 15 / 100;
	ASSERT_GT(recovery, 0);
	EXPECT_EQ(newHorizonsCombatSkills::applyVeteran(activationState.get(), defenderSideHero), recovery);
	EXPECT_EQ(activationState->getAvailableHealth(), woundedHealth + recovery);
	EXPECT_EQ(activationState->veteranPhysicalDamageSinceActivation, 0);
	EXPECT_EQ(newHorizonsCombatSkills::applyVeteran(activationState.get(), defenderSideHero), 0);
	EXPECT_EQ(recipient->veteranPhysicalDamageSinceActivation, history);
}

TEST_F(NewHorizonsVeteranHistorySerializationTest, HistoryWithoutVeteranStillPersistsAcrossRoundBoundary)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_FALSE(defenderSideHero->hasActivePerk("new-horizons:armorer", "new-horizons:armorer.veteran"));
	ASSERT_NO_FATAL_FAILURE(shoot());
	const auto history = recipient->veteranPhysicalDamageSinceActivation;
	auto restored = roundtrip(*recipient, snapshotVersion);
	ASSERT_NO_THROW(restored->localInit(battle()));
	restored->afterNewRound(false);
	EXPECT_EQ(restored->veteranPhysicalDamageSinceActivation, history);
	auto state = restored->acquireState();
	EXPECT_EQ(newHorizonsCombatSkills::applyVeteran(state.get(), defenderSideHero), 0);
	EXPECT_EQ(state->veteranPhysicalDamageSinceActivation, 0);
}

TEST_F(NewHorizonsVeteranHistorySerializationTest, OldWriterRejectsAuthenticHistoryBeforePrefixAndOldReaderDefaultsZero)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(shoot());
	CMemorySerializer rejected;
	rejected.oser.version = previousVersion;
	EXPECT_THROW(rejected.oser & *recipient, std::runtime_error);
	EXPECT_TRUE(rejected.extractBuffer().empty());

	// The attacker received no damage; this is a genuine old-compatible descriptor.
	ASSERT_EQ(source->veteranPhysicalDamageSinceActivation, 0);
	CMemorySerializer old;
	old.oser.version = previousVersion;
	old.iser.version = previousVersion;
	old.iser.cb = gameState().get();
	ASSERT_NO_THROW(old.oser & *source);
	CStack restored;
	restored.veteranPhysicalDamageSinceActivation = 37;
	ASSERT_NO_THROW(old.iser & restored);
	EXPECT_EQ(restored.veteranPhysicalDamageSinceActivation, 0);
	ASSERT_NO_THROW(restored.localInit(battle()));
	EXPECT_EQ(restored.veteranPhysicalDamageSinceActivation, 0);
}

TEST_F(NewHorizonsVeteranHistorySerializationTest, NegativeHistoryRejectsWriterBeforePrefixAndMalformedCurrentReader)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	CStack invalid;
	invalid.veteranPhysicalDamageSinceActivation = -1;
	CMemorySerializer rejected;
	rejected.oser.version = snapshotVersion;
	EXPECT_THROW(rejected.oser & invalid, std::runtime_error);
	EXPECT_TRUE(rejected.extractBuffer().empty());

	CMemorySerializer valid;
	valid.oser.version = snapshotVersion;
	ASSERT_NO_THROW(valid.oser & *source);
	auto bytes = valid.extractBuffer();
	CMemorySerializer zero;
	CMemorySerializer negative;
	const int64_t zeroValue = 0;
	const int64_t negativeValue = -1;
	zero.oser & zeroValue;
	negative.oser & negativeValue;
	const auto zeroBytes = zero.extractBuffer();
	const auto negativeBytes = negative.extractBuffer();
	ASSERT_GE(bytes.size(), zeroBytes.size());
	ASSERT_TRUE(std::equal(zeroBytes.begin(), zeroBytes.end(), bytes.end() - zeroBytes.size()));
	bytes.resize(bytes.size() - zeroBytes.size());
	bytes.insert(bytes.end(), negativeBytes.begin(), negativeBytes.end());
	CMemorySerializer malformed(std::move(bytes));
	malformed.iser.version = snapshotVersion;
	malformed.iser.cb = gameState().get();
	CStack restored;
	EXPECT_THROW(malformed.iser & restored, std::runtime_error);
}

TEST_F(NewHorizonsVeteranHistorySerializationTest, MaximumSignedHistoryRoundTripsWithoutNarrowing)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	source->veteranPhysicalDamageSinceActivation = std::numeric_limits<int64_t>::max();
	auto restored = roundtrip(*source, snapshotVersion);
	EXPECT_EQ(restored->veteranPhysicalDamageSinceActivation, std::numeric_limits<int64_t>::max());
	ASSERT_NO_THROW(restored->localInit(battle()));
	EXPECT_EQ(restored->veteranPhysicalDamageSinceActivation, std::numeric_limits<int64_t>::max());
}
