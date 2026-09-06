/*
 * HeroCommandTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/SideInBattle.h"
#include "../../../lib/bonuses/Bonus.h"
// Full game-state roundtrips instantiate serializers for the complete object graph.
#include "../../../lib/CPlayerState.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../lib/campaign/CampaignState.h"
#include "../../../lib/gameState/CGameStateCampaign.h"
#include "../../../lib/gameState/TavernHeroesPool.h"
#include "../../../lib/mapObjects/MiscObjects.h"
#include "../../../lib/mapObjects/ObjectTemplate.h"
#include "../../../lib/mapObjects/Quest.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapObjects/TownBuildingInstance.h"
#include "../../../lib/mapping/CCastleEvent.h"
#include "../../../lib/rmg/CMapGenOptions.h"
#include "../../../lib/modding/ModScope.h"
#include "../../../lib/serializer/CMemorySerializer.h"

class HeroCommandTest : public HeroCommandFixture {};

TEST_F(HeroCommandTest, LegacyGameHasNoCommands)
{
	useCommands = false;
	prepareCommands();
	EXPECT_FALSE(battle()->battleUsesHeroCommands());
	EXPECT_FALSE(issue(HeroCommand::CHARGE));
	EXPECT_TRUE(gameState()->getHeroCommandRules().isNull());
}

TEST_F(HeroCommandTest, ChargeChangesRealDamageWithoutManaOrCreatureTurn)
{
	prepareCommands();
	auto * from = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * to = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);
	const auto active = battle()->getActiveStackID();
	const auto mana = attackerSideHero->mana;
	const auto before = battle()->calculateDmgRange(BattleAttackInfo(from, to, 0, false)).damage.min;
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_GT(battle()->calculateDmgRange(BattleAttackInfo(from, to, 0, false)).damage.min, before);
	EXPECT_EQ(attackerSideHero->mana, mana);
	EXPECT_EQ(battle()->getActiveStackID(), active);
	EXPECT_EQ(battle()->battleGetActiveOrder(BattleSide::ATTACKER), HeroCommand::CHARGE);
	EXPECT_FALSE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::ADVANCE));
	advanceRound();
	EXPECT_EQ(battle()->battleGetActiveOrder(BattleSide::ATTACKER), HeroCommand::NONE);
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(from, to, 0, false)).damage.min, before);
}

TEST_F(HeroCommandTest, HoldTheLineReducesRealIncomingPhysicalDamage)
{
	prepareCommands();
	auto * ours = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);
	const auto before = battle()->calculateDmgRange(BattleAttackInfo(enemy, ours, 0, false)).damage.min;
	ASSERT_TRUE(issue(HeroCommand::HOLD_THE_LINE));
	EXPECT_LT(battle()->calculateDmgRange(BattleAttackInfo(enemy, ours, 0, false)).damage.min, before);
	advanceRound();
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(enemy, ours, 0, false)).damage.min, before);
}

TEST_F(HeroCommandTest, AdvanceChangesMovementRangeAndExpires)
{
	prepareCommands();
	const auto * active = battle()->battleActiveUnit();
	const auto before = active->getMovementRange();
	ASSERT_TRUE(issue(HeroCommand::ADVANCE));
	EXPECT_GT(active->getMovementRange(), before);
	advanceRound();
	EXPECT_EQ(active->getMovementRange(), before);
}

TEST_F(HeroCommandTest, DoctrinePersistsAcrossRoundsAndSwitchDoesNotStack)
{
	prepareCommands();
	ASSERT_TRUE(issue(HeroCommand::AGGRESSIVE));
	advanceRound();
	EXPECT_EQ(battle()->battleGetActiveDoctrine(BattleSide::ATTACKER), HeroCommand::AGGRESSIVE);
	const auto starts = server.startedActions.size();
	EXPECT_FALSE(issue(HeroCommand::AGGRESSIVE));
	EXPECT_FALSE(issue(HeroCommand::NONE));
	EXPECT_EQ(server.startedActions.size(), starts);
	ASSERT_TRUE(issue(HeroCommand::DEFENSIVE));
	EXPECT_EQ(battle()->battleGetActiveDoctrine(BattleSide::ATTACKER), HeroCommand::DEFENSIVE);
	const auto effects = battle()->battleActiveUnit()->getAllBonuses(Selector::sourceTypeSel(BonusSource::HERO_COMMAND));
	ASSERT_EQ(effects->size(), 2u);
	for(const auto & effect : *effects)
		EXPECT_NE(effect->type, BonusType::PERCENTAGE_DAMAGE_BOOST);
}

TEST_F(HeroCommandTest, WrongSideTargetsAndInvalidIdentifierAreRejectedBeforeState)
{
	prepareCommands();
	const auto starts = server.startedActions.size();
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE)));
	EXPECT_FALSE(issue(static_cast<HeroCommand>(127)));
	auto malformed = BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE);
	malformed.aimToUnit(battle()->battleActiveUnit());
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), malformed));
	EXPECT_EQ(server.startedActions.size(), starts);
	EXPECT_FALSE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
	EXPECT_TRUE(issue(HeroCommand::CHARGE));
}

class HeroActionBudgetTest : public HeroCommandFixture, public ::testing::WithParamInterface<std::tuple<int, int>> {};

TEST_P(HeroActionBudgetTest, EverySecondSpellOrderDoctrineCombinationIsRejected)
{
	prepareCommands(true);
	const auto [first, second] = GetParam();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), heroAction(first)));
	const auto mana = attackerSideHero->mana;
	const auto starts = server.startedActions.size();
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), heroAction(second)));
	EXPECT_EQ(attackerSideHero->mana, mana);
	EXPECT_EQ(server.startedActions.size(), starts);
	advanceRound();
	EXPECT_TRUE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::ADVANCE));
}

INSTANTIATE_TEST_SUITE_P(AllNine, HeroActionBudgetTest,
	::testing::Combine(::testing::Values(0, 1, 2), ::testing::Values(0, 1, 2)));

TEST_F(HeroCommandTest, BattleSideAndActionRoundTripAndOldSideDefaults)
{
	prepareCommands();
	ASSERT_TRUE(issue(HeroCommand::AGGRESSIVE));
	CMemorySerializer memory;
	auto action = BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::DEFENSIVE);
	memory.oser & battle()->getSide(BattleSide::ATTACKER);
	memory.oser & action;
	SideInBattle restored(gameState().get());
	BattleAction decoded;
	memory.iser & restored;
	memory.iser & decoded;
	EXPECT_TRUE(restored.heroCommandUsed);
	EXPECT_EQ(restored.activeDoctrine, HeroCommand::AGGRESSIVE);
	EXPECT_EQ(decoded.command, HeroCommand::DEFENSIVE);
	EXPECT_EQ(decoded.actionType, EActionType::HERO_COMMAND);

	CMemorySerializer legacy;
	legacy.oser.version = ESerializationVersion::TOWN_CUSTOM_INITIAL_GARRISON;
	legacy.iser.version = ESerializationVersion::TOWN_CUSTOM_INITIAL_GARRISON;
	SideInBattle old(gameState().get());
	legacy.oser & old;
	legacy.iser & restored;
	EXPECT_FALSE(restored.heroCommandUsed);
	EXPECT_EQ(restored.activeDoctrine, HeroCommand::NONE);
	EXPECT_EQ(restored.activeOrder, HeroCommand::NONE);
}

TEST_F(HeroCommandTest, PerGameRulesSnapshotRoundTripsAndOldMissingRulesStayLegacy)
{
	startGame();
	ASSERT_EQ(gameState()->getHeroCommandRules()["rulesetVersion"].Integer(), 1);
	for(auto version : {ESerializationVersion::CURRENT, ESerializationVersion::TOWN_CUSTOM_INITIAL_GARRISON})
	{
		SCOPED_TRACE(static_cast<int>(version));
		CMemorySerializer memory;
		memory.oser.version = version;
		memory.iser.version = version;
		ASSERT_NO_THROW(memory.oser & *gameState());
		CGameState restored;
		memory.iser.cb = &restored;
		ASSERT_NO_THROW(memory.iser & restored);
		if(version == ESerializationVersion::CURRENT)
			EXPECT_EQ(restored.getHeroCommandRules(), gameState()->getHeroCommandRules());
		else
			EXPECT_TRUE(restored.getHeroCommandRules().isNull());
	}
}

TEST(HeroCommandRulesTest, NamedSettingsArrayLoadsRealContent)
{
	GameSettings settings;
	JsonNode files;
	files.Vector().emplace_back("config/newHorizonsCombat");
	files.setModScope(ModScope::scopeBuiltin());
	settings.loadBase(files);
	const auto & rules = settings.getValue(EGameSettings::COMBAT_HERO_COMMANDS);
	EXPECT_EQ(rules["rulesetVersion"].Integer(), 1);
	EXPECT_NO_THROW(heroCommands::validateRules(rules));
}

TEST(HeroCommandRulesTest, FormulaIsCoefficientBasedAndUnknownRulesFailClosed)
{
	const JsonNode file(JsonPath::builtin("config/newHorizonsCombat"));
	auto rules = file["combat"]["heroCommands"];
	EXPECT_EQ(heroCommands::coefficient(rules["commands"]["charge"]["effects"]["meleeDamagePercent"], 20, 0), 30);
	rules["rulesetVersion"].Integer() = 2;
	EXPECT_THROW(heroCommands::validateRules(rules), std::runtime_error);
}
