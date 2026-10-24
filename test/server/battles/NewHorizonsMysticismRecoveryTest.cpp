/*
 * NewHorizonsMysticismRecoveryTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/entities/hero/NewHorizonsRecoverySpecialties.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClient.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../server/queries/BattleQueries.h"
#include "../../../server/queries/QueriesProcessor.h"
namespace
{
class RecoveryRecordingServer : public RecordingGameServer
{
public:
	std::vector<SetMana> restorations;
	std::optional<NewTurn> lastTurn;
	void applyPack(CPackForClient & pack) override
	{
		if(const auto * mana = dynamic_cast<SetMana *>(&pack); mana && mana->operation == SetMana::Operation::RESTORE_NORMAL)
			restorations.push_back(*mana);
		if(const auto * turn = dynamic_cast<NewTurn *>(&pack))
			lastTurn = *turn;
		RecordingGameServer::applyPack(pack);
	}
};
class NewHorizonsMysticismRecoveryTest : public HeroCommandFixture
{
protected:
	RecoveryRecordingServer recoveryServer;
	std::string subject = "core:axsis";
	bool captured = true;
	bool legacy = false;
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires curated New Horizons content";
	}
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		auto rules = JsonNode(JsonPath::builtin("config/newHorizonsHeroes"));
		rules.setOverrideFlag(true);
		rules.Struct().erase("recoverySpecialties");
		if(captured)
		{
			auto & table = rules["recoverySpecialties"];
			table["version"].Integer() = 1;
			for(const auto key : {"infernalWellspring", "ritualReserve", "deepTrance"})
			{
				table[key]["dailyPercentPerLevel"].Integer() = 5;
				table[key]["dailyMaximumPercent"].Integer() = 100;
			}
			table["infernalWellspring"]["victoryPercent"].Integer() = 25;
			table["ritualReserve"]["lowNormalPercent"].Integer() = 50;
			table["deepTrance"]["firstCombatPercent"].Integer() = 50;
		}
		if(legacy)
			rules["startingSkills"].Struct().erase("startingBookReplacements");
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			legacy ? JsonNode() : JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		for(const auto & object : map->objects)
			if(auto * hero = dynamic_cast<CGHeroInstance *>(object.get()); hero && hero->getOwner() == PlayerColor(0))
				hero->setHeroType(HeroTypeID(HeroTypeID::decode(subject)));
	}
	void prepare(int normal = 0, int buffer = 13)
	{
		startGame();
		recoveryServer.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(recoveryServer, gameState());
		gameHandler->randomizer->setSeed(seed);
		attackerSideHero->level = 20;
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:wisdom")), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:wisdom", "new-horizons:wisdom.mysticism"));
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		ASSERT_EQ(attackerSideHero->manaLimit(), 100);
		attackerSideHero->initializeSpellPoints(normal, buffer);
		for(auto * hero : {attackerSideHero, defenderSideHero})
			hero->setStackCount(SlotID(0), 4); // Controlled battle survivors, not recruitment acceptance.
	}
	void acceptResult()
	{
		for(const auto player : {PlayerColor(0), PlayerColor(1)})
		{
			const auto query = gameHandler->queries->topQuery(player);
			if(query && query->getType() == QueryType::BattleDialog)
				ASSERT_TRUE(gameHandler->queryReply(query->queryID, 0, player));
		}
	}
	void finish(BattleSide winner = BattleSide::ATTACKER, EBattleResult result = EBattleResult::NORMAL)
	{
		startBattle();
		beginCombat();
		auto query = std::make_shared<CBattleQuery>(gameHandler.get(), battle());
		gameHandler->queries->addQuery(query);
		if(result == EBattleResult::NORMAL)
			gameHandler->battles->cheatBattleVictory(battle()->sideToPlayer(winner));
		else
		{
			const auto losingSide = battle()->otherSide(winner);
			for(int count = 0; count < 8 && battle()->battleActiveUnit()->unitSide() != losingSide; ++count)
			{
				const auto * active = battle()->battleActiveUnit();
				ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->battleGetActionController(active), BattleAction::makeDefend(active)));
			}
			ASSERT_EQ(battle()->battleActiveUnit()->unitSide(), losingSide);
			const auto player = battle()->sideToPlayer(losingSide);
			if(result == EBattleResult::SURRENDER)
				gameHandler->giveResource(player, EGameResID::GOLD, battle()->battleGetSurrenderCost(player));
			const auto action = result == EBattleResult::SURRENDER ? BattleAction::makeSurrender(losingSide) : BattleAction::makeRetreat(losingSide);
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), player, action));
		}
		ASSERT_TRUE(query->result);
		ASSERT_NO_FATAL_FAILURE(acceptResult());
	}
};
}
TEST_F(NewHorizonsMysticismRecoveryTest, DailyCoreFloorsLevelScalingAndOrdinaryMaximumStaySeparate)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(newHorizonsHeroes::originalMysticismRecovery(*attackerSideHero), 10);
	EXPECT_EQ(attackerSideHero->manaRegain(), 20);
	attackerSideHero->level = 1;
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 20, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(attackerSideHero->manaRegain(), 5); // floor(5*5/100)=0.
	attackerSideHero->level = std::numeric_limits<uint32_t>::max();
	EXPECT_EQ(attackerSideHero->manaRegain(), 10);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 13);
}
TEST_F(NewHorizonsMysticismRecoveryTest, RealArtifactsCompeteInsteadOfReceivingTheMysticismMultiplier)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	giveArtifact(attackerSideHero, ArtifactID(ArtifactID::decode("core:charmOfMana")), ArtifactPosition::MISC1);
	giveArtifact(attackerSideHero, ArtifactID(ArtifactID::decode("core:talismanOfMana")), ArtifactPosition::MISC2);
	giveArtifact(attackerSideHero, ArtifactID(ArtifactID::decode("core:mysticOrbOfMana")), ArtifactPosition::MISC3);
	EXPECT_EQ(attackerSideHero->manaRegain(), 31); // 1+5+10+15, not doubled.
	EXPECT_EQ(newHorizonsHeroes::victoryMysticismRecovery(*attackerSideHero), 5); // Quarter of D20, not artifact31.
}
TEST_F(NewHorizonsMysticismRecoveryTest, JaegarSamplesOnlyNormalStrictlyBelowHalfOnCompletedGlobalDay)
{
	subject = "core:jaegar";
	ASSERT_NO_FATAL_FAILURE(prepare(49, 1000));
	EXPECT_EQ(attackerSideHero->getManaNewTurn(true), 74);
	EXPECT_EQ(attackerSideHero->getManaNewTurn(false), 69);
	attackerSideHero->setNormalSpellPoints(50);
	EXPECT_EQ(attackerSideHero->getManaNewTurn(true), 70);
	attackerSideHero->setNormalSpellPoints(51);
	EXPECT_EQ(attackerSideHero->getManaNewTurn(true), 71);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 1000);
}
TEST_F(NewHorizonsMysticismRecoveryTest, ActualNewTurnExcludesInitialJaegarExtraThenAppliesCompletedDaySample)
{
	subject = "core:jaegar";
	ASSERT_NO_FATAL_FAILURE(prepare(0, 13));
	ASSERT_EQ(gameState()->day, 0u);
	gameHandler->onNewTurn();
	ASSERT_TRUE(recoveryServer.lastTurn);
	EXPECT_EQ(recoveryServer.lastTurn->day, 1u);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 20);
	attackerSideHero->setNormalSpellPoints(49);
	gameHandler->onNewTurn();
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 74);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 13);
}
TEST_F(NewHorizonsMysticismRecoveryTest, MeditationRemainsSeparateAfterDailyFloorAndNormalCapacityClips)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:wisdom")), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({"new-horizons:wisdom", "new-horizons:wisdom.meditation"});
	attackerSideHero->setMovementPoints(25);
	EXPECT_EQ(attackerSideHero->getManaNewTurn(true, 100), 35);
	EXPECT_EQ(attackerSideHero->getManaNewTurn(false, 100), 20);
	attackerSideHero->setNormalSpellPoints(95);
	EXPECT_EQ(attackerSideHero->getManaNewTurn(true, 100), 100);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 13);
}
TEST_F(NewHorizonsMysticismRecoveryTest, AxsisRealVictoryRestoresNormalOnceAndLeavesBufferUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(prepare(7, 13));
	ASSERT_NO_FATAL_FAILURE(finish());
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 12);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 13);
	ASSERT_EQ(recoveryServer.restorations.size(), 1u);
	EXPECT_EQ(recoveryServer.restorations.front().amount, 5);
	gameHandler->battles->cheatBattleVictory(PlayerColor(0));
	EXPECT_EQ(recoveryServer.restorations.size(), 1u);
}
TEST_F(NewHorizonsMysticismRecoveryTest, OpponentRetreatIsARealVictory)
{
	ASSERT_NO_FATAL_FAILURE(prepare(7, 13));
	ASSERT_NO_FATAL_FAILURE(finish(BattleSide::ATTACKER, EBattleResult::ESCAPE));
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 12);
}
TEST_F(NewHorizonsMysticismRecoveryTest, OpponentSurrenderIsARealVictory)
{
	ASSERT_NO_FATAL_FAILURE(prepare(7, 13));
	ASSERT_NO_FATAL_FAILURE(finish(BattleSide::ATTACKER, EBattleResult::SURRENDER));
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 12);
}
TEST_F(NewHorizonsMysticismRecoveryTest, LostBattleDoesNotGrantWellspring)
{
	ASSERT_NO_FATAL_FAILURE(prepare(7, 13));
	ASSERT_NO_FATAL_FAILURE(finish(BattleSide::DEFENDER));
	EXPECT_TRUE(recoveryServer.restorations.empty());
}
TEST_F(NewHorizonsMysticismRecoveryTest, CancellationDoesNotInventVictoryRecovery)
{
	ASSERT_NO_FATAL_FAILURE(prepare(7, 13));
	startBattle();
	BattleCancelled cancel;
	cancel.battleID = battle()->getBattleID();
	gameState()->apply(cancel);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 7);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 13);
	EXPECT_TRUE(recoveryServer.restorations.empty());
}
TEST_F(NewHorizonsMysticismRecoveryTest, MixedLegacyMagicStillUsesExplicitNormalBoundsAndKeepsRecovery)
{
	legacy = true;
	ASSERT_NO_FATAL_FAILURE(prepare(99, 0));
	EXPECT_EQ(attackerSideHero->getManaNewTurn(), 100);
	ASSERT_NO_FATAL_FAILURE(finish());
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 100);
	ASSERT_EQ(recoveryServer.restorations.size(), 1u);
	EXPECT_EQ(recoveryServer.restorations.front().amount, 1);
}
TEST_F(NewHorizonsMysticismRecoveryTest, AbsentCaptureKeepsOrdinaryMysticism)
{
	captured = false;
	ASSERT_NO_FATAL_FAILURE(prepare(7, 13));
	EXPECT_EQ(attackerSideHero->manaRegain(), 10);
	ASSERT_NO_FATAL_FAILURE(finish());
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 7);
	EXPECT_TRUE(recoveryServer.restorations.empty());
}
TEST_F(NewHorizonsMysticismRecoveryTest, InactiveMysticismDisablesAllRecoveryWithoutChangingSelection)
{
	subject = "core:rosic";
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:wisdom")), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	EXPECT_TRUE(newHorizonsHeroes::hasCapturedDeepTrance(*attackerSideHero));
	EXPECT_EQ(newHorizonsHeroes::firstCombatMysticismRecovery(*attackerSideHero), 0);
	EXPECT_EQ(attackerSideHero->manaRegain(), 1);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 0);
}
TEST_F(NewHorizonsMysticismRecoveryTest, RosicFirstCombatBudgetUsesOriginalMysticismNotEnhancedDailyFloor)
{
	subject = "core:rosic";
	ASSERT_NO_FATAL_FAILURE(prepare(0, 13));
	EXPECT_EQ(newHorizonsHeroes::enhancedMysticismRecovery(*attackerSideHero, true), 20);
	EXPECT_EQ(newHorizonsHeroes::firstCombatMysticismRecovery(*attackerSideHero), 5);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 0); // Pure preview consumes/grants nothing.
}

TEST_F(NewHorizonsMysticismRecoveryTest, CapturedRecoveryDoesNotTransferToAnotherHeroIdentity)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:christian")));
	EXPECT_EQ(attackerSideHero->manaRegain(), 10);
	EXPECT_EQ(newHorizonsHeroes::victoryMysticismRecovery(*attackerSideHero), 0);
	EXPECT_FALSE(newHorizonsHeroes::hasCapturedDeepTrance(*attackerSideHero));
}
TEST_F(NewHorizonsMysticismRecoveryTest, RealArmageddonDrawHasNoWellspringVictoryGrant)
{
	ASSERT_NO_FATAL_FAILURE(prepare(7, 13));
	giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	defenderSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:havocMagic")), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	defenderSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	defenderSideHero->addSpellToSpellbook(SpellID::ARMAGEDDON);
	defenderSideHero->initializeSpellPoints(100, 0);
	startBattle();
	beginCombat();
	auto query = std::make_shared<CBattleQuery>(gameHandler.get(), battle());
	gameHandler->queries->addQuery(query);
	for(int count = 0; count < 8 && battle()->battleActiveUnit()->unitSide() != BattleSide::DEFENDER; ++count)
	{
		const auto * active = battle()->battleActiveUnit();
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->battleGetActionController(active), BattleAction::makeDefend(active)));
	}
	ASSERT_EQ(battle()->battleActiveUnit()->unitSide(), BattleSide::DEFENDER);
	BattleAction cast;
	cast.actionType = EActionType::HERO_SPELL;
	cast.side = BattleSide::DEFENDER;
	cast.spell = SpellID::ARMAGEDDON;
	cast.aimToHex(BattleHex::INVALID);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), cast));
	ASSERT_TRUE(query->result);
	ASSERT_EQ(query->result->winner, BattleSide::NONE);
	ASSERT_NO_FATAL_FAILURE(acceptResult());
	EXPECT_TRUE(recoveryServer.restorations.empty());
}
TEST_F(NewHorizonsMysticismRecoveryTest, CapturedPresentationStatesActualGlobalDaySampleAndConfiguredMath)
{
	subject = "core:jaegar";
	ASSERT_NO_FATAL_FAILURE(prepare(49, 13));
	const auto text = newHorizonsHeroes::resolveSpecialtyPresentation(*attackerSideHero->getHeroType(),
		attackerSideHero->getPrimaryGrowthRules(), attackerSideHero->getMagicRules(), std::nullopt, attackerSideHero);
	EXPECT_EQ(text.name, "Ritual Reserve");
	EXPECT_NE(text.description.find("5% per hero level"), std::string::npos);
	EXPECT_NE(text.description.find("completed global-day boundary"), std::string::npos);
	EXPECT_NE(text.description.find("50% of the original"), std::string::npos);
	EXPECT_NE(text.description.find("25 Normal Spell Points"), std::string::npos);
	EXPECT_EQ(text.description.find("EndTurn snapshot"), std::string::npos);
	auto old = attackerSideHero->getPrimaryGrowthRules();
	old.Struct().erase("recoverySpecialties");
	const auto legacy = newHorizonsHeroes::resolveSpecialtyPresentation(*attackerSideHero->getHeroType(),
		old, attackerSideHero->getMagicRules(), std::nullopt);
	EXPECT_NE(legacy.name, "Ritual Reserve");
}
