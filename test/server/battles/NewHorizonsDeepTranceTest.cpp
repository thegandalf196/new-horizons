/*
	* NewHorizonsDeepTranceTest.cpp, part of VCMI / New Horizons
	* License: GNU General Public License v2.0 or later; see license.txt
	*/
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "BattleStartSnapshotFixture.h"
#include "../../../lib/battle/BattleLayout.h"
#include "../../../lib/entities/hero/NewHorizonsRecoverySpecialties.h"
#include "../../../lib/gameState/NewHorizonsBattleSpellPoints.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/modding/IdentifierStorage.h"
#include "../../../lib/modding/ModScope.h"
#include "../../../lib/networkPacks/PacksForClient.h"
#include "../../../lib/networkPacks/PacksForLobby.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/serializer/CMemorySerializer.h"
namespace
{
class NewHorizonsDeepTranceTest : public HeroCommandFixture
{
protected:
	bool captured = true;
	bool historicalCodecCapture = false;
	bool legacyMagic = false;
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires curated New Horizons content";
	}
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		rules.setOverrideFlag(true);
		// Only the explicit old positive codec fixture predates later families.
		if(historicalCodecCapture)
			for(const auto key : {"learningExchangeSpecialties", "swampSeerSpecialty", "reclamationSpecialty"})
				rules.Struct().erase(key);
		rules.Struct().erase("recoverySpecialties");
		if(captured)
		{
			auto & table = rules["recoverySpecialties"];
			table["version"].Integer() = 1;
			auto & row = table["deepTrance"];
			row["dailyPercentPerLevel"].Integer() = 5;
			row["dailyMaximumPercent"].Integer() = 100;
			row["firstCombatPercent"].Integer() = 50;
		}
		if(legacyMagic)
			rules["startingSkills"].Struct().erase("startingBookReplacements");
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			legacyMagic ? JsonNode() : JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		for(const auto & object : map->objects)
			if(auto * hero = dynamic_cast<CGHeroInstance *>(object.get()); hero && hero->getOwner() == PlayerColor(0))
				hero->setHeroType(HeroTypeID(HeroTypeID::decode("core:rosic")));
	}
	void prepare(bool active = true)
	{
		startGame();
		ASSERT_TRUE(attackerSideHero->getPerkState().hasSelection("new-horizons:wisdom", "new-horizons:wisdom.mysticism"));
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		SetSecSkill rank;
		rank.id = attackerSideHero->id;
		rank.which = SecondarySkill(SecondarySkill::decode("new-horizons:wisdom"));
		rank.val = active ? MasteryLevel::BASIC : MasteryLevel::NONE;
		rank.mode = ChangeValueMode::ABSOLUTE;
		gameState()->apply(rank);
		attackerSideHero->initializeSpellPoints(5, 19);
	}
	void acceptEntry()
	{
		const auto layout = BattleLayout::createDefaultLayout(*gameState(), attackerSideHero, defenderSideHero);
		const auto terrain = gameState()->getTile({5, 5, 0})->getTerrainID();
		const auto id = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "battlefield", std::string("core:sand_shore"));
		ASSERT_TRUE(id);
		BattleSideArray<const CArmedInstance *> armies{attackerSideHero, defenderSideHero};
		BattleSideArray<const CGHeroInstance *> heroes{attackerSideHero, defenderSideHero};
		BattleStart start;
		start.battleID = gameState()->nextBattleID;
		start.info = BattleInfo::setupBattle(gameState().get(), {5, 5, 0}, terrain, BattleField(*id), armies, heroes, layout, nullptr);
		gameHandler->sendAndApply(start);
	}
	void complete()
	{
		BattleResultsApplied done;
		done.battleID = battle()->getBattleID();
		gameState()->apply(done);
		BattleEnded ended;
		ended.battleID = done.battleID;
		gameState()->apply(ended);
		ASSERT_TRUE(gameState()->currentBattles.empty());
		ASSERT_EQ(attackerSideHero->battle, nullptr);
		ASSERT_EQ(defenderSideHero->battle, nullptr);
	}
};
}
TEST_F(NewHorizonsDeepTranceTest, FirstAcceptedFightRecoversOriginalMysticismAndSecondDoesNot)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto grant = newHorizonsHeroes::firstCombatMysticismRecovery(*attackerSideHero);
	ASSERT_GT(grant, 0);
	ASSERT_NO_FATAL_FAILURE(acceptEntry());
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 5 + grant);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 19);
	EXPECT_EQ(attackerSideHero->getNewHorizonsLastAcceptedCombatDay(), gameState()->day);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).deepTranceRecoveredNormal, grant);
	ASSERT_NO_FATAL_FAILURE(complete());
	attackerSideHero->initializeSpellPoints(5, 19);
	ASSERT_NO_FATAL_FAILURE(acceptEntry());
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 5);
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).deepTranceConsumed);
}
TEST_F(NewHorizonsDeepTranceTest, FullFirstFightConsumesAndNextDayAllowsRecovery)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->initializeSpellPoints(attackerSideHero->manaLimit(), 19);
	ASSERT_NO_FATAL_FAILURE(acceptEntry());
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).deepTranceConsumed);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).deepTranceRecoveredNormal, 0);
	ASSERT_NO_FATAL_FAILURE(complete());
	attackerSideHero->initializeSpellPoints(5, 19);
	ASSERT_NO_FATAL_FAILURE(acceptEntry());
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 5);
	ASSERT_NO_FATAL_FAILURE(complete());
	++gameState()->day;
	ASSERT_NO_FATAL_FAILURE(acceptEntry());
	EXPECT_GT(attackerSideHero->getNormalSpellPoints(), 5);
}
TEST_F(NewHorizonsDeepTranceTest, InactiveFirstFightConsumesWithoutInventingRecovery)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	ASSERT_NO_FATAL_FAILURE(acceptEntry());
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).deepTranceConsumed);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 5);
	EXPECT_EQ(attackerSideHero->getNewHorizonsLastAcceptedCombatDay(), gameState()->day);
	ASSERT_NO_FATAL_FAILURE(complete());
	SetSecSkill rank;
	rank.id = attackerSideHero->id;
	rank.which = SecondarySkill(SecondarySkill::decode("new-horizons:wisdom"));
	rank.val = MasteryLevel::BASIC;
	rank.mode = ChangeValueMode::ABSOLUTE;
	gameState()->apply(rank);
	ASSERT_NO_FATAL_FAILURE(acceptEntry());
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 5);
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).deepTranceConsumed);
}
TEST_F(NewHorizonsDeepTranceTest, CancellationAndRestartRestoreOriginalDayAndPools)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(acceptEntry());
	const auto recovered = attackerSideHero->getNormalSpellPoints();
	const auto oldID = battle()->getBattleID();
	const auto layout = battle()->getLayout();
	gameHandler->battles->restartBattle(oldID, attackerSideHero, defenderSideHero, {5, 5, 0},
		attackerSideHero, defenderSideHero, layout, nullptr);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), recovered);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).initialAcceptedCombatDay, -1);
	BattleCancelled cancel;
	cancel.battleID = battle()->getBattleID();
	gameState()->apply(cancel);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 5);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 19);
	EXPECT_EQ(attackerSideHero->getNewHorizonsLastAcceptedCombatDay(), -1);
}
TEST_F(NewHorizonsDeepTranceTest, AcceptedWorldSaveRetainsDayWithoutResumingBattleOrRegranting)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(acceptEntry());
	const auto normal = attackerSideHero->getNormalSpellPoints();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * loaded = restored.getHero(attackerSideHero->id);
	ASSERT_NE(loaded, nullptr);
	EXPECT_EQ(loaded->getNewHorizonsLastAcceptedCombatDay(), gameState()->day);
	EXPECT_EQ(loaded->getNormalSpellPoints(), normal);
	EXPECT_TRUE(restored.currentBattles.empty());
	EXPECT_EQ(loaded->battle, nullptr);
}
TEST_F(NewHorizonsDeepTranceTest, MalformedSecondSideRejectsBeforeReceiptOrPoolsChange)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto before = gameState()->saveToMemory();
	ASSERT_NO_FATAL_FAILURE(acceptEntry());
	CGameState replica;
	replica.preInit(LIBRARY);
	replica.loadFromMemory(before);
	BattleStart bad;
	bad.battleID = BattleID(0);
	bad.info = battleStartFixture::snapshot(*battle(), &replica);
	auto & first = bad.info->getSide(BattleSide::ATTACKER);
	first.deepTranceConsumed = false;
	first.initialAcceptedCombatDay = -1;
	first.deepTranceRecoveredNormal = 0;
	bad.info->getSide(BattleSide::DEFENDER).initialNormalSpellPoints = -1;
	EXPECT_THROW(replica.apply(bad), std::runtime_error);
	const auto * hero = replica.getHero(attackerSideHero->id);
	ASSERT_NE(hero, nullptr);
	EXPECT_EQ(hero->getNormalSpellPoints(), 5);
	EXPECT_EQ(hero->getNewHorizonsLastAcceptedCombatDay(), -1);
}
TEST_F(NewHorizonsDeepTranceTest, LegacyCompletionRetainsRecoveryWithoutTemporaryMana)
{
	legacyMagic = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto grant = newHorizonsHeroes::firstCombatMysticismRecovery(*attackerSideHero);
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::COMBAT_MANA_BONUS, BonusSource::OTHER, 20, BonusSourceID()));
	ASSERT_NO_FATAL_FAILURE(acceptEntry());
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).deepTranceRecoveredNormal, grant);
	ASSERT_NO_FATAL_FAILURE(complete());
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 5 + grant);
}
TEST_F(NewHorizonsDeepTranceTest, HistoricalAbsentPolicyDoesNotConsumeFirstFight)
{
	captured = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(acceptEntry());
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 5);
	EXPECT_EQ(attackerSideHero->getNewHorizonsLastAcceptedCombatDay(), -1);
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).deepTranceConsumed);
}

TEST_F(NewHorizonsDeepTranceTest, DuplicateHeroIdentityRejectsWithoutSpendingDayOrChangingPools)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto day = gameState()->day;
	const auto next = gameState()->nextBattleID;
	const auto beforeAttacker = attackerSideHero->getNewHorizonsLastAcceptedCombatDay();
	const auto beforeDefender = defenderSideHero->getNewHorizonsLastAcceptedCombatDay();
	const auto defenderNormal = defenderSideHero->getNormalSpellPoints();
	const auto defenderBuffer = defenderSideHero->getBufferSpellPoints();
	const auto layout = BattleLayout::createDefaultLayout(*gameState(), attackerSideHero, defenderSideHero);
	const auto terrain = gameState()->getTile({5, 5, 0})->getTerrainID();
	const auto id = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "battlefield", std::string("core:sand_shore"));
	ASSERT_TRUE(id);
	BattleSideArray<const CArmedInstance *> armies{attackerSideHero, defenderSideHero};
	BattleSideArray<const CGHeroInstance *> heroes{attackerSideHero, defenderSideHero};
	BattleStart rejected;
	rejected.battleID = next;
	rejected.info = BattleInfo::setupBattle(gameState().get(), {5, 5, 0}, terrain, BattleField(*id), armies, heroes, layout, nullptr);
	rejected.info->getSide(BattleSide::DEFENDER).heroID = attackerSideHero->id;
	EXPECT_THROW(newHorizonsHeroes::validateBattleSpellPointEntry(*gameState(), *rejected.info), std::runtime_error);
	EXPECT_THROW(gameHandler->sendAndApply(rejected), std::runtime_error);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 5);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 19);
	EXPECT_EQ(defenderSideHero->getNormalSpellPoints(), defenderNormal);
	EXPECT_EQ(defenderSideHero->getBufferSpellPoints(), defenderBuffer);
	EXPECT_EQ(attackerSideHero->getNewHorizonsLastAcceptedCombatDay(), beforeAttacker);
	EXPECT_EQ(defenderSideHero->getNewHorizonsLastAcceptedCombatDay(), beforeDefender);
	EXPECT_EQ(attackerSideHero->battle, nullptr);
	EXPECT_EQ(defenderSideHero->battle, nullptr);
	EXPECT_TRUE(gameState()->currentBattles.empty());
	EXPECT_EQ(gameState()->nextBattleID, next);
	EXPECT_EQ(gameState()->day, day);
}

TEST_F(NewHorizonsDeepTranceTest, LegacyNearFullEntryClipsToMaximumAndRetainsOnlyActualRecovery)
{
	legacyMagic = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_EQ(attackerSideHero->manaLimit(), 100);
	attackerSideHero->initializeSpellPoints(99, 19);
	EXPECT_EQ(newHorizonsHeroes::firstCombatMysticismRecovery(*attackerSideHero), 1);
	ASSERT_NO_FATAL_FAILURE(acceptEntry());
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 100);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 19);
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).deepTranceConsumed);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).deepTranceRecoveredNormal, 1);
	EXPECT_EQ(attackerSideHero->getNewHorizonsLastAcceptedCombatDay(), gameState()->day);
	ASSERT_NO_FATAL_FAILURE(complete());
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 100);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 19);
}
TEST_F(NewHorizonsDeepTranceTest, LegacyFullFirstEntryConsumesOpportunityWithoutOverfillOrBufferChange)
{
	legacyMagic = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_EQ(attackerSideHero->manaLimit(), 100);
	attackerSideHero->initializeSpellPoints(100, 19);
	EXPECT_EQ(newHorizonsHeroes::firstCombatMysticismRecovery(*attackerSideHero), 0);
	ASSERT_NO_FATAL_FAILURE(acceptEntry());
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 100);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 19);
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).deepTranceConsumed);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).deepTranceRecoveredNormal, 0);
	EXPECT_EQ(attackerSideHero->getNewHorizonsLastAcceptedCombatDay(), gameState()->day);
	ASSERT_NO_FATAL_FAILURE(complete());
	attackerSideHero->initializeSpellPoints(5, 19);
	ASSERT_NO_FATAL_FAILURE(acceptEntry());
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 5);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 19);
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).deepTranceConsumed);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).deepTranceRecoveredNormal, 0);
}

TEST_F(NewHorizonsDeepTranceTest, ReplayPredecessorRejectsCapturedPolicyAndActualReceiptBeforeEveryPrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(acceptEntry());
	const auto & side = battle()->getSide(BattleSide::ATTACKER);
	ASSERT_TRUE(side.deepTranceConsumed);
	ASSERT_GT(side.deepTranceRecoveredNormal, 0);
	const auto reject = [](auto & value)
	{
		CMemorySerializer old;
		old.oser.version = ESerializationVersion::NEW_HORIZONS_REPLAY_PAYLOAD_VERSIONS;
		EXPECT_THROW(value.serialize(old.oser), std::runtime_error);
		EXPECT_TRUE(old.extractBuffer().empty());
	};
	reject(*attackerSideHero);
	reject(battle()->getSide(BattleSide::ATTACKER));
	reject(*battle());
	BattleStart entry;
	entry.battleID = battle()->getBattleID();
	entry.info = battleStartFixture::snapshot(*battle(), gameState().get());
	reject(entry);
	GameSettings settings;
	settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, gameState()->getHeroDevelopmentRules());
	reject(settings);
	reject(gameState()->getMap());
	reject(*gameState());
	LobbyStartGame lobby;
	lobby.initializedStartInfo = std::make_shared<StartInfo>(*gameState()->getStartInfo());
	lobby.initializedGameState = gameState();
	reject(lobby);
	CMemorySerializer current;
	current.oser.version = current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(battle()->getSide(BattleSide::ATTACKER).serialize(current.oser));
	SideInBattle decoded(nullptr);
	ASSERT_NO_THROW(decoded.serialize(current.iser));
	EXPECT_EQ(decoded.deepTranceRecoveredNormal, side.deepTranceRecoveredNormal);
	EXPECT_EQ(decoded.initialAcceptedCombatDay, side.initialAcceptedCombatDay);
	EXPECT_TRUE(decoded.deepTranceConsumed);
}
TEST_F(NewHorizonsDeepTranceTest, ReplayPredecessorAbsentWorldRetainsAbsentPolicyAndDefaultReceipt)
{
	historicalCodecCapture = true;
	captured = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(attackerSideHero->getPrimaryGrowthRules()["recoverySpecialties"].isNull());
	CMemorySerializer old;
	old.oser.version = old.iser.version = ESerializationVersion::NEW_HORIZONS_REPLAY_PAYLOAD_VERSIONS;
	ASSERT_NO_THROW(old.oser & *gameState());
	CGameState restored;
	restored.preInit(LIBRARY);
	old.iser.loadingGamestate = true;
	old.iser.cb = &restored;
	ASSERT_NO_THROW(old.iser & restored);
	const auto * decoded = restored.getHero(attackerSideHero->id);
	ASSERT_NE(decoded, nullptr);
	EXPECT_TRUE(restored.getHeroDevelopmentRules()["recoverySpecialties"].isNull());
	EXPECT_TRUE(decoded->getPrimaryGrowthRules()["recoverySpecialties"].isNull());
	EXPECT_EQ(decoded->getNewHorizonsLastAcceptedCombatDay(), -1);
	EXPECT_EQ(decoded->getNormalSpellPoints(), 5);
	EXPECT_EQ(decoded->getBufferSpellPoints(), 19);
	EXPECT_TRUE(restored.currentBattles.empty());
}
