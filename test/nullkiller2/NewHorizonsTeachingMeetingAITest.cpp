/*
 * NewHorizonsTeachingMeetingAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "nullkiller2/NullkillerTest.h"
#include "AI/Nullkiller2/AIUtility.h"
#include "AI/Nullkiller2/Behaviors/CaptureObjectsBehavior.h"
#include "AI/Nullkiller2/Engine/Nullkiller.h"
#include "AI/Nullkiller2/Engine/PriorityEvaluator.h"
#include "AI/Nullkiller2/Goals/ExecuteHeroChain.h"
#include "AI/Nullkiller2/Pathfinding/AIPathfinder.h"
#include "lib/CPlayerState.h"
#include "lib/GameConstants.h"
#include "lib/GameSettings.h"
#include "lib/callback/IClient.h"
#include "lib/entities/creature/NewHorizonsMusterRules.h"
#include "lib/gameState/CGameState.h"
#include "lib/mapping/CMap.h"
#include "lib/mapObjects/army/CStackInstance.h"
#include "lib/modding/CModHandler.h"
#include "lib/networkPacks/PacksForServer.h"
#include "lib/serializer/CMemorySerializer.h"
#include "lib/spells/CSpellHandler.h"
#include "mock/GameHandlerTestServer.h"
#include "server/CGameHandler.h"
#include <shared_mutex>

namespace
{
const PlayerColor PLAYER(0);
const PlayerColor OTHER(1);
const std::string LEARNING = "new-horizons:learning";
SpellID arrow() { return SpellID(SpellID::decode("core:magicArrow")); }
SecondarySkill skill(const char * key) { return SecondarySkill(SecondarySkill::decode(key)); }

class TeachingMovementLoopback final : public IClient
{
	CGameHandler & handler;
	int requestId = 0;
public:
	std::vector<MoveHero> movements;
	explicit TeachingMovementLoopback(CGameHandler & handler) : handler(handler) {}
	std::optional<BattleAction> makeSurrenderRetreatDecision(PlayerColor,
		const BattleID &, const BattleStateInfoForRetreat &) override { return std::nullopt; }
	int sendRequest(const CPackForServer & request, PlayerColor player, bool) override
	{
		const auto * move = dynamic_cast<const MoveHero *>(&request);
		if(!move)
			throw std::runtime_error("Teaching path must use an ordinary movement request");
		movements.push_back(*move);
		auto incoming = CMemorySerializer::deepCopy(request);
		incoming->player = player;
		incoming->requestID = ++requestId;
		handler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, *incoming);
		return requestId;
	}
};

class NewHorizonsTeachingMeetingAITest : public NullkillerTest
{
protected:
	bool allied = false;
	CGHeroInstance * traveler = nullptr;
	CGHeroInstance * partner = nullptr;
	CGHeroInstance * third = nullptr;
	std::unique_ptr<GameHandlerTestServer> server;
	std::unique_ptr<CGameHandler> handler;
	std::unique_ptr<TeachingMovementLoopback> client;
	std::unique_ptr<NK2AI::AIGateway> gateway;
	void SetUp() override
	{
		NullkillerTest::SetUp();
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
	}
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		if(allied)
		{
			loaded->howManyTeams = 1;
			loaded->players[PLAYER.getNum()].team = TeamID(0);
			loaded->players[OTHER.getNum()].team = TeamID(0);
		}
	}
	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER).playerActive(OTHER)
			.hero({5, 5, 0}, HeroTypeID(0), PLAYER).heroGarrison({{CreatureID(0), 1}})
			.hero({8, 5, 0}, HeroTypeID(1), allied ? OTHER : PLAYER).heroGarrison({{CreatureID(0), 1}})
			.hero({20, 20, 0}, HeroTypeID(2), PLAYER).heroGarrison({{CreatureID(0), 1}})
			.hero({30, 30, 0}, HeroTypeID(3), OTHER).heroGarrison({{CreatureID(0), 1}});
		startWithMap(std::move(builder));
		traveler = findHeroAt({5, 5, 0}); partner = findHeroAt({8, 5, 0}); third = findHeroAt({20, 20, 0});
		ASSERT_NE(traveler, nullptr); ASSERT_NE(partner, nullptr); ASSERT_NE(third, nullptr);
		server = std::make_unique<GameHandlerTestServer>(gameState(), PLAYER);
		handler = std::make_unique<CGameHandler>(*server, gameState());
		gameState()->actingPlayers.insert(PLAYER);
		for(auto * hero : {traveler, partner, third})
		{
			hero->setSecSkillLevel(skill("new-horizons:learning"), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
			hero->setSecSkillLevel(skill("new-horizons:sorceryMagic"), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
			if(!hero->hasSpellbook())
				ASSERT_TRUE(handler->giveHeroNewArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK));
			handler->changeSpells(hero, false, hero->getSpellsInSpellbook());
		}
		handler->setMovePoints(traveler->id, 2000);
		revealMap(PLAYER);
		client = std::make_unique<TeachingMovementLoopback>(*handler);
		gateway = makeGateway(PLAYER, client.get());
	}
	int week() const
	{
		const auto calendar = gateway->cc->getCalendar();
		return newHorizonsMuster::absoluteWeek(calendar.getCurrentDay(), calendar.getDaysInWeek());
	}
	void select(const std::string & perk)
	{
		if(perk == "masterTeacher")
		{
			traveler->applyPerkSelection({LEARNING, LEARNING + ".eagleEye"});
			traveler->applyPerkSelection({LEARNING, LEARNING + ".fieldStudy"});
		}
		traveler->applyPerkSelection({LEARNING, LEARNING + "." + perk});
		ASSERT_TRUE(traveler->hasActivePerk(LEARNING, LEARNING + "." + perk));
	}
	NK2AI::Goals::TGoalVec plans()
	{
		auto * ai = gateway->nullkiller.get();
		ai->heroManager->update(); ai->armyManager->update();
		NK2AI::PathfinderSettings settings; settings.useHeroChain = false;
		ai->pathfinder->updatePaths(ai->getHeroesForPathfinding(), settings);
		ai->memory->visitableObjs = {partner->id};
		ai->memory->alreadyVisited = {partner->id}; // Receipts, not coarse visit memory, govern usefulness.
		ai->objectClusterizer->reset(); ai->objectClusterizer->clusterize();
		return NK2AI::Goals::CaptureObjectsBehavior().decompose(ai);
	}
	std::shared_ptr<NK2AI::Goals::ExecuteHeroChain> selectedPlan()
	{
		std::shared_ptr<NK2AI::Goals::ExecuteHeroChain> chosen;
		float best = -1;
		for(const auto & goal : plans())
		{
			const std::shared_ptr<NK2AI::Goals::AbstractGoal> abstract = goal;
			auto path = std::dynamic_pointer_cast<NK2AI::Goals::ExecuteHeroChain>(abstract);
			if(!path || path->getPath().targetHero != traveler)
				continue;
			const auto score = gateway->nullkiller->priorityEvaluator->evaluate(goal);
			if(score > best) { best = score; chosen = path; }
		}
		return chosen;
	}
	void execute(const std::shared_ptr<NK2AI::Goals::ExecuteHeroChain> & goal)
	{
		// Normal AI execution owns this shared simulation lock; waitTillFree
		// temporarily releases/reacquires it around ordinary movement completion.
		std::shared_lock lock(CGameState::mutex);
		goal->accept(gateway.get());
	}
};
}

TEST_F(NewHorizonsTeachingMeetingAITest, ScholarPlannerExecutesUsefulSpellOnlyMeetingThroughOrdinaryMovement)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(select("scholar"));
	handler->changeSpells(partner, true, {arrow()});
	ASSERT_TRUE(partner->getNewHorizonsScholarSpellFor(*traveler));
	ASSERT_FALSE(traveler->spellbookContainsSpell(arrow()));
	auto goal = selectedPlan(); ASSERT_NE(goal, nullptr);
	// A base ChainActor counts as one participant; exchanges add participants.
	EXPECT_EQ(goal->getPath().exchangeCount, 1);
	const auto snapshotArmy = [](const CCreatureSet * army)
	{
		std::map<SlotID, std::pair<CreatureID, TQuantity>> result;
		for(const auto & [slot, stack] : army->Slots())
			result.emplace(slot, std::make_pair(stack->getCreatureID(), stack->getCount()));
		return result;
	};
	const auto travelerArmy = snapshotArmy(traveler);
	const auto partnerArmy = snapshotArmy(partner);
	ASSERT_FALSE(travelerArmy.empty());
	ASSERT_FALSE(partnerArmy.empty());
	ASSERT_NE(goal->getPath().heroArmy, nullptr);
	EXPECT_EQ(snapshotArmy(goal->getPath().heroArmy), travelerArmy); // No planned reinforcement.
	EXPECT_GT(NK2AI::teachingMeetingReward(gateway->nullkiller.get(), traveler, partner), 0);
	ASSERT_NO_THROW(execute(goal));
	ASSERT_FALSE(client->movements.empty());
	EXPECT_TRUE(traveler->spellbookContainsSpell(arrow()));
	EXPECT_EQ(snapshotArmy(traveler), travelerArmy);
	EXPECT_EQ(snapshotArmy(partner), partnerArmy);
	EXPECT_TRUE(traveler->hasNewHorizonsScholarMeeting(partner->id, week()));
	EXPECT_TRUE(partner->hasNewHorizonsScholarMeeting(traveler->id, week()));
	EXPECT_FALSE(NK2AI::shouldVisit(gateway->nullkiller.get(), traveler, partner));
	EXPECT_EQ(selectedPlan(), nullptr);
}

TEST_F(NewHorizonsTeachingMeetingAITest, MentorPlannerExecutesExperienceOnlyMeetingAndCannotRepeatRecipient)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(select("mentor"));
	traveler->level = 2; partner->level = 1;
	const auto before = partner->exp;
	const auto expected = partner->calculateXp(250 * traveler->level);
	auto goal = selectedPlan(); ASSERT_NE(goal, nullptr);
	ASSERT_NO_THROW(execute(goal));
	ASSERT_FALSE(client->movements.empty());
	EXPECT_EQ(partner->exp, before + expected);
	EXPECT_EQ(traveler->getNewHorizonsLearningMentorRecipients()[0], partner->id);
	EXPECT_FALSE(NK2AI::shouldVisit(gateway->nullkiller.get(), traveler, partner));
	EXPECT_EQ(selectedPlan(), nullptr);
}

TEST_F(NewHorizonsTeachingMeetingAITest, MasterTeacherUsesStandaloneExpertEligibilityAndLeavesOnlyDistinctSecondRecipient)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(select("masterTeacher"));
	ASSERT_FALSE(traveler->hasActivePerk(LEARNING, LEARNING + ".mentor"));
	traveler->level = 2; partner->level = 1; third->level = 1;
	const auto before = partner->exp;
	const auto expected = partner->calculateXp(500 * traveler->level);
	auto goal = selectedPlan(); ASSERT_NE(goal, nullptr);
	ASSERT_NO_THROW(execute(goal));
	EXPECT_EQ(partner->exp, before + expected);
	EXPECT_FALSE(NK2AI::shouldVisit(gateway->nullkiller.get(), traveler, partner));
	EXPECT_GT(NK2AI::teachingMeetingReward(gateway->nullkiller.get(), traveler, third), 0);
	EXPECT_EQ(traveler->getNewHorizonsLearningMentorRecipients()[1], ObjectInstanceID::NONE);
}

TEST_F(NewHorizonsTeachingMeetingAITest, EmptyUnselectedAndEqualLevelPairsNeverCreateBlanketRevisits)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_FALSE(NK2AI::shouldVisit(gateway->nullkiller.get(), traveler, partner));
	EXPECT_EQ(selectedPlan(), nullptr);
	ASSERT_NO_FATAL_FAILURE(select("mentor"));
	traveler->level = partner->level = 2;
	EXPECT_FALSE(NK2AI::shouldVisit(gateway->nullkiller.get(), traveler, partner));
	EXPECT_EQ(selectedPlan(), nullptr);
}

TEST_F(NewHorizonsTeachingMeetingAITest, ConsumedScholarPlanRejectsBeforeMovementEvenWhenExplicitlyRequested)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(select("scholar"));
	handler->changeSpells(partner, true, {arrow()});
	auto goal = selectedPlan(); ASSERT_NE(goal, nullptr);
	traveler->markNewHorizonsScholarMeeting(partner->id, week());
	partner->markNewHorizonsScholarMeeting(traveler->id, week());
	EXPECT_THROW(execute(goal), NK2AI::cannotFulfillGoalException);
	EXPECT_TRUE(client->movements.empty());
	const auto paths = gateway->nullkiller->pathfinder->getPathInfo(partner->visitablePos());
	const auto forced = NK2AI::Goals::CaptureObjectsBehavior::getVisitGoals(paths, gateway->nullkiller.get(), partner, true);
	EXPECT_TRUE(std::ranges::none_of(forced, [](const auto & goal) { return !goal->invalid(); }));
	EXPECT_FALSE(traveler->spellbookContainsSpell(arrow()));
}

TEST_F(NewHorizonsTeachingMeetingAITest, MovedPartnerPlanRejectsBeforeMovementAndDoesNotSpendReceipt)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(select("mentor"));
	traveler->level = 2; partner->level = 1;
	auto goal = selectedPlan(); ASSERT_NE(goal, nullptr);
	handler->changeObjPos(partner->id, partner->pos + int3(1, 0, 0), PLAYER);
	EXPECT_THROW(execute(goal), NK2AI::cannotFulfillGoalException);
	EXPECT_TRUE(client->movements.empty());
	EXPECT_TRUE(traveler->canGrantNewHorizonsLearningMentorTo(*partner, week()));
}

TEST_F(NewHorizonsTeachingMeetingAITest, AlliedOwnerMeetingIsUsefulButOnlyOwnTravelerMayPlanIt)
{
	allied = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(select("scholar"));
	ASSERT_NE(traveler->getOwner(), partner->getOwner());
	ASSERT_NE(gateway->cc->getPlayerRelations(PLAYER, OTHER), PlayerRelations::ENEMIES);
	handler->changeSpells(partner, true, {arrow()});
	EXPECT_TRUE(NK2AI::shouldVisit(gateway->nullkiller.get(), traveler, partner));
	EXPECT_EQ(NK2AI::teachingMeetingReward(gateway->nullkiller.get(), partner, traveler), 0);
	EXPECT_NE(selectedPlan(), nullptr);
}
