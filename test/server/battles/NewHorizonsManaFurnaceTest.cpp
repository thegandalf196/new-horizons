/*
 * NewHorizonsManaFurnaceTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "BattleStartSnapshotFixture.h"
#include "../../../lib/battle/BattleLayout.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/gameState/NewHorizonsBattleSpellPoints.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/modding/IdentifierStorage.h"
#include "../../../lib/modding/ModScope.h"
#include "../../../lib/networkPacks/PacksForClient.h"
#include <limits>
namespace
{
class NewHorizonsManaFurnaceTest : public HeroCommandFixture
{
protected:
	bool captured = true;
	bool defenderAyden = false;
	bool legacyMagic = false;
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires curated New Horizons content";
	}
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		rules.setOverrideFlag(true);
		auto & table = rules["additionalSpecialties"];
		table["version"].Integer() = 1;
		table.Struct().erase("manaFurnace");
		if(captured)
		{
			table["manaFurnace"]["restorePointsPerLevel"].Integer() = 2;
			table["manaFurnace"]["maximumRestorePoints"].Integer() = 40;
		}
		// A legacy magic fixture cannot request modern starting-book migration.
		if(legacyMagic)
			rules["startingSkills"].Struct().erase("startingBookReplacements");
		ASSERT_NO_THROW(newHorizonsHeroes::validateHeroRules(rules, true));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			legacyMagic ? JsonNode() : JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		for(const auto & object : loaded->objects)
			if(auto * hero = dynamic_cast<CGHeroInstance *>(object.get()); hero && (hero->getOwner() == PlayerColor(0)
				|| (defenderAyden && hero->getOwner() == PlayerColor(1))))
				hero->setHeroType(HeroTypeID(HeroTypeID::decode("core:ayden")));
	}
	void prepare(int normal = 5, int buffer = 19, bool intelligence = true)
	{
		startGame();
		attackerSideHero->level = 20;
		ASSERT_TRUE(attackerSideHero->getPerkState().hasSelection("new-horizons:wisdom", "new-horizons:wisdom.intelligence"));
		SetSecSkill rank;
		rank.id = attackerSideHero->id;
		rank.which = SecondarySkill(SecondarySkill::decode("new-horizons:wisdom"));
		rank.val = intelligence ? MasteryLevel::BASIC : MasteryLevel::NONE;
		rank.mode = ChangeValueMode::ABSOLUTE;
		gameState()->apply(rank);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		attackerSideHero->initializeSpellPoints(normal, buffer);
		if(!legacyMagic)
		{
			ASSERT_EQ(attackerSideHero->manaLimit(), intelligence ? 130 : 100);
		}
		else
		{
			ASSERT_GE(attackerSideHero->manaLimit(), 100);
		}
		ASSERT_EQ(attackerSideHero->getNormalSpellPoints(), normal);
		ASSERT_EQ(attackerSideHero->getBufferSpellPoints(), buffer);
	}
	std::unique_ptr<BattleInfo> descriptor()
	{
		const auto layout = BattleLayout::createDefaultLayout(*gameState(), attackerSideHero, defenderSideHero);
		const auto terrain = gameState()->getTile({5, 5, 0})->getTerrainID();
		const auto id = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "battlefield", std::string("core:sand_shore"));
		if(!id)
			throw std::runtime_error("Missing ordinary fixture battlefield");
		BattleSideArray<const CArmedInstance *> armies{attackerSideHero, defenderSideHero};
		BattleSideArray<const CGHeroInstance *> heroes{attackerSideHero, defenderSideHero};
		return BattleInfo::setupBattle(gameState().get(), {5, 5, 0}, terrain, BattleField(*id), armies, heroes, layout, nullptr);
	}
	void acceptEntry()
	{
		BattleStart start;
		start.battleID = gameState()->nextBattleID;
		start.info = descriptor();
		gameHandler->sendAndApply(start);
	}
	void pools(const CGHeroInstance * hero, int normal, int buffer, int maximum = 130)
	{
		EXPECT_EQ(hero->getNormalSpellPoints(), normal);
		EXPECT_EQ(hero->getBufferSpellPoints(), buffer);
		EXPECT_EQ(hero->manaLimit(), maximum);
	}
};
}
TEST_F(NewHorizonsManaFurnaceTest, AcceptedEntryRestoresOnlyNormalAndRetainsOriginalCancellationSnapshot)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	startBattle();
	pools(attackerSideHero, 45, 19);
	const auto & side = battle()->getSide(BattleSide::ATTACKER);
	EXPECT_EQ(side.initialNormalSpellPoints, 5);
	EXPECT_EQ(side.initialBufferSpellPoints, 19);
	EXPECT_EQ(side.temporaryBufferRemaining, 0);
	EXPECT_EQ(side.manaFurnaceRecoveredNormal, 40);
}
TEST_F(NewHorizonsManaFurnaceTest, FullAndPartialCapacityUseOnlyMissingNormal)
{
	ASSERT_NO_FATAL_FAILURE(prepare(120, 19));
	startBattle();
	pools(attackerSideHero, 130, 19);
	BattleCancelled cancel;
	cancel.battleID = battle()->getBattleID();
	gameState()->apply(cancel);
	attackerSideHero->initializeSpellPoints(130, 19);
	ASSERT_NO_FATAL_FAILURE(acceptEntry());
	pools(attackerSideHero, 130, 19);
}
TEST_F(NewHorizonsManaFurnaceTest, EmptyNormalAndFirstLevelUseConfiguredPerLevelBudget)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0, 19));
	attackerSideHero->level = 1;
	startBattle();
	pools(attackerSideHero, 2, 19);
}
TEST_F(NewHorizonsManaFurnaceTest, InactiveIntelligenceDoesNotRecoverOrChangeCapacity)
{
	ASSERT_NO_FATAL_FAILURE(prepare(5, 19, false));
	startBattle();
	pools(attackerSideHero, 5, 19, 100);
}
TEST_F(NewHorizonsManaFurnaceTest, OtherIdentityKeepsItsPoolsEvenWithIntelligenceAndCapturedPolicy)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:xyron")));
	startBattle();
	pools(attackerSideHero, 5, 19);
}
TEST_F(NewHorizonsManaFurnaceTest, AbsentHistoricalRowStaysAbsentAfterEntryAndWorldLoad)
{
	captured = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	startBattle();
	pools(attackerSideHero, 5, 19);
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * loaded = restored.getHero(attackerSideHero->id);
	ASSERT_NE(loaded, nullptr);
	EXPECT_FALSE(newHorizonsHeroes::manaFurnaceRules(loaded->getPrimaryGrowthRules()));
	pools(loaded, 5, 19);
}
TEST_F(NewHorizonsManaFurnaceTest, PureSetupAndWorldPoolRoundtripNeverReapplyRecovery)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto setup = descriptor();
	ASSERT_NE(setup, nullptr);
	pools(attackerSideHero, 5, 19);
	setup.reset();
	startBattle();
	pools(attackerSideHero, 45, 19);
	gameHandler->spendSpellPoints(attackerSideHero->id, 25);
	pools(attackerSideHero, 39, 0);
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * loaded = restored.getHero(attackerSideHero->id);
	ASSERT_NE(loaded, nullptr);
	pools(loaded, 39, 0);
	EXPECT_TRUE(restored.currentBattles.empty());
	// Ordinary saves preserve pools, not ongoing battle descriptors.
	EXPECT_EQ(loaded->battle, nullptr);
}
TEST_F(NewHorizonsManaFurnaceTest, ActualBattleRestartRollsBackSpendingAndReappliesOneRecovery)
{
	ASSERT_NO_FATAL_FAILURE(prepare(5, 2));
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	pools(attackerSideHero, 45, 2);
	const auto oldID = battle()->getBattleID();
	gameHandler->spendSpellPoints(attackerSideHero->id, 10);
	pools(attackerSideHero, 37, 0);
	const auto layout = battle()->getLayout();
	gameHandler->battles->restartBattle(oldID, attackerSideHero, defenderSideHero, {5, 5, 0},
		attackerSideHero, defenderSideHero, layout, nullptr);
	pools(attackerSideHero, 45, 2);
	EXPECT_NE(battle()->getBattleID(), oldID);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).initialNormalSpellPoints, 5);
}
TEST_F(NewHorizonsManaFurnaceTest, ExistingPenaltyRunsBeforeRecoveryAndCancellationRestoresBothOriginalPools)
{
	ASSERT_NO_FATAL_FAILURE(prepare(5, 2));
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::COMBAT_MANA_BONUS, BonusSource::OTHER, -10, BonusSourceID()));
	startBattle();
	pools(attackerSideHero, 40, 0);
	BattleCancelled cancel;
	cancel.battleID = battle()->getBattleID();
	gameState()->apply(cancel);
	pools(attackerSideHero, 5, 2);
}
TEST_F(NewHorizonsManaFurnaceTest, CompletedBattleRetainsRecoveryAndLaterRealEntryGetsFreshBudget)
{
	ASSERT_NO_FATAL_FAILURE(prepare(5, 2));
	startBattle();
	gameHandler->spendSpellPoints(attackerSideHero->id, 10);
	BattleResultsApplied complete;
	complete.battleID = battle()->getBattleID();
	gameState()->apply(complete);
	pools(attackerSideHero, 37, 0);
	BattleEnded ended;
	ended.battleID = complete.battleID;
	gameState()->apply(ended);
	EXPECT_EQ(attackerSideHero->battle, nullptr);
	EXPECT_EQ(defenderSideHero->battle, nullptr);
	EXPECT_TRUE(gameState()->currentBattles.empty());
	ASSERT_NO_FATAL_FAILURE(acceptEntry());
	pools(attackerSideHero, 77, 0);
}
TEST_F(NewHorizonsManaFurnaceTest, DuplicateActiveArmyAndStaleEntryIdentityRejectWithoutAnotherGrant)
{
	ASSERT_NO_FATAL_FAILURE(prepare(5, 2));
	startBattle();
	auto original = battleStartFixture::snapshot(*battle(), gameState().get());
	auto stale = battleStartFixture::snapshot(*battle(), gameState().get());
	original->getSide(BattleSide::ATTACKER).manaFurnaceRecoveredNormal = 0;
	stale->getSide(BattleSide::ATTACKER).manaFurnaceRecoveredNormal = 0;
	BattleStart duplicate;
	duplicate.battleID = gameState()->nextBattleID;
	duplicate.info = std::move(original);
	EXPECT_THROW(gameHandler->sendAndApply(duplicate), std::runtime_error);
	pools(attackerSideHero, 45, 2);
	EXPECT_EQ(gameState()->currentBattles.size(), 1u);
	BattleCancelled cancel;
	cancel.battleID = battle()->getBattleID();
	gameState()->apply(cancel);
	BattleStart old;
	old.battleID = cancel.battleID;
	old.info = std::move(stale);
	EXPECT_THROW(gameHandler->sendAndApply(old), std::runtime_error);
	pools(attackerSideHero, 5, 2);
	EXPECT_TRUE(gameState()->currentBattles.empty());
}
TEST_F(NewHorizonsManaFurnaceTest, InvalidSecondSideSnapshotOrPolicyRejectsBeforeEitherHeroMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(5, 2));
	const auto before = gameState()->saveToMemory();
	startBattle();
	CGameState replica;
	replica.preInit(LIBRARY);
	replica.loadFromMemory(before);
	auto * attacker = replica.getHero(attackerSideHero->id);
	auto * defender = replica.getHero(defenderSideHero->id);
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(defender, nullptr);
	const auto defenderNormal = defender->getNormalSpellPoints(), defenderBuffer = defender->getBufferSpellPoints();
	BattleStart badPool;
	badPool.battleID = BattleID(0);
	badPool.info = battleStartFixture::snapshot(*battle(), &replica);
	badPool.info->getSide(BattleSide::ATTACKER).manaFurnaceRecoveredNormal = 0;
	badPool.info->getSide(BattleSide::DEFENDER).initialNormalSpellPoints = -1;
	EXPECT_THROW(replica.apply(badPool), std::runtime_error);
	pools(attacker, 5, 2);
	EXPECT_EQ(attacker->battle, nullptr);
	EXPECT_EQ(defender->battle, nullptr);
	// Deliberately corrupt the in-process second hero's captured policy; no decoder validates this packet.
	auto & rules = const_cast<JsonNode &>(defender->getPrimaryGrowthRules());
	const auto originalRules = rules;
	rules["additionalSpecialties"]["manaFurnace"]["maximumRestorePoints"].Integer() = -1;
	BattleStart badPolicy;
	badPolicy.battleID = BattleID(0);
	badPolicy.info = battleStartFixture::snapshot(*battle(), &replica);
	badPolicy.info->getSide(BattleSide::ATTACKER).manaFurnaceRecoveredNormal = 0;
	EXPECT_THROW(replica.apply(badPolicy), std::runtime_error);
	rules = originalRules;
	pools(attacker, 5, 2);
	EXPECT_EQ(defender->getNormalSpellPoints(), defenderNormal);
	EXPECT_EQ(defender->getBufferSpellPoints(), defenderBuffer);
	EXPECT_TRUE(replica.currentBattles.empty());
	EXPECT_EQ(replica.nextBattleID, BattleID(0));
	EXPECT_EQ(attacker->battle, nullptr);
	EXPECT_EQ(defender->battle, nullptr);
}

TEST_F(NewHorizonsManaFurnaceTest, BothEligibleHeroesRecoverIndependently)
{
	defenderAyden = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(defenderSideHero->getPerkState().hasSelection("new-horizons:wisdom", "new-horizons:wisdom.intelligence"));
	SetSecSkill rank;
	rank.id = defenderSideHero->id;
	rank.which = SecondarySkill(SecondarySkill::decode("new-horizons:wisdom"));
	rank.val = MasteryLevel::BASIC;
	rank.mode = ChangeValueMode::ABSOLUTE;
	gameState()->apply(rank);
	defenderSideHero->level = 20;
	defenderSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	defenderSideHero->initializeSpellPoints(9, 7);
	startBattle();
	pools(attackerSideHero, 45, 19);
	pools(defenderSideHero, 49, 7);
}

TEST_F(NewHorizonsManaFurnaceTest, PureQueryBoundsZeroAndHugeLevelsWithoutChangingPools)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->level = 0;
	EXPECT_EQ(newHorizonsHeroes::manaFurnaceRestoration(attackerSideHero, {2, 40}), 0);
	attackerSideHero->level = std::numeric_limits<ui32>::max();
	EXPECT_EQ(newHorizonsHeroes::manaFurnaceRestoration(attackerSideHero, {1000, 1000000}), 125);
	pools(attackerSideHero, 5, 19);
	attackerSideHero->level = 20;
}
TEST_F(NewHorizonsManaFurnaceTest, CapacityIncreaseCreatesEmptySpaceAndEntryAddsOnlyItsOwnBudget)
{
	ASSERT_NO_FATAL_FAILURE(prepare(125, 19));
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 200, ChangeValueMode::ABSOLUTE);
	pools(attackerSideHero, 125, 19, 260);
	startBattle();
	pools(attackerSideHero, 165, 19, 260);
}

TEST_F(NewHorizonsManaFurnaceTest, LegacyCompletionRetainsOnlyActualRecoveryNotTemporaryCombatMana)
{
	legacyMagic = true;
	ASSERT_NO_FATAL_FAILURE(prepare(5, 0));
	const auto maximum = attackerSideHero->manaLimit();
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::COMBAT_MANA_BONUS, BonusSource::OTHER, 20, BonusSourceID()));
	startBattle();
	pools(attackerSideHero, 65, 0, maximum);
	const auto & side = battle()->getSide(BattleSide::ATTACKER);
	EXPECT_EQ(side.initialMana, 5);
	EXPECT_EQ(side.manaFurnaceRecoveredNormal, 40);
	gameHandler->spendSpellPoints(attackerSideHero->id, 30);
	pools(attackerSideHero, 35, 0, maximum);
	BattleResultsApplied complete;
	complete.battleID = battle()->getBattleID();
	gameState()->apply(complete);
	pools(attackerSideHero, 35, 0, maximum);
}
TEST_F(NewHorizonsManaFurnaceTest, LegacyCapacityLimitedActualGrantSurvivesWithoutLeakingTemporaryBonus)
{
	legacyMagic = true;
	ASSERT_NO_FATAL_FAILURE(prepare(5, 0));
	const auto maximum = attackerSideHero->manaLimit();
	attackerSideHero->setNormalSpellPoints(maximum - 30);
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::COMBAT_MANA_BONUS, BonusSource::OTHER, 20, BonusSourceID()));
	startBattle();
	pools(attackerSideHero, maximum, 0, maximum);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).manaFurnaceRecoveredNormal, 10);
	BattleResultsApplied complete;
	complete.battleID = battle()->getBattleID();
	gameState()->apply(complete);
	pools(attackerSideHero, maximum - 20, 0, maximum);
}
TEST_F(NewHorizonsManaFurnaceTest, LegacyCancellationRestoresOriginalBeforePenaltyAndRecovery)
{
	legacyMagic = true;
	ASSERT_NO_FATAL_FAILURE(prepare(5, 0));
	const auto maximum = attackerSideHero->manaLimit();
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::COMBAT_MANA_BONUS, BonusSource::OTHER, -10, BonusSourceID()));
	startBattle();
	pools(attackerSideHero, 40, 0, maximum);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).manaFurnaceRecoveredNormal, 40);
	BattleCancelled cancel;
	cancel.battleID = battle()->getBattleID();
	gameState()->apply(cancel);
	pools(attackerSideHero, 5, 0, maximum);
}
TEST_F(NewHorizonsManaFurnaceTest, DescriptorCopyRetainsEntryProvenanceButCannotDispatchAnotherEntry)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	startBattle();
	auto copy = battleStartFixture::snapshot(*battle(), gameState().get());
	EXPECT_EQ(copy->getSide(BattleSide::ATTACKER).manaFurnaceRecoveredNormal, 40);
	EXPECT_EQ(copy->getSide(BattleSide::ATTACKER).initialNormalSpellPoints, 5);
	copy->getSide(BattleSide::ATTACKER).manaFurnaceRecoveredNormal = 7;
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).manaFurnaceRecoveredNormal, 40);
	EXPECT_THROW(newHorizonsHeroes::validateBattleSpellPointEntry(*gameState(), *copy), std::runtime_error);
	pools(attackerSideHero, 45, 19);
}

TEST_F(NewHorizonsManaFurnaceTest, DuplicateHeroIdentityRejectsPlannerAndAdmissionBeforeMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(5, 2));
	const auto day = gameState()->day;
	const auto next = gameState()->nextBattleID;
	const auto defenderNormal = defenderSideHero->getNormalSpellPoints();
	const auto defenderBuffer = defenderSideHero->getBufferSpellPoints();
	auto malformed = descriptor();
	malformed->getSide(BattleSide::DEFENDER).heroID = attackerSideHero->id;
	EXPECT_THROW(newHorizonsHeroes::validateBattleSpellPointEntry(*gameState(), *malformed), std::runtime_error);
	BattleStart rejected;
	rejected.battleID = next;
	rejected.info = std::move(malformed);
	EXPECT_THROW(gameHandler->sendAndApply(rejected), std::runtime_error);
	pools(attackerSideHero, 5, 2);
	EXPECT_EQ(defenderSideHero->getNormalSpellPoints(), defenderNormal);
	EXPECT_EQ(defenderSideHero->getBufferSpellPoints(), defenderBuffer);
	EXPECT_EQ(attackerSideHero->battle, nullptr);
	EXPECT_EQ(defenderSideHero->battle, nullptr);
	EXPECT_TRUE(gameState()->currentBattles.empty());
	EXPECT_EQ(gameState()->nextBattleID, next);
	EXPECT_EQ(gameState()->day, day);
}
