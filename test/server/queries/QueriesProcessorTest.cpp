#include "StdInc.h"

#include <gtest/gtest.h>

#include "../../../server/queries/CQuery.h"
#include "../../../server/queries/MapQueries.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../../server/queries/QueriesProcessor.h"
#include "CGameHandler.h"

#include "mock/GameHandlerTestServer.h"
#include "mock/TinyH3MBuilder.h"
#include "mock/TinyMapGameTest.h"

#include "lib/battle/BattleInfo.h"
#include "lib/callback/GameRandomizer.h"
#include "lib/GameConstants.h"
#include "lib/CSkillHandler.h"
#include "lib/entities/hero/NewHorizonsHeroRules.h"
#include "lib/gameState/CGameState.h"
#include "lib/mapObjects/CGDwelling.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/mapping/CMap.h"
#include "lib/networkPacks/PacksForClient.h"
#include "lib/modding/CModHandler.h"

#ifdef ENABLE_NULLKILLER2_AI
#include "../../../lib/callback/AIFactory.h"
#include "../../../lib/callback/CCallback.h"
#include "../../../lib/callback/CGlobalAI.h"
#include "../../../lib/callback/IClient.h"
#include "../../../lib/entities/hero/CHeroHandler.h"
#include "../../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/serializer/CTypeList.h"
#include "../../../server/ServerNetPackVisitors.h"
#include <chrono>
#include <future>
#include <mutex>
#endif

namespace
{

enum class QueryEvent
{
	OnAdding,
	OnAdded,
	OnRemoval,
	OnExposure
};

class TestQuery;

class RecordingQueryServer final : public GameHandlerTestServer
{
public:
	using GameHandlerTestServer::GameHandlerTestServer;

	std::vector<std::string> applied;

	void applyPack(CPackForClient & pack) override
	{
		if(dynamic_cast<const HeroPerkChosen *>(&pack))
			applied.push_back("perk");
		if(dynamic_cast<const QueryResolved *>(&pack))
			applied.push_back("resolved");
		GameHandlerTestServer::applyPack(pack);
	}
};

struct RecordedEvent
{
	TestQuery * query;
	QueryEvent event;
};

class TestQuery : public CQuery
{
public:
	TestQuery(CGameHandler * gh, std::initializer_list<PlayerColor> affectedPlayers, QueryType type)
		: CQuery(gh, type)
	{
		for(auto player : affectedPlayers)
			players.push_back(player);
	}

	TestQuery(CGameHandler * gh, PlayerColor player, QueryType type)
		: CQuery(gh, type)
	{
		players.push_back(player);
	}

	std::vector<RecordedEvent> * sharedEventLog = nullptr;
	std::vector<QueryEvent> events;
	std::vector<PlayerColor> onAddingCalls;
	std::vector<PlayerColor> onAddedCalls;
	std::vector<PlayerColor> onRemovalCalls;
	std::vector<QueryPtr> exposureArgs;
	bool popOnExposure = false;
	bool addReplacementOnRemoval = false;
	QueryPtr replacementQuery;

	void onAdding(PlayerColor color) override
	{
		events.push_back(QueryEvent::OnAdding);
		onAddingCalls.push_back(color);
		if(sharedEventLog)
			sharedEventLog->push_back({this, QueryEvent::OnAdding});
	}

	void onAdded(PlayerColor color) override
	{
		events.push_back(QueryEvent::OnAdded);
		onAddedCalls.push_back(color);
		if(sharedEventLog)
			sharedEventLog->push_back({this, QueryEvent::OnAdded});
	}

	void onRemoval(PlayerColor color) override
	{
		events.push_back(QueryEvent::OnRemoval);
		onRemovalCalls.push_back(color);

		if(addReplacementOnRemoval && replacementQuery)
			owner->addQuery(replacementQuery);

		if(sharedEventLog)
			sharedEventLog->push_back({this, QueryEvent::OnRemoval});
	}

	void onExposure(QueryPtr topQuery) override
	{
		events.push_back(QueryEvent::OnExposure);
		exposureArgs.push_back(topQuery);

		if(sharedEventLog)
			sharedEventLog->push_back({this, QueryEvent::OnExposure});

		if(popOnExposure)
			owner->popIfTop(*this);
	}
};

class QueriesProcessorTest : public ::testing::Test
{
protected:
	GameHandlerTestServer server;
	CGameHandler gh{server};
	QueriesProcessor & queries = *gh.queries;
};

class NeutralDwellingBattleQueryTest : public TinyMapGameTest
{
protected:
	void startGame()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder
			.size(36, false)
			.playerActive(PlayerColor(0))
			.playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0))
			.heroGarrison({{CreatureID(0), 10}})
			.dwelling({7, 5, 0}, MapObjectSubID(0), PlayerColor(1));

		startWithMap(std::move(builder));
	}
};

class NewHorizonsFactionSkillQueryTest : public TinyMapGameTest
{
protected:
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
	}

	void startGame(HeroTypeID heroType = HeroTypeID(16))
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder
			.size(36, false)
			.playerActive(PlayerColor(0))
			.hero({5, 5, 0}, heroType, PlayerColor(0))
			.heroGarrison({{CreatureID(0), 10}});
		startWithMap(std::move(builder));
	}
};

#ifdef ENABLE_NULLKILLER2_AI
class NewHorizonsPerkAITest : public TinyMapGameTest
{
protected:
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_MASTERIES, JsonNode());
	}

	void configurePlayer(PlayerSettings & settings) const override
	{
		if(settings.color == PlayerColor(1))
			settings.connectedPlayerIDs.clear();
	}

	void startGame(HeroTypeID heroType = HeroTypeID(0))
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder
			.size(36, false)
			.playerActive(PlayerColor(0))
			.playerActive(PlayerColor(1))
			.hero({5, 5, 0}, heroType, PlayerColor(1))
			.heroGarrison({{CreatureID(0), 10}});
		startWithMap(std::move(builder));
	}
};

class PerkLevelUpEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit PerkLevelUpEnvironment(std::shared_ptr<CGameState> state)
		: state(std::move(state))
	{
	}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class PerkLevelUpLoopback final : public IClient
{
	CGameHandler & handler;
	std::once_flag completion;

public:
	struct Result
	{
		int choice = -1;
		bool accepted = false;
		std::string error;
	};

	std::weak_ptr<CGlobalAI> ai;
	std::promise<Result> completed;

	explicit PerkLevelUpLoopback(CGameHandler & handler)
		: handler(handler)
	{
	}

	void complete(Result result) noexcept
	{
		try
		{
			std::call_once(completion, [this, result = std::move(result)]() mutable
			{
				try
				{
					completed.set_value(std::move(result));
				}
				catch(...)
				{
					// The test must never let a transport failure escape an AI task.
				}
			});
		}
		catch(...)
		{
			// The test must never let a transport failure escape an AI task.
		}
	}

	std::optional<BattleAction> makeSurrenderRetreatDecision(PlayerColor, const BattleID &, const BattleStateInfoForRetreat &) override
	{
		return std::nullopt;
	}

	int sendRequest(const CPackForServer & outgoing, PlayerColor player, bool waitTillRealize) override
	{
		constexpr int request = 17;
		try
		{
			const auto * reply = dynamic_cast<const QueryReply *>(&outgoing);
			if(!reply || reply->player != player || !waitTillRealize)
				throw std::runtime_error("Unexpected perk AI request");

			QueryReply pack = *reply;
			pack.requestID = request;
			const auto controller = ai.lock();
			if(!controller)
				throw std::runtime_error("Missing perk AI controller");
			controller->requestSent(&pack, request);

			ApplyGhNetPackVisitor visitor(handler, GameConnectionID::FIRST_CONNECTION);
			pack.visitTyped(visitor);
			PackageApplied ack;
			ack.player = player;
			ack.requestID = request;
			ack.packType = CTypeList::getInstance().getTypeID<QueryReply>(nullptr);
			ack.result = visitor.getResult();
			controller->requestRealized(&ack);
			complete({pack.reply.value_or(-1), visitor.getResult(), {}});
		}
		catch(const std::exception & error)
		{
			complete({-1, false, error.what()});
		}
		catch(...)
		{
			complete({-1, false, "unknown perk AI transport failure"});
		}
		return request;
	}
};
#endif

}

TEST_F(QueriesProcessorTest, topQuery_returnsNullWhenPlayerHasNoQueries)
{
	EXPECT_EQ(queries.topQuery(PlayerColor(1)), nullptr);
}

TEST_F(NeutralDwellingBattleQueryTest, ownedDwellingUsesNeutralBattleSideWithoutNeutralQuery)
{
	startGame();

	auto * hero = findHeroByOwner(PlayerColor(0));
	auto * dwelling = findFirst<CGDwelling>();
	ASSERT_NE(hero, nullptr);
	ASSERT_NE(dwelling, nullptr);
	ASSERT_TRUE(dwelling->setCreature(SlotID(0), CreatureID(1), 10));

	GameHandlerTestServer server(gameState());
	CGameHandler gh(server, gameState());

	gh.battles->startBattle(hero, dwelling);

	const auto * battle = gameState()->getBattle(PlayerColor(0));
	ASSERT_NE(battle, nullptr);
	EXPECT_EQ(battle->getSide(BattleSide::DEFENDER).color, PlayerColor::NEUTRAL);

	const auto attackerQuery = gh.queries->topQuery(PlayerColor(0));
	ASSERT_NE(attackerQuery, nullptr);
	EXPECT_EQ(attackerQuery->getType(), QueryType::Battle);
	ASSERT_EQ(attackerQuery->players.size(), 1);
	EXPECT_EQ(attackerQuery->players.front(), PlayerColor(0));
	EXPECT_EQ(gh.queries->topQuery(PlayerColor(1)), nullptr);
}

TEST_F(NeutralDwellingBattleQueryTest, heroLevelUpRejectsForgedChoiceIndicesBeforeRemoval)
{
	startGame();
	const auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	GameHandlerTestServer server(gameState());
	CGameHandler gh(server, gameState());
	HeroLevelUp levelUp;
	levelUp.player = PlayerColor(0);
	levelUp.heroId = hero->id;
	levelUp.skills = {SecondarySkill::ARCHERY, SecondarySkill::LOGISTICS};
	CHeroLevelUpDialogQuery query(&gh, levelUp, hero);

	EXPECT_FALSE(query.isValidReply(std::nullopt));
	EXPECT_FALSE(query.isValidReply(-1));
	EXPECT_TRUE(query.isValidReply(0));
	EXPECT_TRUE(query.isValidReply(1));
	EXPECT_FALSE(query.isValidReply(2));
	const int archeryBefore = hero->getSecSkillLevel(SecondarySkill::ARCHERY);
	auto liveQuery = std::make_shared<CHeroLevelUpDialogQuery>(&gh, levelUp, hero);
	gh.queries->addQuery(liveQuery);
	EXPECT_FALSE(gh.queryReply(liveQuery->queryID, 2, PlayerColor(0)));
	EXPECT_EQ(gh.queries->topQuery(PlayerColor(0)), liveQuery);
	EXPECT_EQ(hero->getSecSkillLevel(SecondarySkill::ARCHERY), archeryBefore);

	levelUp.skills.clear();
	CHeroLevelUpDialogQuery emptyQuery(&gh, levelUp, hero);
	EXPECT_TRUE(emptyQuery.isValidReply(0));
	EXPECT_FALSE(emptyQuery.isValidReply(1));
}

TEST_F(NewHorizonsFactionSkillQueryTest, factionSkillRankUpIsAuthoritativeAndExcludesForeignSkills)
{
	startGame();
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	const auto ownFactionSkill = newHorizonsHeroes::factionSkill(
		hero->getPrimaryGrowthRules(), hero->getFactionID());
	const auto foreignFactionSkill = newHorizonsHeroes::factionSkill(
		hero->getPrimaryGrowthRules(), FactionID::CASTLE);
	ASSERT_TRUE(ownFactionSkill.has_value());
	ASSERT_TRUE(foreignFactionSkill.has_value());
	ASSERT_NE(*ownFactionSkill, *foreignFactionSkill);

	GameHandlerTestServer server(gameState());
	CGameHandler gameHandler(server, gameState());

	// Leave only the hero's own faction skill below Expert. The randomizer must
	// still offer it even though its legacy class probability is zero.
	for(int index = 0; index < LIBRARY->skillh->size(); ++index)
		hero->setSecSkillLevel(SecondarySkill(index), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	hero->setSecSkillLevel(*ownFactionSkill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	hero->setSecSkillLevel(*foreignFactionSkill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	(void)gameHandler.randomizer->rollPrimarySkillForLevelup(hero); // seed the hero-specific RNG
	const auto choices = gameHandler.randomizer->rollSecondarySkills(hero);
	ASSERT_LE(choices.size(), 2u);
	EXPECT_EQ(std::count(choices.begin(), choices.end(), *ownFactionSkill), 1);
	EXPECT_EQ(std::count(choices.begin(), choices.end(), *foreignFactionSkill), 0);

	HeroLevelUp levelUp;
	levelUp.player = PlayerColor(0);
	levelUp.heroId = hero->id;
	levelUp.skills = {*ownFactionSkill, *foreignFactionSkill};
	CHeroLevelUpDialogQuery query(&gameHandler, levelUp, hero);
	EXPECT_TRUE(query.isValidReply(0));
	EXPECT_FALSE(query.isValidReply(1));

	// A stale reply after another authoritative transition must be rejected.
	hero->setSecSkillLevel(*ownFactionSkill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	EXPECT_FALSE(query.isValidReply(0));
}

TEST_F(NewHorizonsFactionSkillQueryTest, canonicalSkillOfferWeightsDriveSelectionAndRejectZeroWeights)
{
	startGame();
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);

	const SecondarySkill wisdom(SecondarySkill::decode("new-horizons:wisdom"));
	const SecondarySkill offense(SecondarySkill::decode("new-horizons:offense"));
	const SecondarySkill logistics(SecondarySkill::decode("new-horizons:logistics"));
	const SecondarySkill spellcraft(SecondarySkill::decode("new-horizons:spellcraft"));
	ASSERT_GE(wisdom.getNum(), 0);
	ASSERT_GE(offense.getNum(), 0);
	ASSERT_GE(logistics.getNum(), 0);
	ASSERT_GE(spellcraft.getNum(), 0);
	ASSERT_TRUE(newHorizonsHeroes::usesSkillOfferWeights(hero->getPrimaryGrowthRules()));
	EXPECT_EQ(newHorizonsHeroes::skillOfferWeight(hero->getPrimaryGrowthRules(), wisdom), 0);
	EXPECT_EQ(newHorizonsHeroes::skillOfferWeight(hero->getPrimaryGrowthRules(), logistics), 9);
	EXPECT_EQ(newHorizonsHeroes::skillOfferWeight(hero->getPrimaryGrowthRules(), spellcraft), 3);

	GameHandlerTestServer server(gameState());
	CGameHandler gameHandler(server, gameState());
	GameRandomizer & randomizer = *gameHandler.randomizer;
	randomizer.setSeed(17);
	const std::set<SecondarySkill> zeroAndPositive = {wisdom, offense};
	for(int i = 0; i < 32; ++i)
		EXPECT_EQ(randomizer.rollSecondarySkillForLevelup(hero, zeroAndPositive), offense);

	int logisticsDraws = 0;
	int spellcraftDraws = 0;
	randomizer.setSeed(31);
	const std::set<SecondarySkill> weighted = {logistics, spellcraft};
	for(int i = 0; i < 64; ++i)
	{
		const auto selected = randomizer.rollSecondarySkillForLevelup(hero, weighted);
		if(selected == logistics)
			++logisticsDraws;
		else if(selected == spellcraft)
			++spellcraftDraws;
		else
			FAIL() << "canonical selection returned an unexpected skill";
	}
	EXPECT_GT(logisticsDraws, spellcraftDraws);

	// A canonical level-up offer is one weighted pool: an owned skill that can
	// advance competes directly with a learnable new skill, and the second draw
	// cannot repeat the first one.
	auto & rules = const_cast<JsonNode &>(hero->getPrimaryGrowthRules());
	for(auto & [skill, weight] : rules["skillOfferWeights"].Struct())
		weight.Integer() = 0;
	rules["skillOfferWeights"][SecondarySkill::encode(offense.getNum())].Integer() = 4;
	rules["skillOfferWeights"][SecondarySkill::encode(spellcraft.getNum())].Integer() = 3;
	for(int index = 0; index < LIBRARY->skillh->size(); ++index)
		hero->setSecSkillLevel(SecondarySkill(index), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	hero->setSecSkillLevel(offense, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	(void)randomizer.rollPrimarySkillForLevelup(hero); // seed the hero-specific RNG
	const auto combined = randomizer.rollSecondarySkills(hero);
	ASSERT_EQ(combined.size(), 2u);
	EXPECT_EQ(std::set<SecondarySkill>(combined.begin(), combined.end()),
		(std::set<SecondarySkill>{offense, spellcraft}));
}

TEST_F(NewHorizonsFactionSkillQueryTest, duplicateLegacyAliasUsesFirstSavedFactionSkillIdentity)
{
	startGame(HeroTypeID(64)); // Death Knight: Necropolis might hero.
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	const auto canonical = newHorizonsHeroes::factionSkill(
		hero->getPrimaryGrowthRules(), hero->getFactionID());
	ASSERT_TRUE(canonical.has_value());
	ASSERT_NE(*canonical, SecondarySkill::NECROMANCY);
	hero->setSecSkillLevel(*canonical, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	hero->setSecSkillLevel(SecondarySkill::NECROMANCY, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);

	GameHandlerTestServer server(gameState());
	CGameHandler gameHandler(server, gameState());
	for(int index = 0; index < LIBRARY->skillh->size(); ++index)
		hero->setSecSkillLevel(SecondarySkill(index), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	hero->setSecSkillLevel(*canonical, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	hero->setSecSkillLevel(SecondarySkill::NECROMANCY, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	(void)gameHandler.randomizer->rollPrimarySkillForLevelup(hero);
	const auto choices = gameHandler.randomizer->rollSecondarySkills(hero);
	ASSERT_EQ(std::count(choices.begin(), choices.end(), *canonical), 1);
	EXPECT_EQ(std::count(choices.begin(), choices.end(), SecondarySkill::NECROMANCY), 0);

	HeroLevelUp levelUp;
	levelUp.player = PlayerColor(0);
	levelUp.heroId = hero->id;
	levelUp.skills = {*canonical, SecondarySkill::NECROMANCY};
	CHeroLevelUpDialogQuery query(&gameHandler, levelUp, hero);
	EXPECT_TRUE(query.isValidReply(0));
	EXPECT_FALSE(query.isValidReply(1));
}

TEST_F(NeutralDwellingBattleQueryTest, heroLevelUpValidatesAndAppliesOnlyTheStoredPerkOffer)
{
	startGame();
	const auto * found = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(found, nullptr);
	auto * hero = gameState()->getHero(found->id);
	ASSERT_NE(hero, nullptr);
	GameHandlerTestServer server(gameState());
	CGameHandler gh(server, gameState());

	const std::string skillId = "new-horizons:offense";
	const int decoded = SecondarySkill::decode(skillId);
	ASSERT_GE(decoded, 0);
	const SecondarySkill skill(decoded);
	ASSERT_EQ(SecondarySkill::encode(skill.getNum()), skillId);
	gh.changeSecSkill(hero, skill, 1, ChangeValueMode::ABSOLUTE);
	auto & state = const_cast<newHorizonsHeroes::PerkState &>(hero->getPerkState());
	state.rules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
	state.selected.clear();
	state.validate();

	HeroLevelUp levelUp;
	levelUp.player = PlayerColor(0);
	levelUp.heroId = hero->id;
	levelUp.skills = {SecondarySkill::ARCHERY};
	levelUp.perkOfferSeed = 42;
	levelUp.perks = state.prepareOffer([hero](const std::string & id)
	{
		return hero->getPerkSkillRank(id);
	}, levelUp.perkOfferSeed);
	ASSERT_FALSE(levelUp.perks.empty());

	CHeroLevelUpDialogQuery query(&gh, levelUp, hero);
	EXPECT_TRUE(query.isValidReply(0));
	EXPECT_TRUE(query.isValidReply(1));
	levelUp.perkOfferSeed++;
	CHeroLevelUpDialogQuery wrongSeed(&gh, levelUp, hero);
	EXPECT_FALSE(wrongSeed.isValidReply(1));
	levelUp.perkOfferSeed--;
	levelUp.perks.front().name += " forged";
	CHeroLevelUpDialogQuery forged(&gh, levelUp, hero);
	EXPECT_FALSE(forged.isValidReply(1));
	levelUp.perks = state.prepareOffer([hero](const std::string & id)
	{
		return hero->getPerkSkillRank(id);
	}, levelUp.perkOfferSeed);

	auto liveQuery = std::make_shared<CHeroLevelUpDialogQuery>(&gh, levelUp, hero);
	gh.queries->addQuery(liveQuery);
	ASSERT_TRUE(gh.queryReply(liveQuery->queryID, 1, PlayerColor(0)));
	EXPECT_EQ(hero->getPerkState().selected.size(), 1u);
	EXPECT_EQ(hero->getPerkState().selected.front(), levelUp.perks.front().selection);
}

TEST_F(NeutralDwellingBattleQueryTest, heroLevelUpReplicatesPerkChoiceBeforeResolvingQuery)
{
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires the New Horizons module";
	startGame();
	const auto * found = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(found, nullptr);
	auto * hero = gameState()->getHero(found->id);
	ASSERT_NE(hero, nullptr);
	RecordingQueryServer server(gameState());
	CGameHandler gh(server, gameState());

	const std::string skillId = "new-horizons:sorceryMagic";
	const int decoded = SecondarySkill::decode(skillId);
	ASSERT_GE(decoded, 0);
	const SecondarySkill skill(decoded);
	gh.changeSecSkill(hero, skill, 1, ChangeValueMode::ABSOLUTE);
	auto & state = const_cast<newHorizonsHeroes::PerkState &>(hero->getPerkState());
	state.rules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
	state.selected.clear();
	state.validate();

	HeroLevelUp levelUp;
	levelUp.player = PlayerColor(0);
	levelUp.heroId = hero->id;
	levelUp.perkOfferSeed = 0;
	levelUp.perks = state.prepareOffer([hero](const std::string & id)
	{
		return hero->getPerkSkillRank(id);
	}, levelUp.perkOfferSeed);
	ASSERT_FALSE(levelUp.perks.empty());
	auto liveQuery = std::make_shared<CHeroLevelUpDialogQuery>(&gh, levelUp, hero);
	gh.queries->addQuery(liveQuery);

	ASSERT_TRUE(gh.queryReply(liveQuery->queryID, 0, PlayerColor(0)));
	EXPECT_EQ(server.applied, (std::vector<std::string>{"perk", "resolved"}));
	ASSERT_EQ(hero->getPerkState().selected.size(), 1u);
	EXPECT_EQ(hero->getPerkState().selected.front(), levelUp.perks.front().selection);
}

#ifdef ENABLE_NULLKILLER2_AI
TEST_F(NewHorizonsPerkAITest, computerPlayerChoosesPerkThroughAuthoritativeLevelUpQuery)
{
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires the New Horizons module";
	startGame();
	auto * hero = findHeroByOwner(PlayerColor(1));
	ASSERT_NE(hero, nullptr);
	const auto owner = hero->getOwner();
	ASSERT_EQ(owner, PlayerColor(1));
	GameHandlerTestServer server(gameState(), owner);
	CGameHandler gh(server, gameState());

	const SecondarySkill sorcery(SecondarySkill::decode("new-horizons:sorceryMagic"));
	ASSERT_GE(sorcery.getNum(), 0);
	gh.changeSecSkill(hero, sorcery, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	auto & perkState = const_cast<newHorizonsHeroes::PerkState &>(hero->getPerkState());
	perkState.rules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
	perkState.selected.clear();
	perkState.validate();
	for(int index = 0; index < LIBRARY->skillh->size(); ++index)
		gh.changeSecSkill(hero, SecondarySkill(index), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);

	const auto initialLevel = hero->level;
	hero->setExperience(LIBRARY->heroh->reqExp(initialLevel + 1), ChangeValueMode::ABSOLUTE);
	gh.levelUpHero(hero);
	const auto query = std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(gh.queries->topQuery(owner));
	ASSERT_NE(query, nullptr);
	ASSERT_TRUE(query->hlu.skills.empty());
	ASSERT_FALSE(query->hlu.perks.empty());
	gh.onAdvInterfaceReady(owner);
	ASSERT_TRUE(query->prompted);

	const auto transport = std::make_shared<PerkLevelUpLoopback>(gh);
	const auto callback = makeCallback(owner, transport.get());
	const auto ai = AIFactory::createAdventureAI("Nullkiller2");
	transport->ai = ai;
	ai->initGameInterface(std::make_shared<PerkLevelUpEnvironment>(gameState()), callback);
	auto result = transport->completed.get_future();
	ai->heroGotLevel(hero, query->hlu.primskill, query->hlu.skills, query->hlu.perks, query->queryID);
	const auto ready = result.wait_for(std::chrono::seconds(10));
	ASSERT_EQ(ready, std::future_status::ready);
	ai->finish();

	const auto outcome = result.get();
	const auto choice = outcome.choice;
	ASSERT_TRUE(outcome.accepted) << outcome.error;
	ASSERT_GE(choice, static_cast<int>(query->hlu.skills.size()));
	ASSERT_LT(choice, static_cast<int>(query->hlu.skills.size() + query->hlu.perks.size()));
	EXPECT_EQ(hero->level, initialLevel + 1);
	ASSERT_EQ(hero->getPerkState().selected.size(), 1u);
	EXPECT_EQ(hero->getPerkState().selected.front(),
		query->hlu.perks.at(static_cast<size_t>(choice) - query->hlu.skills.size()).selection);
	EXPECT_FALSE(gh.queries->topQuery(owner));
}

TEST_F(NewHorizonsPerkAITest, rampartAIChoosesActiveFactionPerkThenFactionSkillRank)
{
	startGame(HeroTypeID(16)); // Mephala, a Rampart Ranger.
	auto * hero = findHeroByOwner(PlayerColor(1));
	ASSERT_NE(hero, nullptr);
	const auto owner = hero->getOwner();
	ASSERT_EQ(owner, PlayerColor(1));
	ASSERT_EQ(hero->getFactionID(), FactionID::RAMPART);

	const SecondarySkill sylvanLuck(SecondarySkill::decode("new-horizons:sylvanLuck"));
	ASSERT_GE(sylvanLuck.getNum(), 0);
	GameHandlerTestServer server(gameState(), owner);
	CGameHandler gh(server, gameState());

	// Isolate the faction path. The hero keeps only the canonical faction Skill;
	// this makes the second level-up's rank choice unambiguous while still
	// exercising the normal server-authored offer and AI reply transport.
	for(int index = 0; index < LIBRARY->skillh->size(); ++index)
		gh.changeSecSkill(hero, SecondarySkill(index), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	gh.changeSecSkill(hero, sylvanLuck, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_EQ(hero->getSecSkillLevel(sylvanLuck), MasteryLevel::BASIC);

	auto answerWithAI = [&](const std::shared_ptr<CHeroLevelUpDialogQuery> & query)
	{
		auto transport = std::make_shared<PerkLevelUpLoopback>(gh);
		const auto callback = makeCallback(owner, transport.get());
		const auto ai = AIFactory::createAdventureAI("Nullkiller2");
		transport->ai = ai;
		ai->initGameInterface(std::make_shared<PerkLevelUpEnvironment>(gameState()), callback);
		auto result = transport->completed.get_future();
		ai->heroGotLevel(hero, query->hlu.primskill, query->hlu.skills, query->hlu.perks, query->queryID);
		const auto ready = result.wait_for(std::chrono::seconds(10));
		ai->finish();
		if(ready != std::future_status::ready)
		{
			ADD_FAILURE() << "Nullkiller did not answer the faction level-up query";
			return PerkLevelUpLoopback::Result{-1, false, "AI query timed out"};
		}
		return result.get();
	};

	// First level-up: the active faction perk is offered and selected. Planned
	// entries must never appear in the server-authored offer.
	hero->setExperience(LIBRARY->heroh->reqExp(hero->level + 1), ChangeValueMode::ABSOLUTE);
	gh.levelUpHero(hero);
	auto query = std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(gh.queries->topQuery(owner));
	ASSERT_NE(query, nullptr);
	ASSERT_FALSE(query->hlu.perks.empty());
	for(const auto & candidate : query->hlu.perks)
	{
		const auto definition = newHorizonsHeroes::perkDefinition(hero->getPerkState().rules,
			candidate.selection.skillId, candidate.selection.perkId);
		ASSERT_TRUE(definition.has_value());
		EXPECT_EQ(definition->effect["status"].String(), "active");
	}
	gh.onAdvInterfaceReady(owner);
	const auto firstOutcome = answerWithAI(query);
	ASSERT_TRUE(firstOutcome.accepted) << firstOutcome.error;
	ASSERT_GE(firstOutcome.choice, static_cast<int>(query->hlu.skills.size()));
	const auto chosenPerkIndex = static_cast<size_t>(firstOutcome.choice) - query->hlu.skills.size();
	ASSERT_LT(chosenPerkIndex, query->hlu.perks.size());
	const auto & chosenPerk = query->hlu.perks[chosenPerkIndex];
	EXPECT_EQ(chosenPerk.selection.skillId, "new-horizons:sylvanLuck");
	EXPECT_EQ(chosenPerk.requiredRank, 1);
	ASSERT_EQ(hero->getPerkState().selected.size(), 1u);
	EXPECT_EQ(hero->getPerkState().selected.front(), chosenPerk.selection);
	EXPECT_TRUE(hero->hasActivePerk(chosenPerk.selection.skillId, chosenPerk.selection.perkId));

	// Second level-up: after the sole active faction perk is selected, the AI
	// must advance the faction Skill itself instead of falling back to a foreign
	// or generic legacy skill.
	hero->setExperience(LIBRARY->heroh->reqExp(hero->level + 1), ChangeValueMode::ABSOLUTE);
	gh.levelUpHero(hero);
	query = std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(gh.queries->topQuery(owner));
	ASSERT_NE(query, nullptr);
	ASSERT_TRUE(query->hlu.perks.empty());
	const auto skillChoice = std::find(query->hlu.skills.begin(), query->hlu.skills.end(), sylvanLuck);
	ASSERT_NE(skillChoice, query->hlu.skills.end());
	const auto skillIndex = static_cast<int>(std::distance(query->hlu.skills.begin(), skillChoice));
	gh.onAdvInterfaceReady(owner);
	const auto secondOutcome = answerWithAI(query);
	ASSERT_TRUE(secondOutcome.accepted) << secondOutcome.error;
	EXPECT_EQ(secondOutcome.choice, skillIndex);
	EXPECT_EQ(hero->getSecSkillLevel(sylvanLuck), MasteryLevel::ADVANCED);
}
#endif

TEST_F(QueriesProcessorTest, popIfTop_removesTopQuery)
{
	auto query = std::make_shared<TestQuery>(&gh, PlayerColor(1), QueryType::HeroMovement);

	queries.addQuery(query);

	EXPECT_EQ(queries.topQuery(PlayerColor(1)), query);
	EXPECT_EQ(queries.countQuery(query), 1);

	queries.popIfTop(query);

	EXPECT_EQ(queries.topQuery(PlayerColor(1)), nullptr);
	EXPECT_EQ(queries.countQuery(query), 0);
}

TEST_F(QueriesProcessorTest, popIfTop_doesNothingWhenQueryIsNotPresent)
{
	auto addedQuery = std::make_shared<TestQuery>(&gh, PlayerColor(1), QueryType::HeroMovement);
	auto missingQuery = std::make_shared<TestQuery>(&gh, PlayerColor(1), QueryType::MapObjectVisit);

	queries.addQuery(addedQuery);

	EXPECT_EQ(queries.topQuery(PlayerColor(1)), addedQuery);
	EXPECT_EQ(queries.countQuery(addedQuery), 1);
	EXPECT_EQ(queries.countQuery(missingQuery), 0);

	queries.popIfTop(missingQuery);

	EXPECT_EQ(queries.topQuery(PlayerColor(1)), addedQuery);
	EXPECT_EQ(queries.countQuery(addedQuery), 1);
	EXPECT_EQ(queries.countQuery(missingQuery), 0);
}

TEST_F(QueriesProcessorTest, popIfTop_skipsWhenNestedQueryIsAbove_andLaterSucceedsAfterUnwind)
{
	auto movementQuery = std::make_shared<TestQuery>(&gh, PlayerColor(1), QueryType::HeroMovement);
	auto visitQuery = std::make_shared<TestQuery>(&gh, PlayerColor(1), QueryType::MapObjectVisit);

	queries.addQuery(movementQuery);
	queries.addQuery(visitQuery);

	EXPECT_EQ(queries.topQuery(PlayerColor(1)), visitQuery);
	EXPECT_EQ(queries.countQuery(movementQuery), 1);
	EXPECT_EQ(queries.countQuery(visitQuery), 1);

	queries.popIfTop(movementQuery);

	EXPECT_EQ(queries.topQuery(PlayerColor(1)), visitQuery);
	EXPECT_EQ(queries.countQuery(movementQuery), 1);
	EXPECT_EQ(queries.countQuery(visitQuery), 1);

	queries.popIfTop(visitQuery);

	EXPECT_EQ(queries.topQuery(PlayerColor(1)), movementQuery);
	EXPECT_EQ(queries.countQuery(movementQuery), 1);
	EXPECT_EQ(queries.countQuery(visitQuery), 0);

	queries.popIfTop(movementQuery);

	EXPECT_EQ(queries.topQuery(PlayerColor(1)), nullptr);
	EXPECT_EQ(queries.countQuery(movementQuery), 0);
	EXPECT_EQ(queries.countQuery(visitQuery), 0);
}

TEST_F(QueriesProcessorTest, popIfTop_exposesQueryBelowWithRemovedQueryAsArgument)
{
	auto bottomQuery = std::make_shared<TestQuery>(&gh, PlayerColor(1), QueryType::HeroMovement);
	auto topQuery = std::make_shared<TestQuery>(&gh, PlayerColor(1), QueryType::MapObjectVisit);

	queries.addQuery(bottomQuery);
	queries.addQuery(topQuery);

	queries.popIfTop(topQuery);

	EXPECT_EQ(queries.topQuery(PlayerColor(1)), bottomQuery);

	EXPECT_EQ(bottomQuery->events, std::vector<QueryEvent>({
		QueryEvent::OnAdding,
		QueryEvent::OnAdded,
		QueryEvent::OnExposure
	}));

	EXPECT_EQ(bottomQuery->onAddingCalls, std::vector<PlayerColor>({PlayerColor(1)}));
	EXPECT_EQ(bottomQuery->onAddedCalls, std::vector<PlayerColor>({PlayerColor(1)}));
	EXPECT_TRUE(bottomQuery->onRemovalCalls.empty());

	ASSERT_EQ(bottomQuery->exposureArgs.size(), 1);
	EXPECT_EQ(bottomQuery->exposureArgs[0], topQuery);

	EXPECT_EQ(topQuery->events, std::vector<QueryEvent>({
		QueryEvent::OnAdding,
		QueryEvent::OnAdded,
		QueryEvent::OnRemoval
	}));
}

TEST_F(QueriesProcessorTest, popIfTop_allowsExposedQueryToPopItself)
{
	auto bottomQuery = std::make_shared<TestQuery>(&gh, PlayerColor(1), QueryType::HeroMovement);
	auto topQuery = std::make_shared<TestQuery>(&gh, PlayerColor(1), QueryType::MapObjectVisit);

	bottomQuery->popOnExposure = true;

	queries.addQuery(bottomQuery);
	queries.addQuery(topQuery);

	queries.popIfTop(topQuery);

	EXPECT_EQ(queries.topQuery(PlayerColor(1)), nullptr);
	EXPECT_EQ(queries.countQuery(bottomQuery), 0);
	EXPECT_EQ(queries.countQuery(topQuery), 0);

	EXPECT_EQ(bottomQuery->events, std::vector<QueryEvent>({
		QueryEvent::OnAdding,
		QueryEvent::OnAdded,
		QueryEvent::OnExposure,
		QueryEvent::OnRemoval
	}));

	ASSERT_EQ(bottomQuery->exposureArgs.size(), 1);
	EXPECT_EQ(bottomQuery->exposureArgs[0], topQuery);

	EXPECT_EQ(bottomQuery->onRemovalCalls, std::vector<PlayerColor>({PlayerColor(1)}));

	EXPECT_EQ(topQuery->events, std::vector<QueryEvent>({
		QueryEvent::OnAdding,
		QueryEvent::OnAdded,
		QueryEvent::OnRemoval
	}));
}

TEST_F(QueriesProcessorTest, popIfTop_removesMultiPlayerQueryOnlyWhereItIsTop)
{
	auto sharedQuery = std::make_shared<TestQuery>(
		&gh,
		std::initializer_list<PlayerColor>{PlayerColor(0), PlayerColor(1)},
		QueryType::Generic);

	auto blueTopQuery = std::make_shared<TestQuery>(&gh, PlayerColor(1), QueryType::MapObjectVisit);

	queries.addQuery(sharedQuery);
	queries.addQuery(blueTopQuery);

	queries.popIfTop(sharedQuery);

	EXPECT_EQ(queries.topQuery(PlayerColor(0)), nullptr);
	EXPECT_EQ(queries.topQuery(PlayerColor(1)), blueTopQuery);
	EXPECT_EQ(queries.countQuery(sharedQuery), 1);
	EXPECT_EQ(queries.countQuery(blueTopQuery), 1);

	EXPECT_EQ(sharedQuery->onRemovalCalls, std::vector<PlayerColor>({PlayerColor(0)}));
}

TEST_F(QueriesProcessorTest, popIfTop_removesMultiPlayerQueryAfterItBecomesTopAgain)
{
	auto sharedQuery = std::make_shared<TestQuery>(
		&gh,
		std::initializer_list<PlayerColor>{PlayerColor(0), PlayerColor(1)},
		QueryType::Generic);

	auto blueTopQuery = std::make_shared<TestQuery>(&gh, PlayerColor(1), QueryType::MapObjectVisit);

	queries.addQuery(sharedQuery);
	queries.addQuery(blueTopQuery);

	queries.popIfTop(sharedQuery);

	EXPECT_EQ(queries.topQuery(PlayerColor(0)), nullptr);
	EXPECT_EQ(queries.topQuery(PlayerColor(1)), blueTopQuery);
	EXPECT_EQ(queries.countQuery(sharedQuery), 1);

	queries.popIfTop(blueTopQuery);

	ASSERT_EQ(sharedQuery->exposureArgs.size(), 1);
	EXPECT_EQ(sharedQuery->exposureArgs[0], blueTopQuery);

	EXPECT_EQ(queries.topQuery(PlayerColor(1)), sharedQuery);
	EXPECT_EQ(queries.countQuery(blueTopQuery), 0);
	EXPECT_EQ(queries.countQuery(sharedQuery), 1);

	queries.popIfTop(sharedQuery);

	EXPECT_EQ(queries.topQuery(PlayerColor(0)), nullptr);
	EXPECT_EQ(queries.topQuery(PlayerColor(1)), nullptr);
	EXPECT_EQ(queries.countQuery(sharedQuery), 0);

	EXPECT_EQ(sharedQuery->onRemovalCalls, std::vector<PlayerColor>({
		PlayerColor(0),
		PlayerColor(1)
	}));
}

TEST_F(QueriesProcessorTest, popQuery_removesMultiPlayerQueryOnlyWhereItIsTop)
{
	auto sharedQuery = std::make_shared<TestQuery>(
		&gh,
		std::initializer_list<PlayerColor>{PlayerColor(0), PlayerColor(1)},
		QueryType::Generic);

	auto blueTopQuery = std::make_shared<TestQuery>(&gh, PlayerColor(1), QueryType::MapObjectVisit);

	queries.addQuery(sharedQuery);
	queries.addQuery(blueTopQuery);

	queries.popQuery(*sharedQuery);

	EXPECT_EQ(queries.topQuery(PlayerColor(0)), nullptr);
	EXPECT_EQ(queries.topQuery(PlayerColor(1)), blueTopQuery);
	EXPECT_EQ(queries.countQuery(sharedQuery), 1);
	EXPECT_EQ(sharedQuery->onRemovalCalls, std::vector<PlayerColor>({PlayerColor(0)}));
}

TEST_F(QueriesProcessorTest, popQuery_removesRemainingMultiPlayerQueryAfterItBecomesTop)
{
	auto sharedQuery = std::make_shared<TestQuery>(
		&gh,
		std::initializer_list<PlayerColor>{PlayerColor(0), PlayerColor(1)},
		QueryType::Generic);

	auto blueTopQuery = std::make_shared<TestQuery>(&gh, PlayerColor(1), QueryType::MapObjectVisit);

	queries.addQuery(sharedQuery);
	queries.addQuery(blueTopQuery);

	queries.popQuery(*sharedQuery);
	queries.popIfTop(blueTopQuery);
	queries.popQuery(*sharedQuery);

	EXPECT_EQ(queries.topQuery(PlayerColor(0)), nullptr);
	EXPECT_EQ(queries.topQuery(PlayerColor(1)), nullptr);
	EXPECT_EQ(queries.countQuery(sharedQuery), 0);
	EXPECT_EQ(sharedQuery->onRemovalCalls, std::vector<PlayerColor>({
		PlayerColor(0),
		PlayerColor(1)
	}));
}

TEST_F(QueriesProcessorTest, popIfTop_callsRemovalBeforeExposure)
{
	std::vector<RecordedEvent> eventLog;

	auto bottomQuery = std::make_shared<TestQuery>(&gh, PlayerColor(1), QueryType::HeroMovement);
	auto topQuery = std::make_shared<TestQuery>(&gh, PlayerColor(1), QueryType::MapObjectVisit);

	bottomQuery->sharedEventLog = &eventLog;
	topQuery->sharedEventLog = &eventLog;

	queries.addQuery(bottomQuery);
	queries.addQuery(topQuery);

	eventLog.clear();

	queries.popIfTop(topQuery);

	ASSERT_EQ(eventLog.size(), 2);
	EXPECT_EQ(eventLog[0].query, topQuery.get());
	EXPECT_EQ(eventLog[0].event, QueryEvent::OnRemoval);
	EXPECT_EQ(eventLog[1].query, bottomQuery.get());
	EXPECT_EQ(eventLog[1].event, QueryEvent::OnExposure);
}

TEST_F(QueriesProcessorTest, popQuery_callsRemovalBeforeExposure)
{
	std::vector<RecordedEvent> eventLog;

	auto bottomQuery = std::make_shared<TestQuery>(&gh, PlayerColor(1), QueryType::HeroMovement);
	auto topQuery = std::make_shared<TestQuery>(&gh, PlayerColor(1), QueryType::MapObjectVisit);

	bottomQuery->sharedEventLog = &eventLog;
	topQuery->sharedEventLog = &eventLog;

	queries.addQuery(bottomQuery);
	queries.addQuery(topQuery);

	eventLog.clear();

	queries.popQuery(*topQuery);

	ASSERT_EQ(eventLog.size(), 2);
	EXPECT_EQ(eventLog[0].query, topQuery.get());
	EXPECT_EQ(eventLog[0].event, QueryEvent::OnRemoval);
	EXPECT_EQ(eventLog[1].query, bottomQuery.get());
	EXPECT_EQ(eventLog[1].event, QueryEvent::OnExposure);
}

TEST_F(QueriesProcessorTest, popIfTop_skipsExposureWhenRemovalAddsNewTopQuery)
{
	auto bottomQuery = std::make_shared<TestQuery>(&gh, PlayerColor(1), QueryType::HeroMovement);
	auto topQuery = std::make_shared<TestQuery>(&gh, PlayerColor(1), QueryType::MapObjectVisit);
	auto replacementQuery = std::make_shared<TestQuery>(&gh, PlayerColor(1), QueryType::Generic);

	topQuery->addReplacementOnRemoval = true;
	topQuery->replacementQuery = replacementQuery;

	queries.addQuery(bottomQuery);
	queries.addQuery(topQuery);

	queries.popIfTop(topQuery);

	EXPECT_EQ(queries.topQuery(PlayerColor(1)), replacementQuery);
	EXPECT_EQ(queries.countQuery(bottomQuery), 1);
	EXPECT_EQ(queries.countQuery(topQuery), 0);
	EXPECT_EQ(queries.countQuery(replacementQuery), 1);

	EXPECT_EQ(bottomQuery->events, std::vector<QueryEvent>({
		QueryEvent::OnAdding,
		QueryEvent::OnAdded
	}));
	EXPECT_TRUE(bottomQuery->exposureArgs.empty());

	EXPECT_EQ(topQuery->events, std::vector<QueryEvent>({
		QueryEvent::OnAdding,
		QueryEvent::OnAdded,
		QueryEvent::OnRemoval
	}));

	EXPECT_EQ(replacementQuery->events, std::vector<QueryEvent>({
		QueryEvent::OnAdding,
		QueryEvent::OnAdded
	}));
}

TEST_F(QueriesProcessorTest, addQuery_addsSameQueryForAllAffectedPlayers)
{
	auto query = std::make_shared<TestQuery>(&gh, std::initializer_list<PlayerColor>{PlayerColor(0), PlayerColor(1)}, QueryType::Generic);

	queries.addQuery(query);

	EXPECT_EQ(queries.topQuery(PlayerColor(0)), query);
	EXPECT_EQ(queries.topQuery(PlayerColor(1)), query);
	EXPECT_EQ(queries.countQuery(query), 2);

	EXPECT_EQ(query->events, std::vector<QueryEvent>({
	QueryEvent::OnAdding,
	QueryEvent::OnAdded,
	QueryEvent::OnAdding,
	QueryEvent::OnAdded
	}));

	EXPECT_EQ(query->onAddingCalls, std::vector<PlayerColor>({PlayerColor(0), PlayerColor(1)}));
	EXPECT_EQ(query->onAddedCalls, std::vector<PlayerColor>({PlayerColor(0), PlayerColor(1)}));
	EXPECT_TRUE(query->onRemovalCalls.empty());
	EXPECT_TRUE(query->exposureArgs.empty());
}

TEST_F(QueriesProcessorTest, addQuery_skipsDuplicateBackWithoutRepeatingOnAdded)
{
	auto query = std::make_shared<TestQuery>(&gh, PlayerColor(1), QueryType::HeroMovement);

	queries.addQuery(query);
	queries.addQuery(query);

	EXPECT_EQ(queries.topQuery(PlayerColor(1)), query);
	EXPECT_EQ(queries.countQuery(query), 1);

	EXPECT_EQ(query->events, std::vector<QueryEvent>({
		QueryEvent::OnAdding,
		QueryEvent::OnAdded
	}));

	EXPECT_EQ(query->onAddingCalls, std::vector<PlayerColor>({PlayerColor(1)}));
	EXPECT_EQ(query->onAddedCalls, std::vector<PlayerColor>({PlayerColor(1)}));
}

TEST_F(QueriesProcessorTest, getQuery_returnsNullForUnknownQueryId)
{
	auto query = std::make_shared<TestQuery>(&gh, PlayerColor(1), QueryType::HeroMovement);

	queries.addQuery(query);

	EXPECT_EQ(queries.getQuery(QueryID(query->queryID.getNum() + 1)), nullptr);
}

TEST_F(QueriesProcessorTest, getQuery_returnsAddedQueryAndNullAfterRemoval)
{
	auto query = std::make_shared<TestQuery>(&gh, PlayerColor(1), QueryType::HeroMovement);

	queries.addQuery(query);

	EXPECT_EQ(queries.getQuery(query->queryID), query);

	queries.popIfTop(query);

	EXPECT_EQ(queries.getQuery(query->queryID), nullptr);
}

TEST_F(QueriesProcessorTest, countQuery_returnsZeroForNullptr)
{
	EXPECT_EQ(queries.countQuery(nullptr), 0);
}

TEST_F(QueriesProcessorTest, foreignPlayerCannotPreseedReplyOnAnotherPlayersNonTopQuery)
{
	std::optional<int32_t> captured;
	auto ownTop = std::make_shared<CGenericQuery>(&gh, PlayerColor(0), [](std::optional<int32_t>) {});
	auto foreign = std::make_shared<CGenericQuery>(&gh, PlayerColor(1), [&](std::optional<int32_t> reply)
	{
		captured = reply;
	});
	queries.addQuery(ownTop);
	queries.addQuery(foreign);

	EXPECT_FALSE(gh.queryReply(foreign->queryID, 7, PlayerColor(0)));
	queries.popQuery(foreign);
	EXPECT_FALSE(captured.has_value());
}
