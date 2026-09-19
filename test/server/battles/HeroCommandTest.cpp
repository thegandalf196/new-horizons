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

TEST_F(HeroCommandTest, CreatureLocationSpellPacketPreservesUnitZeroAndLanding)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands());
	const auto * source = battle()->battleGetUnitByID(0);
	ASSERT_NE(source, nullptr);
	ASSERT_EQ(source->unitId(), 0u);
	const BattleHex landing(71);

	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::TELEPORT;
	action.aimToHex(BattleHex(88)); // setTarget must discard this stale destination.
	action.setTarget(battle::Target{battle::Destination(source), battle::Destination(landing)});

	CMemorySerializer memory;
	memory.oser & action;
	BattleAction decoded;
	memory.iser & decoded;
	EXPECT_EQ(decoded.actionType, EActionType::HERO_SPELL);
	EXPECT_EQ(decoded.side, BattleSide::ATTACKER);
	EXPECT_EQ(decoded.spell, SpellID::TELEPORT);
	const auto target = decoded.getTarget(battle());
	ASSERT_EQ(target.size(), 2u);
	EXPECT_EQ(target[0].unitValue, source);
	EXPECT_EQ(target[0].hexValue, source->getPosition());
	EXPECT_EQ(target[1].unitValue, nullptr);
	EXPECT_EQ(target[1].hexValue, landing);
	EXPECT_FALSE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
}

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
	EXPECT_FALSE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE));
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

TEST_F(HeroCommandTest, ChargeExpiresAtTheRoundBoundary)
{
	prepareCommands();
	auto * from = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * to = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);
	const auto before = battle()->calculateDmgRange(BattleAttackInfo(from, to, 0, false)).damage.min;
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_GT(battle()->calculateDmgRange(BattleAttackInfo(from, to, 0, false)).damage.min, before);
	advanceRound();
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(from, to, 0, false)).damage.min, before);
}

TEST_F(HeroCommandTest, LegacyDoctrineIdsAreNeverIssuableOrExposed)
{
	prepareCommands();
	for(const auto command : {HeroCommand::AGGRESSIVE, HeroCommand::DEFENSIVE})
	{
		EXPECT_FALSE(heroCommands::supportedByRules(battle()->getHeroCommandRules(), command));
		EXPECT_FALSE(issue(command));
	}
	EXPECT_EQ(battle()->battleGetActiveDoctrine(BattleSide::ATTACKER), HeroCommand::NONE);
	EXPECT_TRUE(battle()->battleActiveUnit()->getAllBonuses(Selector::sourceTypeSel(BonusSource::HERO_COMMAND))->empty());
	EXPECT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_EQ(battle()->battleGetActiveDoctrine(BattleSide::ATTACKER), HeroCommand::NONE);
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

TEST_P(HeroActionBudgetTest, EverySecondSpellOrderCombinationIsRejected)
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
	EXPECT_TRUE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE));
}

INSTANTIATE_TEST_SUITE_P(AllNine, HeroActionBudgetTest,
	::testing::Combine(::testing::Values(0, 1, 2), ::testing::Values(0, 1, 2)));

TEST_F(HeroCommandTest, BattleSideAndActionRoundTripAndOldSideDefaults)
{
	prepareCommands();
	CMemorySerializer memory;
	SideInBattle source(gameState().get());
	source = battle()->getSide(BattleSide::ATTACKER);
	source.heroCommandUsed = true;
	source.activeDoctrine = HeroCommand::AGGRESSIVE;
	auto action = BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::DEFENSIVE);
	memory.oser & source;
	memory.oser & action;
	SideInBattle restored(gameState().get());
	BattleAction decoded;
	memory.iser & restored;
	memory.iser & decoded;
	EXPECT_TRUE(restored.heroCommandUsed);
	EXPECT_EQ(restored.activeDoctrine, HeroCommand::NONE);
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

TEST_F(HeroCommandTest, LegacyAdvanceIdentitySurvivesSideDecodeUntilBattleNormalization)
{
	prepareCommands();
	SideInBattle source(gameState().get());
	source = battle()->getSide(BattleSide::ATTACKER);
	source.heroCommandUsed = true;
	source.activeOrder = HeroCommand::ADVANCE;

	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::HERO_COMMANDS;
	memory.iser.version = ESerializationVersion::HERO_COMMANDS;
	memory.oser & source;
	SideInBattle restored(gameState().get());
	memory.iser & restored;

	EXPECT_TRUE(restored.heroCommandUsed);
	EXPECT_EQ(restored.activeDoctrine, HeroCommand::NONE);
	EXPECT_EQ(restored.activeOrder, HeroCommand::ADVANCE);
}

TEST_F(HeroCommandTest, PerGameRulesSnapshotRoundTripsAndRefuseLossyLegacyWrites)
{
	startGame();
	ASSERT_EQ(gameState()->getHeroCommandRules()["rulesetVersion"].Integer(), heroCommands::ORDERS_ONLY_RULESET_VERSION);
	CMemorySerializer current;
	ASSERT_NO_THROW(current.oser & *gameState());
	CGameState restored;
	current.iser.cb = &restored;
	ASSERT_NO_THROW(current.iser & restored);
	EXPECT_EQ(restored.getHeroCommandRules(), gameState()->getHeroCommandRules());

	CMemorySerializer legacy;
	legacy.oser.version = ESerializationVersion::TOWN_CUSTOM_INITIAL_GARRISON;
	EXPECT_THROW(legacy.oser & *gameState(), std::runtime_error);
}

TEST(HeroCommandRulesTest, NamedSettingsArrayLoadsRealContent)
{
	GameSettings settings;
	JsonNode files;
	files.Vector().emplace_back("config/newHorizonsCombat");
	files.setModScope(ModScope::scopeBuiltin());
	settings.loadBase(files);
	const auto & rules = settings.getValue(EGameSettings::COMBAT_HERO_COMMANDS);
	EXPECT_EQ(rules["rulesetVersion"].Integer(), heroCommands::ORDERS_ONLY_RULESET_VERSION);
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
