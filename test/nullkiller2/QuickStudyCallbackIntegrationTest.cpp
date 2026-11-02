/* Quick Study callback/ACK integration; VCMI / New Horizons, GPL v2 or later. */
#include "StdInc.h"
#include "nullkiller2/NullkillerTest.h"
#include "AI/Nullkiller2/Analyzers/HeroManager.h"
#include "AI/Nullkiller2/Engine/Nullkiller.h"
#include "lib/IGameSettings.h"
#include "lib/callback/CCallback.h"
#include "lib/callback/GameRandomizer.h"
#include "lib/callback/IClient.h"
#include "lib/entities/hero/CHeroHandler.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/modding/CModHandler.h"
#include "lib/networkPacks/PacksForClient.h"
#include "lib/networkPacks/PacksForServer.h"
#include "lib/serializer/CMemorySerializer.h"
#include "lib/serializer/CTypeList.h"
#include "server/CGameHandler.h"
#include "server/ServerNetPackVisitors.h"
#include "server/queries/MapQueries.h"
#include "server/queries/QueriesProcessor.h"
#include "mock/GameHandlerTestServer.h"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>

namespace
{
const PlayerColor OWNER(0);
constexpr auto LEARNING = "new-horizons:learning";
constexpr auto QUICK = "new-horizons:learning.quickStudy";

// Transport queues real CCallback requests; the test thread drives the authority.
// This prevents a test-only recursive server invocation under the AI state lock.
class QuickStudyClient final : public IClient
{
	std::mutex mutex;
	std::condition_variable ready;
	std::deque<QueryReply> requests;
	int sequence = 0;
	bool invalidRequest = false;
public:
	NK2AI::AIGateway * gateway = nullptr;
	std::optional<BattleAction> makeSurrenderRetreatDecision(
		PlayerColor, const BattleID &, const BattleStateInfoForRetreat &) override { return std::nullopt; }
	int sendRequest(const CPackForServer & outgoing, PlayerColor player, bool) override
	{
		const auto * reply = dynamic_cast<const QueryReply *>(&outgoing);
		std::lock_guard lock(mutex);
		if(!reply || !gateway || reply->player != player)
		{
			invalidRequest = true;
			ready.notify_all();
			return -1;
		}
		auto copy = *reply;
		copy.requestID = ++sequence;
		gateway->requestSent(&copy, copy.requestID);
		requests.push_back(copy);
		ready.notify_all();
		return copy.requestID;
	}
	std::optional<QueryReply> take(std::chrono::milliseconds timeout = std::chrono::seconds(10))
	{
		std::unique_lock lock(mutex);
		if(!ready.wait_for(lock, timeout, [this] { return invalidRequest || !requests.empty(); }) || invalidRequest)
			return std::nullopt;
		auto result = requests.front();
		requests.pop_front();
		return result;
	}
};

class QuickStudyServer final : public GameHandlerTestServer
{
public:
	using GameHandlerTestServer::GameHandlerTestServer;
	std::vector<HeroLevelUp> offers;
	std::function<void(const HeroLevelUp &)> deliver;
	void applyPack(CPackForClient & pack) override
	{
		GameHandlerTestServer::applyPack(pack);
		if(const auto * offer = dynamic_cast<const HeroLevelUp *>(&pack))
		{
			offers.push_back(*offer);
			if(deliver)
				deliver(*offer);
		}
	}
};

class QuickStudyCallbackIntegrationTest : public NullkillerTest
{
protected:
	CGHeroInstance * hero = nullptr;
	std::unique_ptr<QuickStudyServer> server;
	std::unique_ptr<CGameHandler> handler;
	QuickStudyClient client;
	std::unique_ptr<NK2AI::AIGateway> gateway;
	std::shared_ptr<CHeroLevelUpDialogQuery> query;
	void SetUp() override
	{
		NullkillerTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the coherent future optional-Quick profile";
	}
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_MASTERIES, JsonNode());
		loaded->overrideGameSetting(EGameSettings::LEVEL_UP_TOTAL_SKILLS_AMOUNT, JsonNode(0));
	}
	void TearDown() override
	{
		if(gateway)
			gateway->finish();
		gateway.reset();
		client.gateway = nullptr;
		NullkillerTest::TearDown();
	}
	void setRank(const std::string & id, int rank)
	{
		hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(id)), rank, ChangeValueMode::ABSOLUTE);
	}
	std::vector<std::byte> rngBytes() const
	{
		CMemorySerializer snapshot;
		snapshot.oser & *handler->randomizer;
		return snapshot.extractBuffer();
	}
	void dispatch(const HeroLevelUp & offer)
	{
		auto skills = offer.skills;
		gateway->heroGotLevelWithReroll(hero, offer.primskill, skills, offer.perks,
			offer.queryID, offer.optionalRerollAvailable, offer.replacesQueryID, offer.reachedLevel);
	}
	void acknowledge(const QueryReply & request, bool accepted)
	{
		PackageApplied ack;
		ack.player = OWNER;
		ack.requestID = request.requestID;
		ack.packType = CTypeList::getInstance().getTypeID<QueryReply>(nullptr);
		ack.result = accepted;
		gateway->requestRealized(&ack);
	}
	std::optional<int32_t> originalChoice(const HeroLevelUp & offer)
	{
		QuickStudyClient referenceClient;
		auto reference = makeGateway(OWNER, &referenceClient);
		referenceClient.gateway = reference.get();
		auto skills = offer.skills;
		reference->heroGotLevel(hero, offer.primskill, skills, offer.perks, offer.queryID);
		const auto reply = referenceClient.take();
		reference->finish();
		return reply ? reply->reply : std::nullopt;
	}

	bool process(QueryReply request)
	{
		std::unique_lock lock(CGameState::mutex);
		ApplyGhNetPackVisitor visitor(*handler, GameConnectionID::FIRST_CONNECTION);
		request.visitTyped(visitor);
		return visitor.getResult();
	}
	bool chooseWeakInitialSeed()
	{
		// Test-input selection only: use the actual saved sampler to find a weak
		// initial offer. The AI cannot access this loop or the replacement seed.
		for(int seed = 0; seed < 512; ++seed)
		{
			handler->randomizer->setSeed(seed);
			CMemorySerializer snapshot;
			snapshot.oser & *handler->randomizer;
			GameRandomizer probe(*gameState());
			snapshot.iser & probe;
			probe.rollPrimarySkillsForLevelup(hero);
			if(!probe.rollSecondarySkills(hero).empty())
				return false;
			const auto offerSeed = static_cast<uint32_t>(probe.getDefault().nextInt());
			const auto offers = hero->getPerkState().prepareOffer([this](const std::string & id)
				{ return hero->getPerkSkillRank(id); }, offerSeed, hero->getPerkOfferWeightContext());
			if(!offers.empty() && std::all_of(offers.begin(), offers.end(), [](const auto & perk)
				{ return perk.requiredRank == 1; }))
				return true;
		}
		return false;
	}
	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(OWNER).hero({5, 5, 0}, HeroTypeID(0), OWNER)
			.heroGarrison({{CreatureID(0), 10}});
		startWithMap(std::move(builder));
		hero = findHeroAt({5, 5, 0});
		ASSERT_NE(hero, nullptr);
		hero->secSkills.clear();
		auto & state = const_cast<newHorizonsHeroes::PerkState &>(hero->getPerkState());
		state.selected.clear();
		ASSERT_EQ(newHorizonsHeroes::quickStudyRerollInterval(state.rules), 3);
		setRank(LEARNING, 1);
		hero->applyPerkSelection({LEARNING, "new-horizons:learning.mentor"});
		setRank(LEARNING, 2);
		hero->applyPerkSelection({LEARNING, QUICK});
		setRank("new-horizons:offense", 1);
		const auto offense = newHorizonsHeroes::perkOptions(state.rules, "new-horizons:offense");
		const auto basic = std::find_if(offense.begin(), offense.end(), [](const auto & perk)
			{ return perk.effect["status"].String() == "active" && newHorizonsHeroes::perkRequiredRank(perk.requiredRank) == 1; });
		ASSERT_NE(basic, offense.end());
		hero->applyPerkSelection({"new-horizons:offense", basic->id});
		setRank("new-horizons:offense", 2);
		for(const auto * id : {"new-horizons:luck", "new-horizons:mysticism", "new-horizons:wisdom", "new-horizons:logistics"})
			setRank(id, 1);
		hero->level = 5;
		hero->setExperience(LIBRARY->heroh->reqExp(6), ChangeValueMode::ABSOLUTE);
		server = std::make_unique<QuickStudyServer>(gameState(), OWNER);
		handler = std::make_unique<CGameHandler>(*server, gameState());
		gameState()->actingPlayers.insert(OWNER);
		ASSERT_TRUE(chooseWeakInitialSeed()) << "No canonical weak initial offer in bounded test-input search";
		handler->onAdvInterfaceReady(OWNER);
		handler->levelUpHero(hero);
		query = std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(handler->queries->topQuery(OWNER));
		ASSERT_NE(query, nullptr);
		ASSERT_FALSE(server->offers.empty());
		ASSERT_TRUE(query->hlu.optionalRerollAvailable);
		ASSERT_TRUE(query->hlu.skills.empty());
		gateway = makeGateway(OWNER, &client);
		client.gateway = gateway.get();
		gateway->nullkiller->heroManager->update();
		ASSERT_TRUE(gateway->nullkiller->heroManager->shouldRerollLevelUpOffer(
			NK2AI::HeroPtr(hero, gateway->cc.get()), query->hlu.skills, query->hlu.perks, 6));
		server->deliver = [this](const auto & offer) { dispatch(offer); };
	}
};
}

TEST_F(QuickStudyCallbackIntegrationTest, SynchronousRegistrationAndFreshReplacementChooseExactlyOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto initial = server->offers.back();
	const auto rng = rngBytes();
	{
		std::unique_lock lock(gateway->nullkiller->aiStateMutex);
		dispatch(initial);
		EXPECT_EQ(gateway->status.getQueriesCount(), 1);
		dispatch(initial); // Duplicate while the original decision is blocked.
		EXPECT_EQ(gateway->status.getQueriesCount(), 1);
	}
	const auto reroll = client.take();
	ASSERT_TRUE(reroll);
	ASSERT_EQ(reroll->reply, -1);
	EXPECT_EQ(rngBytes(), rng); // The AI decision itself spent no authoritative RNG.
	ASSERT_TRUE(process(*reroll));
	ASSERT_EQ(server->offers.size(), 2u);
	const auto replacement = server->offers.back();
	EXPECT_EQ(replacement.replacesQueryID, initial.queryID);
	EXPECT_NE(replacement.queryID, initial.queryID);
	EXPECT_EQ(gateway->status.getQueriesCount(), 2); // New callback precedes old ACK.
	dispatch(replacement);
	dispatch(initial);
	auto staleConfirmation = initial;
	staleConfirmation.replacesQueryID = initial.queryID;
	dispatch(staleConfirmation);
	acknowledge(*reroll, true);
	EXPECT_EQ(gateway->status.getQueriesCount(), 1);
	const auto selection = client.take();
	ASSERT_TRUE(selection);
	EXPECT_EQ(selection->qid, replacement.queryID);
	ASSERT_TRUE(selection->reply);
	EXPECT_GE(*selection->reply, 0);
	EXPECT_EQ(selection->reply, originalChoice(replacement));
	const auto beforeStale = rngBytes();
	EXPECT_FALSE(handler->queryReply(initial.queryID, 0, OWNER));
	EXPECT_FALSE(handler->queryReply(initial.queryID, -1, OWNER));
	EXPECT_EQ(rngBytes(), beforeStale);
	ASSERT_TRUE(process(*selection));
	acknowledge(*selection, true);
	EXPECT_EQ(gateway->status.getQueriesCount(), 0);
	gateway->finish(); // Drain every worker before checking for any extra command.
	EXPECT_EQ(gateway->status.getQueriesCount(), 0);
	EXPECT_FALSE(client.take(std::chrono::milliseconds(0)));
	EXPECT_EQ(hero->level, 6);
}

TEST_F(QuickStudyCallbackIntegrationTest, AuthoritativeSameIdConfirmationBeforeFalseAckSelectsOriginalOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto initial = server->offers.back();
	dispatch(initial);
	const auto reroll = client.take();
	ASSERT_TRUE(reroll);
	ASSERT_EQ(reroll->reply, -1);
	// Explicit test injection of authority-side availability loss after exposure;
	// no claim that ordinary gameplay currently produces this race.
	query->hlu.optionalRerollAvailable = false;
	const auto rng = rngBytes();
	const auto count = hero->getPerkState().selected.size();
	ASSERT_FALSE(process(*reroll));
	ASSERT_EQ(server->offers.size(), 2u);
	const auto confirmation = server->offers.back();
	EXPECT_EQ(confirmation.queryID, initial.queryID);
	EXPECT_EQ(confirmation.replacesQueryID, initial.queryID);
	EXPECT_EQ(confirmation.perks, initial.perks);
	EXPECT_EQ(confirmation.skills, initial.skills);
	EXPECT_EQ(rngBytes(), rng);
	EXPECT_EQ(hero->level, 6);
	EXPECT_EQ(gateway->status.getQueriesCount(), 1); // No duplicate addQuery.
	const auto selection = client.take(); // The callback arrived before the false ACK.
	ASSERT_TRUE(selection);
	EXPECT_EQ(selection->qid, initial.queryID);
	ASSERT_TRUE(selection->reply);
	EXPECT_GE(*selection->reply, 0);
	EXPECT_EQ(selection->reply, originalChoice(initial));
	dispatch(confirmation);
	acknowledge(*reroll, false);
	EXPECT_EQ(gateway->status.getQueriesCount(), 1);
	ASSERT_TRUE(process(*selection));
	acknowledge(*selection, true);
	EXPECT_EQ(gateway->status.getQueriesCount(), 0);
	EXPECT_EQ(hero->getPerkState().selected.size(), count + 1);
	gateway->finish(); // Drain every worker before checking for any extra command.
	EXPECT_EQ(gateway->status.getQueriesCount(), 0);
	EXPECT_FALSE(client.take(std::chrono::milliseconds(0)));
}

TEST_F(QuickStudyCallbackIntegrationTest, FailedAckAloneNeverSubmitsAnOldQuerySelection)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	dispatch(server->offers.back());
	const auto reroll = client.take();
	ASSERT_TRUE(reroll);
	ASSERT_EQ(reroll->reply, -1);
	const auto rng = rngBytes();
	// A synthetic transport ACK intentionally carries no authoritative offer.
	acknowledge(*reroll, false);
	EXPECT_EQ(gateway->status.getQueriesCount(), 1);
	gateway->finish(); // Drain every worker before checking for any extra command.
	EXPECT_EQ(gateway->status.getQueriesCount(), 1);
	EXPECT_FALSE(client.take(std::chrono::milliseconds(0)));
	EXPECT_EQ(rngBytes(), rng);
	EXPECT_EQ(server->offers.size(), 1u);
}
