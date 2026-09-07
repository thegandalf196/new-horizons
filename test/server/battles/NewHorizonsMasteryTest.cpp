/*
 * NewHorizonsMasteryTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/entities/hero/NewHorizonsMasteryEffects.h"
#include "../../../lib/entities/hero/CHeroHandler.h"
#include "../../../lib/entities/building/TownFortifications.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/networkPacks/PacksForClient.h"
#include "../../../lib/networkPacks/PacksForServer.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/gameState/TavernHeroesPool.h"
#include "../../../lib/gameState/CGameStateCampaign.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/rmg/CMapGenOptions.h"
#include "../../../lib/mapObjects/MiscObjects.h"
#include "../../../lib/mapObjects/ObjectTemplate.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/mapObjects/TownBuildingInstance.h"
#include "../../../lib/mapping/CCastleEvent.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../lib/callback/IClient.h"
#include "../../../server/queries/MapQueries.h"
#include "../../../server/queries/QueriesProcessor.h"
#include "../../../server/ServerNetPackVisitors.h"
#include "../../../lib/callback/AIFactory.h"
#include "../../../lib/callback/CGlobalAI.h"
#include "../../../lib/callback/CCallback.h"
#include "../../../lib/serializer/CTypeList.h"
#include "../../../lib/campaign/CampaignState.h"
#include "../../../lib/texts/CGeneralTextHandler.h"
#include <cstdlib>
#include <future>
#include <chrono>

using namespace newHorizonsHeroes;

class NewHorizonsMasteryTest : public HeroCommandFixture
{
protected:
	bool enabled = true;
	void mapLoaded(CMap * map) override
	{
		useCommands = false;
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, JsonNode());
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_MASTERIES,
			enabled ? JsonNode(JsonPath::builtin("config/newHorizonsMasteries")) : JsonNode());
	}

	void train(int artilleryRank = MasteryLevel::EXPERT, bool ready = true)
	{
		for(SecondarySkill skill : {SecondarySkill::ARTILLERY, SecondarySkill::BALLISTICS,
			SecondarySkill::FIRST_AID, SecondarySkill::LOGISTICS, SecondarySkill::PATHFINDING,
			SecondarySkill::SCOUTING, SecondarySkill::NAVIGATION, SecondarySkill::DIPLOMACY})
			gameHandler->changeSecSkill(attackerSideHero, skill,
				skill == SecondarySkill::ARTILLERY ? artilleryRank : MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		if(ready)
			gameHandler->onAdvInterfaceReady(attackerSideHero->getOwner());
	}

	void levelAndReply()
	{
		attackerSideHero->setExperience(LIBRARY->heroh->reqExp(attackerSideHero->level + 1), ChangeValueMode::ABSOLUTE);
		gameHandler->levelUpHero(attackerSideHero);
		const auto query = std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(gameHandler->queries->topQuery(attackerSideHero->getOwner()));
		ASSERT_TRUE(query);
		ASSERT_TRUE(query->prompted);
		EXPECT_FALSE(attackerSideHero->getMasteryState().pending);
		// All other skills are Expert; Advanced Artillery must be the sole upgrade.
		if(!query->hlu.skills.empty())
		{
			ASSERT_EQ(query->hlu.skills.size(), 1u);
			ASSERT_EQ(query->hlu.skills.front(), SecondarySkill::ARTILLERY);
		}
		ASSERT_TRUE(gameHandler->queryReply(query->queryID, 0, attackerSideHero->getOwner()));
	}

	void choose(MasteryEffect effect)
	{
		const auto query = std::dynamic_pointer_cast<CHeroMasteryDialogQuery>(gameHandler->queries->topQuery(attackerSideHero->getOwner()));
		ASSERT_TRUE(query);
		ASSERT_TRUE(attackerSideHero->getMasteryState().pending);
		const auto offer = *attackerSideHero->getMasteryState().pending;
		const auto option = std::find_if(offer.options.begin(), offer.options.end(), [effect](const auto & entry) { return entry.effect == effect; });
		ASSERT_NE(option, offer.options.end());
		ASSERT_TRUE(gameHandler->heroMasteryReply(query->queryID, offer.hero, offer.sequence,
			static_cast<int>(option - offer.options.begin()), offer.player));
	}

	void reach(const CStack * unit)
	{
		const auto round = battle()->getRound();
		for(size_t i = 0; i < battle()->stacks.size() * 2; ++i)
		{
			const auto * active = battle()->battleActiveUnit();
			ASSERT_NE(active, nullptr);
			if(active->unitId() == unit->unitId())
				return;
			ASSERT_EQ(battle()->getRound(), round);
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), active->unitOwner(), BattleAction::makeDefend(active)));
		}
		FAIL() << "Unit not reached through actual turn queue";
	}
};

TEST_F(NewHorizonsMasteryTest, SecondaryThenMasteryRejectsGenericForeignStaleAndInvalidReplies)
{
	startGame();
	train();
	levelAndReply();
	const auto query = std::dynamic_pointer_cast<CHeroMasteryDialogQuery>(gameHandler->queries->topQuery(PlayerColor(0)));
	ASSERT_TRUE(query);
	ASSERT_TRUE(attackerSideHero->getMasteryState().pending);
	const auto offer = *attackerSideHero->getMasteryState().pending;
	EXPECT_FALSE(gameHandler->queryReply(query->queryID, 0, offer.player));
	EXPECT_FALSE(gameHandler->heroMasteryReply(QueryID(-1), offer.hero, offer.sequence, 0, offer.player));
	EXPECT_FALSE(gameHandler->heroMasteryReply(query->queryID, defenderSideHero->id, offer.sequence, 0, offer.player));
	EXPECT_FALSE(gameHandler->heroMasteryReply(query->queryID, offer.hero, offer.sequence, 0, PlayerColor(1)));
	EXPECT_FALSE(gameHandler->heroMasteryReply(query->queryID, offer.hero, offer.sequence + 1, 0, offer.player));
	for(int choice : {-1, 3})
		EXPECT_FALSE(gameHandler->heroMasteryReply(query->queryID, offer.hero, offer.sequence, choice, offer.player));
	EXPECT_EQ(gameHandler->queries->topQuery(offer.player), query);
	EXPECT_TRUE(attackerSideHero->getMasteryState().selected.empty());
	ASSERT_EQ(server.masteryDialogSawPending.size(), 1u);
	EXPECT_TRUE(server.masteryDialogSawPending.front());
	choose(MasteryEffect::ARTILLERY_VOLLEY);
	EXPECT_EQ(server.progressionPackets, (std::vector<std::string>{"level", "resolved", "offer", "dialog", "chosen", "resolved"}));
	EXPECT_EQ(attackerSideHero->getSecSkillLevel(SecondarySkill::ARTILLERY), MasteryLevel::EXPERT);
	EXPECT_FALSE(gameHandler->heroMasteryReply(query->queryID, offer.hero, offer.sequence, 0, offer.player));
	ASSERT_EQ(attackerSideHero->getMasteryView()->choices.size(), 1u);
	EXPECT_TRUE(attackerSideHero->getMasteryView()->choices.front().active);
}

TEST_F(NewHorizonsMasteryTest, NewlyExpertWaitsUntilNextActualLevel)
{
	startGame();
	train(MasteryLevel::ADVANCED);
	levelAndReply();
	EXPECT_EQ(attackerSideHero->level, 2u);
	EXPECT_EQ(attackerSideHero->getSecSkillLevel(SecondarySkill::ARTILLERY), MasteryLevel::EXPERT);
	EXPECT_FALSE(gameHandler->queries->topQuery(PlayerColor(0)));
	EXPECT_FALSE(attackerSideHero->getMasteryState().pending);
	EXPECT_EQ(attackerSideHero->getMasteryView()->eligibleNextLevel, (std::vector<SecondarySkill>{SecondarySkill::ARTILLERY}));
	levelAndReply();
	ASSERT_TRUE(attackerSideHero->getMasteryState().pending);
	EXPECT_EQ(attackerSideHero->getMasteryState().pending->level, 3u);
	choose(MasteryEffect::ARTILLERY_PRECISION);
}

TEST_F(NewHorizonsMasteryTest, DeferredLevelDialogCannotReclassifyExpertGainedAfterTheLevelRequest)
{
	startGame();
	train(MasteryLevel::ADVANCED, false);
	attackerSideHero->setExperience(LIBRARY->heroh->reqExp(2), ChangeValueMode::ABSOLUTE);
	gameHandler->levelUpHero(attackerSideHero);
	const auto query = std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(gameHandler->queries->topQuery(PlayerColor(0)));
	ASSERT_TRUE(query);
	ASSERT_FALSE(query->prompted);
	EXPECT_FALSE(query->hlu.artilleryExpertBeforeGain);
	// Another reward can arrive while the level dialog waits for readiness.
	gameHandler->changeSecSkill(attackerSideHero, SecondarySkill::ARTILLERY, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	gameHandler->onAdvInterfaceReady(PlayerColor(0));
	ASSERT_TRUE(query->prompted);
	ASSERT_TRUE(gameHandler->queryReply(query->queryID, 0, PlayerColor(0)));
	EXPECT_FALSE(attackerSideHero->getMasteryState().pending);
	EXPECT_FALSE(gameHandler->queries->topQuery(PlayerColor(0)));
	EXPECT_EQ(attackerSideHero->getMasteryView()->eligibleNextLevel, (std::vector<SecondarySkill>{SecondarySkill::ARTILLERY}));
}

TEST_F(NewHorizonsMasteryTest, RankLossIsDormantAndRestorationDoesNotDuplicateChosenBonuses)
{
	startGame();
	train();
	levelAndReply();
	choose(MasteryEffect::ARTILLERY_VOLLEY);
	const auto subtype = BonusSubtypeID(CreatureID(CreatureID::BALLISTA));
	const auto before = attackerSideHero->valOfBonuses(BonusType::HERO_GRANTS_ATTACKS, subtype);
	attackerSideHero->refreshMasteryBonuses();
	attackerSideHero->refreshMasteryBonuses();
	EXPECT_EQ(attackerSideHero->valOfBonuses(BonusType::HERO_GRANTS_ATTACKS, subtype), before);
	gameHandler->changeSecSkill(attackerSideHero, SecondarySkill::ARTILLERY, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	EXPECT_FALSE(attackerSideHero->getMasteryView()->choices.front().active);
	EXPECT_EQ(attackerSideHero->valOfBonuses(BonusType::HERO_GRANTS_ATTACKS, subtype), 0);
	gameHandler->changeSecSkill(attackerSideHero, SecondarySkill::ARTILLERY, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	EXPECT_TRUE(attackerSideHero->getMasteryView()->choices.front().active);
	EXPECT_EQ(attackerSideHero->valOfBonuses(BonusType::HERO_GRANTS_ATTACKS, subtype), before);
	EXPECT_EQ(attackerSideHero->getMasteryState().selected.size(), 1u);
}

TEST_F(NewHorizonsMasteryTest, ChainedExperienceWaitsForMasteryBeforeNextLevel)
{
	startGame();
	train();
	attackerSideHero->setExperience(LIBRARY->heroh->reqExp(3), ChangeValueMode::ABSOLUTE);
	gameHandler->levelUpHero(attackerSideHero);
	const auto normal = gameHandler->queries->topQuery(PlayerColor(0));
	ASSERT_TRUE(normal);
	ASSERT_TRUE(gameHandler->queryReply(normal->queryID, 0, PlayerColor(0)));
	ASSERT_TRUE(attackerSideHero->getMasteryState().pending);
	EXPECT_EQ(attackerSideHero->level, 2u);
	choose(MasteryEffect::ARTILLERY_VOLLEY);
	EXPECT_EQ(attackerSideHero->level, 3u);
	const auto next = gameHandler->queries->topQuery(PlayerColor(0));
	ASSERT_TRUE(next);
	ASSERT_EQ(next->getType(), CHeroLevelUpDialogQuery::TYPE);
	ASSERT_TRUE(gameHandler->queryReply(next->queryID, 0, PlayerColor(0)));
	EXPECT_FALSE(gameHandler->queries->topQuery(PlayerColor(0)));
	EXPECT_EQ(std::count(server.progressionPackets.begin(), server.progressionPackets.end(), "offer"), 1);
}

TEST_F(NewHorizonsMasteryTest, FullWorldPendingAndSelectedSavesRetainIndependentIdentityAndEffects)
{
	startGame();
	train();
	levelAndReply();
	CMemorySerializer memory;
	memory.oser & *gameState();
	CGameState restored;
	memory.iser.cb = &restored;
	memory.iser.loadingGamestate = true;
	memory.iser & restored;
	const auto * hero = restored.getHero(attackerSideHero->id);
	ASSERT_NE(hero, nullptr);
	ASSERT_TRUE(hero->getMasteryState().pending);
	EXPECT_EQ(hero->getMasteryState().pending->options, attackerSideHero->getMasteryState().pending->options);
	EXPECT_EQ(restored.getHeroMasteryRules(), gameState()->getHeroMasteryRules());
	EXPECT_FALSE(hero->getPrimaryGrowthView());
	EXPECT_FALSE(hero->getLeadershipCapacity());
	choose(MasteryEffect::ARTILLERY_VOLLEY);
	CMemorySerializer chosenMemory;
	chosenMemory.oser & *gameState();
	CGameState chosen;
	chosenMemory.iser.cb = &chosen;
	chosenMemory.iser.loadingGamestate = true;
	chosenMemory.iser & chosen;
	ASSERT_EQ(chosen.getHero(attackerSideHero->id)->getMasteryState().selected.size(), 1u);
	EXPECT_EQ(chosen.getHero(attackerSideHero->id)->valOfBonuses(BonusType::HERO_GRANTS_ATTACKS,
		BonusSubtypeID(CreatureID(CreatureID::BALLISTA))), attackerSideHero->valOfBonuses(BonusType::HERO_GRANTS_ATTACKS,
		BonusSubtypeID(CreatureID(CreatureID::BALLISTA))));
}

TEST_F(NewHorizonsMasteryTest, LoadedPendingChoiceIsExposedWithoutApplyingAnotherLevelOrOffer)
{
	startGame();
	train();
	levelAndReply();
	CMemorySerializer memory;
	memory.oser & *gameState();
	auto restored = std::make_shared<CGameState>();
	memory.iser.cb = restored.get();
	memory.iser.loadingGamestate = true;
	memory.iser & *restored;
	RecordingGameServer resumed;
	resumed.gameState = restored;
	CGameHandler handler(resumed, restored);
	handler.onAdvInterfaceReady(PlayerColor(0));
	const auto query = handler.queries->topQuery(PlayerColor(0));
	ASSERT_TRUE(query);
	ASSERT_EQ(query->getType(), CHeroMasteryDialogQuery::TYPE);
	const auto * hero = restored->getHero(attackerSideHero->id);
	ASSERT_TRUE(hero->getMasteryState().pending);
	EXPECT_EQ(hero->level, 2u);
	EXPECT_EQ(resumed.progressionPackets, (std::vector<std::string>{"dialog"}));
	ASSERT_TRUE(handler.heroMasteryReply(query->queryID, hero->id, hero->getMasteryState().pending->sequence, 1, PlayerColor(0)));
	EXPECT_FALSE(handler.queries->topQuery(PlayerColor(0)));
	ASSERT_EQ(hero->getMasteryView()->choices.size(), 1u);
	EXPECT_EQ(hero->getMasteryView()->choices.front().selection.option.effect, MasteryEffect::ARTILLERY_PRECISION);
}

TEST_F(NewHorizonsMasteryTest, OldBinaryAndOldCrossoverRemainAbsentUnderMasteryEnabledWorld)
{
	startGame();
	ASSERT_FALSE(gameState()->getHeroMasteryRules().isNull());
	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::NEW_HORIZONS_CAPABILITIES;
	memory.iser.version = ESerializationVersion::NEW_HORIZONS_CAPABILITIES;
	memory.oser & *gameState();
	CGameState restored;
	memory.iser.cb = &restored;
	memory.iser.loadingGamestate = true;
	memory.iser & restored;
	EXPECT_TRUE(restored.getHeroMasteryRules().isNull());
	EXPECT_FALSE(restored.getHero(attackerSideHero->id)->getMasteryView());
	CampaignState campaign;
	auto node = campaign.crossoverSerialize(attackerSideHero);
	node.Struct().erase("masteryState");
	const auto legacy = campaign.crossoverDeserialize(node, map());
	legacy->initHero(*gameHandler->randomizer);
	EXPECT_FALSE(legacy->getMasteryView());
}

TEST_F(NewHorizonsMasteryTest, PreviewMustResolveAllSixActualTextsAndUseSavedMagnitude)
{
	if(!std::getenv("NH_REQUIRE_MASTERY_TEXTS"))
		GTEST_SKIP() << "Requires actual private mastery-preview translation activation";
	startGame();
	const auto options = masteryOptions(gameState()->getHeroMasteryRules(), SecondarySkill::ARTILLERY);
	ASSERT_TRUE(options);
	for(const auto & option : *options)
	{
		const auto name = LIBRARY->generaltexth->translate(option.nameTextId);
		const auto description = LIBRARY->generaltexth->translate(option.descriptionTextId);
		ASSERT_FALSE(name.empty());
		ASSERT_FALSE(description.empty());
		EXPECT_NE(name, option.nameTextId);
		EXPECT_NE(description, option.descriptionTextId);
		if(option.effect != MasteryEffect::ARTILLERY_PRECISION)
		{
			ASSERT_NE(description.find("{magnitude}"), std::string::npos);
			const auto formatted = formatMasteryDescription(option, description);
			EXPECT_EQ(formatted.find("{magnitude}"), std::string::npos);
			EXPECT_NE(formatted.find(std::to_string(option.magnitude)), std::string::npos);
		}
	}
}

namespace
{
class MasteryEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit MasteryEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class MasteryLoopbackClient final : public IClient
{
	CGameHandler & handler;
public:
	std::weak_ptr<CGlobalAI> ai;
	std::promise<std::pair<int, bool>> completed;
	explicit MasteryLoopbackClient(CGameHandler & handler) : handler(handler) {}
	std::optional<BattleAction> makeSurrenderRetreatDecision(PlayerColor, const BattleID &, const BattleStateInfoForRetreat &) override
	{
		return std::nullopt;
	}
	int sendRequest(const CPackForServer & outgoing, PlayerColor player, bool waitTillRealize) override
	{
		constexpr int request = 17;
		const auto * reply = dynamic_cast<const HeroMasteryReply *>(&outgoing);
		if(!reply || reply->player != player || !waitTillRealize)
			throw std::runtime_error("Unexpected mastery AI request or callback delivery mode");
		// The real final CCallback builds the packet. Only transport is looped back;
		// preserve its full payload and execute the ordinary authoritative visitor.
		HeroMasteryReply pack = *reply;
		pack.requestID = request;
		const auto controller = ai.lock();
		controller->requestSent(&pack, request);
		ApplyGhNetPackVisitor visitor(handler, GameConnectionID(0));
		pack.visitTyped(visitor);
		PackageApplied ack;
		ack.player = player;
		ack.requestID = request;
		ack.packType = CTypeList::getInstance().getTypeID<HeroMasteryReply>(nullptr);
		ack.result = visitor.getResult();
		controller->requestRealized(&ack);
		completed.set_value({pack.choice, visitor.getResult()});
		return request;
	}
};
}

class NewHorizonsMasteryArmyAITest : public NewHorizonsMasteryTest, public ::testing::WithParamInterface<int> {};

TEST_P(NewHorizonsMasteryArmyAITest, ActualNullkillerCallbackValuesArmyThenSubmitsDedicatedAuthoritativeReply)
{
	startGame();
	train();
	const int mode = GetParam();
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName(mode == 1 ? "core:archer" : "core:pikeman"), mode == 2 ? 1 : 100));
	levelAndReply();
	const auto query = gameHandler->queries->topQuery(PlayerColor(0));
	ASSERT_TRUE(query);
	ASSERT_TRUE(attackerSideHero->getMasteryState().pending);
	const auto offer = *attackerSideHero->getMasteryState().pending;
	const auto expected = mode == 0 ? MasteryEffect::ARTILLERY_VOLLEY : mode == 1 ? MasteryEffect::ARTILLERY_PRECISION : MasteryEffect::ARTILLERY_REPAIR;
	auto reordered = offer;
	std::reverse(reordered.options.begin(), reordered.options.end());
	EXPECT_EQ(reordered.options[chooseMasteryForArmy(reordered, *attackerSideHero)].effect, expected);
	const auto transport = std::make_shared<MasteryLoopbackClient>(*gameHandler);
	const auto callback = std::make_shared<CCallback>(gameState(), PlayerColor(0), transport.get());
	const auto ai = AIFactory::createAdventureAI("Nullkiller2");
	transport->ai = ai;
	ai->initGameInterface(std::make_shared<MasteryEnvironment>(gameState()), callback);
	auto result = transport->completed.get_future();
	ai->heroGotMastery(offer, query->queryID);
	const auto ready = result.wait_for(std::chrono::seconds(10));
	ai->finish();
	ASSERT_EQ(ready, std::future_status::ready);
	const auto [choice, accepted] = result.get();
	ASSERT_TRUE(accepted);
	EXPECT_EQ(offer.options[choice].effect, expected);
	ASSERT_EQ(attackerSideHero->getMasteryState().selected.size(), 1u);
	EXPECT_EQ(attackerSideHero->getMasteryState().selected.front().option.effect, expected);
	EXPECT_FALSE(gameHandler->queries->topQuery(PlayerColor(0)));
}

INSTANTIATE_TEST_SUITE_P(EscortRoles, NewHorizonsMasteryArmyAITest, ::testing::Values(0, 1, 2));

class NewHorizonsMasteryExecutionTest : public NewHorizonsMasteryTest, public ::testing::WithParamInterface<bool>
{
protected:
	void prepare(MasteryEffect effect, bool town = false)
	{
		enabled = GetParam();
		startGame(town);
		train();
		if(enabled)
		{
			levelAndReply();
			choose(effect);
		}
		giveArtifact(attackerSideHero, ArtifactID::BALLISTA, ArtifactPosition::MACH1);
	}
};

TEST_P(NewHorizonsMasteryExecutionTest, PrecisionUsesRealDistanceAndFortWallPathsAndExcludesOtherShooters)
{
	prepare(MasteryEffect::ARTILLERY_PRECISION, true);
	const auto towns = gameState()->getPlayerState(PlayerColor(1))->getTowns();
	ASSERT_EQ(towns.size(), 1u);
	ASSERT_GT(towns.front()->fortificationsLevel().wallsHealth, 0);
	startBattle(towns.front());
	const auto machines = battle()->battleGetStacksIf([](const CStack * stack) { return stack->isBallista() && stack->unitSide() == BattleSide::ATTACKER; });
	ASSERT_EQ(machines.size(), 1u);
	const auto * machine = machines.front();
	const auto * archer = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(3, 5), 10);
	const BattleHex target(15, 5);
	EXPECT_EQ(battle()->battleHasDistancePenalty(machine, machine->getPosition(), target), !enabled);
	EXPECT_EQ(battle()->battleHasWallPenalty(machine, machine->getPosition(), target), !enabled);
	EXPECT_TRUE(battle()->battleHasDistancePenalty(archer, archer->getPosition(), target));
	EXPECT_TRUE(battle()->battleHasWallPenalty(archer, archer->getPosition(), target));
}

TEST_P(NewHorizonsMasteryExecutionTest, VolleyExecutesActualAdditionalShotWithoutGivingOtherShootersExtraAttacks)
{
	prepare(MasteryEffect::ARTILLERY_VOLLEY);
	startBattle();
	const auto machines = battle()->battleGetStacksIf([](const CStack * stack) { return stack->isBallista(); });
	ASSERT_EQ(machines.size(), 1u);
	const auto * machine = machines.front();
	const auto * archer = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(3, 5), 10);
	const BattleHex targetHex(14, 5);
	ASSERT_EQ(battle()->battleGetStackByPos(targetHex), nullptr);
	const auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), targetHex, 10000);
	beginCombat();
	reach(machine);
	ASSERT_EQ(battle()->battleGetStackByPos(target->getPosition()), target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeShotAttack(machine, target)));
	const auto shotsBy = [&](const CStack * unit)
	{
		return std::count_if(server.attacks.begin(), server.attacks.end(), [&](const auto & attack) { return attack.shot() && attack.stackAttacking == unit->unitId(); });
	};
	EXPECT_EQ(shotsBy(machine), enabled ? 3 : 2);
	EXPECT_TRUE(target->alive());
	// Archer may already have defended on the route to the machine; start a fresh round.
	endRound();
	reach(archer);
	ASSERT_EQ(battle()->battleGetStackByPos(target->getPosition()), target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeShotAttack(archer, target)));
	EXPECT_EQ(shotsBy(archer), 1);
}

TEST_P(NewHorizonsMasteryExecutionTest, RepairHealsOnlyOwnLivingBallistaOnItsRealActivation)
{
	prepare(MasteryEffect::ARTILLERY_REPAIR);
	gameHandler->changeSecSkill(defenderSideHero, SecondarySkill::ARTILLERY, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	giveArtifact(defenderSideHero, ArtifactID::BALLISTA, ArtifactPosition::MACH1);
	startBattle();
	const auto machines = battle()->battleGetStacksIf([](const CStack * stack) { return stack->isBallista(); });
	ASSERT_EQ(machines.size(), 2u);
	const CStack * own = nullptr;
	const CStack * enemy = nullptr;
	for(const auto * machine : machines)
		(machine->unitSide() == BattleSide::ATTACKER ? own : enemy) = machine;
	ASSERT_NE(own, nullptr);
	ASSERT_NE(enemy, nullptr);
	const auto * troop = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 10);
	for(const auto * unit : {own, enemy, troop})
	{
		StacksInjured injury;
		injury.battleID = BattleID(0);
		auto & attacked = injury.stacks.emplace_back();
		attacked.stackAttacked = unit->unitId();
		attacked.damageAmount = unit == troop ? 1 : 80;
		unit->prepareAttacked(attacked, gameHandler->getRandomGenerator());
		ASSERT_EQ(attacked.killedAmount, 0u);
		gameHandler->sendAndApply(injury);
	}
	beginCombat();
	reach(own);
	EXPECT_EQ(own->getFirstHPleft(), own->getMaxHealth() - (enabled ? 30 : 80));
	EXPECT_EQ(troop->getFirstHPleft(), troop->getMaxHealth() - 1);
	EXPECT_EQ(enemy->getFirstHPleft(), enemy->getMaxHealth() - 80);
}

INSTANTIATE_TEST_SUITE_P(ActiveAndLegacy, NewHorizonsMasteryExecutionTest, ::testing::Bool());
