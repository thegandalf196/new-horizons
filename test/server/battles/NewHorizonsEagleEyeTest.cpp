/*
 * NewHorizonsEagleEyeTest.cpp, part of VCMI engine
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../SpellPointTestUtils.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/filesystem/ResourcePath.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/NewHorizonsEagleEye.h"
#include "../../../lib/entities/hero/NewHorizonsLearningSpecialties.h"
#include "../../../lib/networkPacks/PacksForClient.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../../server/queries/BattleQueries.h"
#include "../../../server/queries/QueriesProcessor.h"
#include <vcmi/spells/Spell.h>
#include <vstd/ContainerUtils.h>

namespace
{
class EagleEyeRecordingServer : public RecordingGameServer
{
public:
	std::vector<ChangeSpells> learningReceipts;
	void applyPack(CPackForClient & pack) override
	{
		if(const auto * learning = dynamic_cast<const ChangeSpells *>(&pack))
			learningReceipts.push_back(*learning);
		if(const auto * result = dynamic_cast<const BattleResultsApplied *>(&pack))
			learningReceipts.push_back(result->learnedSpells);
		RecordingGameServer::applyPack(pack);
	}
};
class NewHorizonsEagleEyeTest : public BattleTestFixture
{
protected:
	EagleEyeRecordingServer learningServer;
	bool legacyMagic = false;
	bool disableFrostRing = false;
	std::string specialtyHero;
	bool specialtyCaptured = true;
	bool secondEnemySpell = false;
	bool retainDraw = false;
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons content";
	}
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		if(retainDraw)
		{
			JsonNode enabled;
			enabled.Bool() = true;
			loaded->overrideGameSetting(EGameSettings::HEROES_RETREAT_ON_WIN_WITHOUT_TROOPS, enabled);
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		auto magic = legacyMagic ? JsonNode() : LIBRARY->settingsHandler->getValue(EGameSettings::MAGIC_NEW_HORIZONS);
		if(disableFrostRing)
			magic["spells"]["core:frostRing"]["active"].Bool() = false;
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magic);
		if(!specialtyHero.empty() || legacyMagic)
		{
			auto rules = JsonNode(JsonPath::builtin("config/newHorizonsHeroes"));
			rules.setOverrideFlag(true);
			if(legacyMagic)
				rules["startingSkills"].Struct().erase("startingBookReplacements");
			if(!specialtyCaptured)
				rules["additionalSpecialties"].Struct().erase("graveArchivist");
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
			if(!specialtyHero.empty())
				for(const auto & object : loaded->objects)
					if(auto * hero = dynamic_cast<CGHeroInstance *>(object.get()); hero && hero->getOwner() == PlayerColor(0))
						hero->setHeroType(HeroTypeID(HeroTypeID::decode(specialtyHero)));
		}
	}
	void prepare(bool selected = true)
	{
		startGame();
		if(secondEnemySpell)
			for(auto * hero : {attackerSideHero, defenderSideHero})
				hero->setStackCount(SlotID(0), 4); // Controlled survivors for the second cast.
		learningServer.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(learningServer, gameState());
		gameHandler->randomizer->setSeed(seed);
		for(auto * hero : {attackerSideHero, defenderSideHero})
		{
			giveArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
			hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:havocMagic")),
				MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
			hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")),
				MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
			for(const auto spell : {SpellID::HASTE, SpellID::MAGIC_ARROW, SpellID::FROST_RING, SpellID::CHAIN_LIGHTNING})
				hero->removeSpellFromSpellbook(SpellID(spell));
		}
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(std::string(newHorizonsEagleEye::SKILL_ID))),
			MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		if(selected && !attackerSideHero->getPerkState().hasSelection(std::string(newHorizonsEagleEye::SKILL_ID),
			std::string(newHorizonsEagleEye::PERK_ID)))
			attackerSideHero->applyPerkSelection({std::string(newHorizonsEagleEye::SKILL_ID),
				std::string(newHorizonsEagleEye::PERK_ID)});
	}
	void acceptResult()
	{
		for(const auto player : {PlayerColor(0), PlayerColor(1)})
		{
			const auto query = gameHandler->queries->topQuery(player);
			if(query && query->getType() == QueryType::BattleDialog)
			{
				ASSERT_TRUE(gameHandler->queryReply(query->queryID, 0, player));
			}
		}
	}
	void prepareEnemyHasteCast(bool selected = true)
	{
		ASSERT_NO_FATAL_FAILURE(prepare(selected));
		defenderSideHero->addSpellToSpellbook(SpellID(SpellID::HASTE));
		defenderSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(defenderSideHero, 100);
		startBattle();
		beginCombat();
		for(int actions = 0; actions < 8 && battle()->battleActiveUnit()
			&& battle()->battleActiveUnit()->unitSide() != BattleSide::DEFENDER; ++actions)
		{
			const auto * active = battle()->battleActiveUnit();
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
				battle()->battleGetActionController(active), BattleAction::makeDefend(active)));
		}
		ASSERT_NE(battle()->battleActiveUnit(), nullptr);
		ASSERT_EQ(battle()->battleActiveUnit()->unitSide(), BattleSide::DEFENDER);
		const auto targets = battle()->battleGetStacksIf([](const CStack * stack)
			{ return stack->unitSide() == BattleSide::DEFENDER && stack->alive(); });
		ASSERT_FALSE(targets.empty());
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::DEFENDER;
		action.spell = SpellID(SpellID::HASTE);
		action.aimToUnit(targets.front());
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), action));
		ASSERT_EQ(battle()->getUsedSpells(BattleSide::DEFENDER), std::vector<SpellID>{SpellID(SpellID::HASTE)});
		ASSERT_TRUE(attackerSideHero->canLearnSpell(SpellID(SpellID::HASTE).toEntity(LIBRARY->spells())));
		if(secondEnemySpell)
		{
			defenderSideHero->addSpellToSpellbook(SpellID(SpellID::MAGIC_ARROW));
			const auto firstRound = battle()->battleGetRound();
			for(int steps = 0; steps < 40 && (battle()->battleGetRound() <= firstRound
				|| battle()->battleActiveUnit()->unitSide() != BattleSide::DEFENDER); ++steps)
			{
				const auto * active = battle()->battleActiveUnit();
				ASSERT_NE(active, nullptr);
				ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
					battle()->battleGetActionController(active), BattleAction::makeDefend(active)));
			}
			ASSERT_GT(battle()->battleGetRound(), firstRound);
			const auto enemies = battle()->battleGetStacksIf([](const CStack * stack)
				{ return stack->unitSide() == BattleSide::ATTACKER && stack->alive(); });
			ASSERT_FALSE(enemies.empty());
			BattleAction arrow;
			arrow.actionType = EActionType::HERO_SPELL;
			arrow.side = BattleSide::DEFENDER;
			arrow.spell = SpellID(SpellID::MAGIC_ARROW);
			arrow.aimToUnit(enemies.front());
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), arrow));
			ASSERT_EQ(battle()->getUsedSpells(BattleSide::DEFENDER),
				(std::vector<SpellID>{SpellID(SpellID::HASTE), SpellID(SpellID::MAGIC_ARROW)}));
		}
	}
	void finishWinner(BattleSide winner)
	{
		auto query = std::make_shared<CBattleQuery>(gameHandler.get(), battle());
		gameHandler->queries->addQuery(query);
		gameHandler->battles->cheatBattleVictory(battle()->sideToPlayer(winner));
		ASSERT_TRUE(query->result);
		ASSERT_NO_FATAL_FAILURE(acceptResult());
	}
	void verifyRetainedLoser(EBattleResult result)
	{
		ASSERT_NO_FATAL_FAILURE(prepareEnemyHasteCast());
		auto query = std::make_shared<CBattleQuery>(gameHandler.get(), battle());
		gameHandler->queries->addQuery(query);
		for(int actions = 0; actions < 8 && battle()->battleActiveUnit()
			&& battle()->battleActiveUnit()->unitSide() != BattleSide::ATTACKER; ++actions)
		{
			const auto * active = battle()->battleActiveUnit();
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
				battle()->battleGetActionController(active), BattleAction::makeDefend(active)));
		}
		ASSERT_NE(battle()->battleActiveUnit(), nullptr);
		ASSERT_EQ(battle()->battleActiveUnit()->unitSide(), BattleSide::ATTACKER);
		if(result == EBattleResult::SURRENDER)
			gameHandler->giveResource(PlayerColor(0), EGameResID::GOLD, battle()->battleGetSurrenderCost(PlayerColor(0)));
		else
			ASSERT_TRUE(battle()->battleCanFlee(PlayerColor(0)));
		const auto action = result == EBattleResult::SURRENDER
			? BattleAction::makeSurrender(BattleSide::ATTACKER) : BattleAction::makeRetreat(BattleSide::ATTACKER);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
		ASSERT_TRUE(query->result);
		ASSERT_NO_FATAL_FAILURE(acceptResult());
		EXPECT_TRUE(attackerSideHero->getSpellsInSpellbook().contains(SpellID(SpellID::HASTE)))
			<< "Learning must precede preserving the escaping hero in the tavern pool";
	}
};
}

TEST_F(NewHorizonsEagleEyeTest, HighestEligibleLevelAndEarliestTie)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(newHorizonsEagleEye::enabled(attackerSideHero));
	ASSERT_TRUE(attackerSideHero->canLearnSpell(SpellID(SpellID::FROST_RING).toEntity(LIBRARY->spells())));
	EXPECT_EQ(newHorizonsEagleEye::selectSpell(attackerSideHero,
		{SpellID(SpellID::HASTE), SpellID(SpellID::FROST_RING), SpellID(SpellID::CHAIN_LIGHTNING)}), SpellID(SpellID::FROST_RING));
	EXPECT_EQ(newHorizonsEagleEye::selectSpell(attackerSideHero,
		{SpellID(SpellID::HASTE), SpellID(SpellID::MAGIC_ARROW), SpellID(SpellID::HASTE)}), SpellID(SpellID::HASTE));
}

TEST_F(NewHorizonsEagleEyeTest, KnownOrSchoolIneligibleEntriesDoNotHideNextEligibleCast)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->addSpellToSpellbook(SpellID(SpellID::HASTE));
	// Level 2 Frost Ring is legally learned without a school skill. Use a
	// genuine Level 3 school-gated spell to measure this exclusion instead.
	const SpellID schoolGated(SpellID::INFERNO);
	attackerSideHero->removeSpellFromSpellbook(schoolGated);
	ASSERT_TRUE(attackerSideHero->canLearnSpell(schoolGated.toEntity(LIBRARY->spells())));
	const auto schools = newHorizonsMagic::spellSchoolSkills(attackerSideHero->getMagicRules(), schoolGated);
	ASSERT_FALSE(schools.empty());
	for(const auto school : schools)
		attackerSideHero->setSecSkillLevel(school, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	ASSERT_FALSE(attackerSideHero->canLearnSpell(schoolGated.toEntity(LIBRARY->spells())));
	ASSERT_TRUE(attackerSideHero->canLearnSpell(SpellID(SpellID::MAGIC_ARROW).toEntity(LIBRARY->spells())));
	EXPECT_EQ(newHorizonsEagleEye::selectSpell(attackerSideHero,
		{SpellID(SpellID::HASTE), schoolGated, SpellID(SpellID::MAGIC_ARROW)}), SpellID(SpellID::MAGIC_ARROW));
}

TEST_F(NewHorizonsEagleEyeTest, UnselectedAndEmptyHistoriesAreInert)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	EXPECT_FALSE(newHorizonsEagleEye::enabled(attackerSideHero));
	EXPECT_FALSE(newHorizonsEagleEye::selectSpell(attackerSideHero, {SpellID(SpellID::HASTE)}));
	EXPECT_FALSE(newHorizonsEagleEye::selectSpell(attackerSideHero, {}));
}

TEST_F(NewHorizonsEagleEyeTest, SavedRosterExclusionAndInvalidHistoryCannotBeLearned)
{
	disableFrostRing = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_FALSE(attackerSideHero->canLearnSpell(SpellID(SpellID::FROST_RING).toEntity(LIBRARY->spells())));
	EXPECT_EQ(newHorizonsEagleEye::selectSpell(attackerSideHero,
		{SpellID(), SpellID(1'000'000), SpellID(SpellID::FROST_RING), SpellID(SpellID::HASTE)}), SpellID(SpellID::HASTE));
}

TEST_F(NewHorizonsEagleEyeTest, AbsentMagicRulesDoNotEnableSelectedPerk)
{
	legacyMagic = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_FALSE(newHorizonsEagleEye::enabled(attackerSideHero));
	EXPECT_FALSE(newHorizonsEagleEye::selectSpell(attackerSideHero, {SpellID(SpellID::HASTE)}));
}

TEST_F(NewHorizonsEagleEyeTest, ActualEnemyHeroCastIsLearnedByWinnerAfterResult)
{
	ASSERT_NO_FATAL_FAILURE(prepareEnemyHasteCast());
	auto query = std::make_shared<CBattleQuery>(gameHandler.get(), battle());
	gameHandler->queries->addQuery(query);
	gameHandler->battles->cheatBattleVictory(PlayerColor(0));
	ASSERT_NO_FATAL_FAILURE(acceptResult());
	EXPECT_FALSE(attackerSideHero->canLearnSpell(SpellID(SpellID::HASTE).toEntity(LIBRARY->spells())));
	EXPECT_TRUE(attackerSideHero->getSpellsInSpellbook().contains(SpellID(SpellID::HASTE)));
}

TEST_F(NewHorizonsEagleEyeTest, ActualRetreatRetainsLearnedSpell)
{
	ASSERT_NO_FATAL_FAILURE(verifyRetainedLoser(EBattleResult::ESCAPE));
}

TEST_F(NewHorizonsEagleEyeTest, ActualSurrenderRetainsLearnedSpell)
{
	ASSERT_NO_FATAL_FAILURE(verifyRetainedLoser(EBattleResult::SURRENDER));
}

TEST_F(NewHorizonsEagleEyeTest, UnselectedLegacyBonusStillPublishesItsOrdinaryLearningReceipt)
{
	ASSERT_NO_FATAL_FAILURE(prepareEnemyHasteCast(false));
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::LEARN_BATTLE_SPELL_LEVEL_LIMIT, BonusSource::OTHER, 3, BonusSourceID()));
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::LEARN_BATTLE_SPELL_CHANCE, BonusSource::OTHER, 100, BonusSourceID()));
	const auto learnerId = attackerSideHero->id;
	ASSERT_FALSE(newHorizonsEagleEye::enabled(attackerSideHero));
	ASSERT_NO_FATAL_FAILURE(finishWinner(BattleSide::ATTACKER));
	const auto receipt = std::ranges::find_if(learningServer.learningReceipts, [learnerId](const ChangeSpells & learning)
		{ return learning.hid == learnerId && !learning.spells.empty(); });
	ASSERT_NE(receipt, learningServer.learningReceipts.end());
	EXPECT_TRUE(receipt->eagleEyeBonus);
	EXPECT_EQ(receipt->spells, std::set<SpellID>{SpellID(SpellID::HASTE)});
	EXPECT_TRUE(attackerSideHero->getSpellsInSpellbook().contains(SpellID(SpellID::HASTE)));
}

TEST_F(NewHorizonsEagleEyeTest, OrdinaryCreatureCastDoesNotEnterEnemyHeroHistoryOrLearningReceipt)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	startBattle();
	beginCombat();
	for(int actions = 0; actions < 8 && battle()->battleActiveUnit()
		&& battle()->battleActiveUnit()->unitSide() != BattleSide::DEFENDER; ++actions)
	{
		const auto * active = battle()->battleActiveUnit();
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->battleGetActionController(active), BattleAction::makeDefend(active)));
	}
	ASSERT_NE(battle()->battleActiveUnit(), nullptr);
	ASSERT_EQ(battle()->battleActiveUnit()->unitSide(), BattleSide::DEFENDER);
	auto * caster = battle()->getStack(battle()->battleActiveUnit()->unitId());
	caster->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELLCASTER, BonusSource::CREATURE_ABILITY, 1, BonusSourceID(),
		BonusSubtypeID(SpellID(SpellID::HASTE))));
	caster->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::CASTS, BonusSource::CREATURE_ABILITY, 1, BonusSourceID()));
	ASSERT_EQ(caster->casts.total(), 1);
	spells::Target target{spells::Destination(caster)};
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeCreatureSpellcast(caster, target, SpellID(SpellID::HASTE))));
	ASSERT_FALSE(learningServer.castsOf(SpellID(SpellID::HASTE)).empty());
	EXPECT_TRUE(battle()->getUsedSpells(BattleSide::DEFENDER).empty());
	const auto learnerId = attackerSideHero->id;
	ASSERT_NO_FATAL_FAILURE(finishWinner(BattleSide::ATTACKER));
	EXPECT_FALSE(attackerSideHero->getSpellsInSpellbook().contains(SpellID(SpellID::HASTE)));
	EXPECT_FALSE(std::ranges::any_of(learningServer.learningReceipts, [learnerId](const ChangeSpells & learning)
		{ return learning.hid == learnerId && !learning.spells.empty(); }));
}

TEST_F(NewHorizonsEagleEyeTest, NormallyDefeatedLoserReceivesNoLearningBeforeRemoval)
{
	ASSERT_NO_FATAL_FAILURE(prepareEnemyHasteCast());
	const auto learnerId = attackerSideHero->id;
	ASSERT_NO_FATAL_FAILURE(finishWinner(BattleSide::DEFENDER));
	EXPECT_EQ(gameState()->getHero(learnerId), nullptr);
	EXPECT_FALSE(std::ranges::any_of(learningServer.learningReceipts, [learnerId](const ChangeSpells & learning)
		{ return learning.hid == learnerId && !learning.spells.empty(); }));
}

TEST_F(NewHorizonsEagleEyeTest, NimbusChoosesDistinctEligibleSpellsInStableLevelAndCastOrder)
{
	specialtyHero = "core:nimbus";
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto before = attackerSideHero->getSpellsInSpellbook();
	EXPECT_EQ(newHorizonsEagleEye::selectSpells(attackerSideHero,
		{SpellID::HASTE, SpellID::HASTE, SpellID::FROST_RING, SpellID::MAGIC_ARROW, SpellID::CHAIN_LIGHTNING}),
		(std::vector<SpellID>{SpellID::FROST_RING, SpellID::HASTE}));
	EXPECT_EQ(attackerSideHero->getSpellsInSpellbook(), before);
	EXPECT_TRUE(newHorizonsEagleEye::selectSpells(attackerSideHero, {}).empty());
	attackerSideHero->addSpellToSpellbook(SpellID::FROST_RING);
	EXPECT_EQ(newHorizonsEagleEye::selectSpells(attackerSideHero,
		{SpellID::FROST_RING, SpellID::MAGIC_ARROW, SpellID::HASTE}),
		(std::vector<SpellID>{SpellID::MAGIC_ARROW, SpellID::HASTE}));
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:learning")),
		MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	EXPECT_TRUE(newHorizonsEagleEye::selectSpells(attackerSideHero, {SpellID::HASTE, SpellID::MAGIC_ARROW}).empty());
}
TEST_F(NewHorizonsEagleEyeTest, NimbusAbsentCaptureRetainsSingleSpellSelection)
{
	specialtyHero = "core:nimbus";
	specialtyCaptured = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(newHorizonsEagleEye::selectSpells(attackerSideHero, {SpellID::HASTE, SpellID::MAGIC_ARROW}),
		(std::vector<SpellID>{SpellID::HASTE}));
}
TEST_F(NewHorizonsEagleEyeTest, NimbusActualTwoEnemyCastsPublishBothWinnerSpells)
{
	specialtyHero = "core:nimbus";
	secondEnemySpell = true;
	ASSERT_NO_FATAL_FAILURE(prepareEnemyHasteCast());
	ASSERT_NO_FATAL_FAILURE(finishWinner(BattleSide::ATTACKER));
	EXPECT_TRUE(attackerSideHero->getSpellsInSpellbook().contains(SpellID::HASTE));
	EXPECT_TRUE(attackerSideHero->getSpellsInSpellbook().contains(SpellID::MAGIC_ARROW));
}
TEST_F(NewHorizonsEagleEyeTest, NimbusActualRetreatPublishesBothDistinctSpells)
{
	specialtyHero = "core:nimbus";
	secondEnemySpell = true;
	ASSERT_NO_FATAL_FAILURE(verifyRetainedLoser(EBattleResult::ESCAPE));
	EXPECT_TRUE(attackerSideHero->getSpellsInSpellbook().contains(SpellID::MAGIC_ARROW));
}
TEST_F(NewHorizonsEagleEyeTest, NimbusActualSurrenderPublishesBothDistinctSpells)
{
	specialtyHero = "core:nimbus";
	secondEnemySpell = true;
	ASSERT_NO_FATAL_FAILURE(verifyRetainedLoser(EBattleResult::SURRENDER));
	EXPECT_TRUE(attackerSideHero->getSpellsInSpellbook().contains(SpellID::MAGIC_ARROW));
}
TEST_F(NewHorizonsEagleEyeTest, NimbusLearningEnhancesOnlyTheCoreContributionWithSeparateFloors)
{
	specialtyHero = "core:nimbus";
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->level = 1;
	EXPECT_EQ(attackerSideHero->calculateXp(101), 111);
	attackerSideHero->level = 10;
	EXPECT_EQ(attackerSideHero->calculateXp(1000), 1150);
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::HERO_EXPERIENCE_GAIN_PERCENT, BonusSource::OTHER, 20, BonusSourceID(),
		BonusSubtypeID(), BonusValueType::PERCENT_TO_BASE));
	EXPECT_EQ(attackerSideHero->calculateXp(1000), 1350);
	EXPECT_EQ(attackerSideHero->calculateXp(1000, 25), 1600);
	EXPECT_EQ(attackerSideHero->calculateXp(0), 0);
	EXPECT_EQ(attackerSideHero->calculateXp(-1000), -1300);
	attackerSideHero->level = std::numeric_limits<uint32_t>::max();
	EXPECT_EQ(attackerSideHero->calculateXp(1000), 1400);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:learning")),
		MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(attackerSideHero->calculateXp(1000), 1200);
}

TEST_F(NewHorizonsEagleEyeTest, NimbusRetainedDrawLearnsBothPriorEnemySpells)
{
	specialtyHero = "core:nimbus";
	secondEnemySpell = true;
	retainDraw = true;
	ASSERT_NO_FATAL_FAILURE(prepareEnemyHasteCast());
	const auto learnerId = attackerSideHero->id;
	auto query = std::make_shared<CBattleQuery>(gameHandler.get(), battle());
	gameHandler->queries->addQuery(query);
	const auto previousRound = battle()->battleGetRound();
	for(int steps = 0; steps < 40 && (battle()->battleGetRound() <= previousRound
		|| battle()->battleActiveUnit()->unitSide() != BattleSide::DEFENDER); ++steps)
	{
		const auto * active = battle()->battleActiveUnit();
		ASSERT_NE(active, nullptr);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->battleGetActionController(active), BattleAction::makeDefend(active)));
	}
	ASSERT_GT(battle()->battleGetRound(), previousRound);
	defenderSideHero->addSpellToSpellbook(SpellID::ARMAGEDDON);
	setTestSpellPointTotal(defenderSideHero, 100);
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::DEFENDER;
	action.spell = SpellID::ARMAGEDDON;
	action.aimToHex(BattleHex::INVALID);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), action));
	ASSERT_TRUE(query->result);
	ASSERT_EQ(query->result->winner, BattleSide::NONE);
	ASSERT_NO_FATAL_FAILURE(acceptResult());
	const auto receipt = std::find_if(learningServer.learningReceipts.begin(), learningServer.learningReceipts.end(),
		[learnerId](const ChangeSpells & entry) { return entry.hid == learnerId && entry.eagleEyeBonus; });
	ASSERT_NE(receipt, learningServer.learningReceipts.end());
	EXPECT_EQ(receipt->spells, (std::set<SpellID>{SpellID::HASTE, SpellID::MAGIC_ARROW}));
}

TEST_F(NewHorizonsEagleEyeTest, NimbusPresentationUsesCapturedNumbersAndLegacyMagicEligibility)
{
	specialtyHero = "core:nimbus";
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto & hero = *attackerSideHero->getHeroType();
	const auto & rules = attackerSideHero->getPrimaryGrowthRules();
	const auto current = newHorizonsHeroes::resolveSpecialtyPresentation(hero, rules,
		attackerSideHero->getMagicRules(), std::nullopt, attackerSideHero);
	EXPECT_EQ(current.name, "Grave Archivist");
	EXPECT_NE(current.description.find("5% per hero level"), std::string::npos);
	EXPECT_NE(current.description.find("1 additional distinct"), std::string::npos);
	EXPECT_THROW(newHorizonsHeroes::resolveSpecialtyPresentation(hero, rules, JsonNode(),
		std::nullopt, attackerSideHero), std::invalid_argument);
	auto absent = rules;
	absent["additionalSpecialties"].Struct().erase("graveArchivist");
	const auto historical = newHorizonsHeroes::resolveSpecialtyPresentation(hero, absent,
		attackerSideHero->getMagicRules(), std::nullopt);
	EXPECT_NE(historical.name, "Grave Archivist");
}

TEST_F(NewHorizonsEagleEyeTest, NimbusCapturedLegacyMagicPresentationKeepsAdditionalLearningInactive)
{
	specialtyHero = "core:nimbus";
	legacyMagic = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(attackerSideHero->getMagicRules().isNull());
	const auto legacy = newHorizonsHeroes::resolveSpecialtyPresentation(*attackerSideHero->getHeroType(),
		attackerSideHero->getPrimaryGrowthRules(), attackerSideHero->getMagicRules(),
		std::nullopt, attackerSideHero);
	EXPECT_EQ(legacy.name, "Grave Archivist");
	EXPECT_NE(legacy.description.find("inactive under this game's legacy magic rules"), std::string::npos);
}
