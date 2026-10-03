/*
 * NewHorizonsEstatesIncomeAITest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"

#include <algorithm>
#include <chrono>
#include <future>

#include "AI/Nullkiller2/Analyzers/BuildAnalyzer.h"
#include "lib/CSkillHandler.h"
#include "lib/GameConstants.h"
#include "lib/GameLibrary.h"
#include "lib/IGameSettings.h"
#include "lib/CPlayerState.h"
#include "lib/battle/BattleInfo.h"
#include "lib/callback/AIFactory.h"
#include "lib/callback/CCallback.h"
#include "lib/callback/CGlobalAI.h"
#include "lib/callback/IClient.h"
#include "lib/entities/hero/CHeroHandler.h"
#include "lib/entities/hero/NewHorizonsPerkState.h"
#include "lib/gameState/CGameState.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/mapObjects/CGTownInstance.h"
#include "lib/modding/CModHandler.h"
#include "lib/networkPacks/PacksForServer.h"
#include "mock/GameHandlerTestServer.h"
#include "nullkiller2/NullkillerTest.h"
#include "server/CGameHandler.h"

namespace
{
const PlayerColor PLAYER(0);
constexpr auto ESTATES_SKILL = "new-horizons:estates";
constexpr auto TAX_COLLECTOR = "new-horizons:estates.taxCollector";
constexpr auto ESTATE_NETWORK = "new-horizons:estates.estateNetwork";
constexpr auto INVESTOR = "new-horizons:estates.investor";

class EstatesAIEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit EstatesAIEnvironment(std::shared_ptr<CGameState> state)
		: state(std::move(state))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class EstatesAIChoiceClient final : public IClient
{
public:
	std::promise<int> completed;

	std::optional<BattleAction> makeSurrenderRetreatDecision(
		PlayerColor,
		const BattleID &,
		const BattleStateInfoForRetreat &) override
	{
		return std::nullopt;
	}

	int sendRequest(const CPackForServer & request, PlayerColor player, bool) override
	{
		const auto * reply = dynamic_cast<const QueryReply *>(&request);
		if(!reply || player != PLAYER || !reply->reply.has_value())
			throw std::runtime_error("Nullkiller submitted an unexpected Estates level-up reply");

		completed.set_value(*reply->reply);
		return 1;
	}
};

void activatePlannedInvestorForFixture(CGHeroInstance * hero)
{
	auto & perkState = const_cast<newHorizonsHeroes::PerkState &>(hero->getPerkState());
	for(auto & skill : perkState.rules["skills"].Struct())
	{
		for(auto & perk : skill.second["perks"].Vector())
		{
			if(perk["id"].String() == INVESTOR)
				perk["effect"]["status"].String() = "active";
		}
	}
	perkState.validate();
}

void prepareInvestorOfferForFixture(CGHeroInstance * hero)
{
	const auto estatesSkill = SecondarySkill(SecondarySkill::decode(ESTATES_SKILL));
	hero->setSecSkillLevel(estatesSkill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	hero->applyPerkSelection({ESTATES_SKILL, TAX_COLLECTOR});
	hero->setSecSkillLevel(estatesSkill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	activatePlannedInvestorForFixture(hero);
}

void selectInvestorForFixture(CGHeroInstance * hero)
{
	prepareInvestorOfferForFixture(hero);
	hero->applyPerkSelection({ESTATES_SKILL, INVESTOR});
}

void restrictFixturePerkOffersToInvestor(CGHeroInstance * hero)
{
	auto & perkState = const_cast<newHorizonsHeroes::PerkState &>(hero->getPerkState());
	for(auto & skill : perkState.rules["skills"].Struct())
	{
		for(auto & perk : skill.second["perks"].Vector())
		{
			const auto id = perk["id"].String();
			if(id != TAX_COLLECTOR && id != INVESTOR)
				perk["effect"]["status"].String() = "planned";
		}
	}
	perkState.validate();
}

class NewHorizonsEstatesIncomeAITest : public NullkillerTest
{
protected:
	void SetUp() override
	{
		NullkillerTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void startGame()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER);
		for(size_t index = 0; index < 11; ++index)
		{
			const int x = 4 + static_cast<int>(index % 4) * 7;
			const int y = 4 + static_cast<int>(index / 4) * 7;
			builder.town({x, y, 0}, FactionID::RAMPART, PLAYER);
		}
		builder.hero({32, 32, 0}, HeroTypeID(0), PLAYER)
			.hero({32, 28, 0}, HeroTypeID(1), PLAYER);
		startWithMap(std::move(builder));

		firstHero = findHeroAt({32, 32, 0});
		secondHero = findHeroAt({32, 28, 0});
		ASSERT_NE(firstHero, nullptr);
		ASSERT_NE(secondHero, nullptr);
	}

	CGHeroInstance * firstHero = nullptr;
	CGHeroInstance * secondHero = nullptr;
};
} // namespace

TEST_F(NewHorizonsEstatesIncomeAITest, BuildAnalyzerUsesTheSharedCappedIncomeForEachActiveHolder)
{
	startGame();
	const int estatesSkill = SecondarySkill::decode("new-horizons:estates");
	ASSERT_GE(estatesSkill, 0);
	for(auto * hero : {firstHero, secondHero})
	{
		hero->setSecSkillLevel(SecondarySkill(estatesSkill), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({"new-horizons:estates", "new-horizons:estates.taxCollector"});
		EXPECT_EQ(hero->dailyIncome()[EGameResID::GOLD], 625);
	}

	const auto * playerState = gameState()->getPlayerState(PLAYER);
	const auto forecast = NK2AI::BuildAnalyzer::calculateDailyIncome(
		playerState->getOwnedObjects(), playerState->getTowns());
	int townIncome = 0;
	for(const auto * town : playerState->getTowns())
		townIncome += town->dailyIncome()[EGameResID::GOLD];
	EXPECT_EQ(forecast[EGameResID::GOLD], townIncome + 2 * 625);
}

TEST_F(NewHorizonsEstatesIncomeAITest, NullkillerSelectsTheOfferedAdvancedEstateNetworkPerk)
{
	startGame();
	const int estatesSkill = SecondarySkill::decode(ESTATES_SKILL);
	ASSERT_GE(estatesSkill, 0);
	firstHero->setSecSkillLevel(SecondarySkill(estatesSkill), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	firstHero->applyPerkSelection({ESTATES_SKILL, TAX_COLLECTOR});

	// Isolate legal selection of this new perk, not comparative valuation
	// against unrelated faction/morale perks. Retain the selected prerequisite.
	auto & perkState = const_cast<newHorizonsHeroes::PerkState &>(firstHero->getPerkState());
	for(auto & skill : perkState.rules["skills"].Struct())
	{
		for(auto & perk : skill.second["perks"].Vector())
			if(perk["id"].String() != TAX_COLLECTOR && perk["id"].String() != ESTATE_NETWORK)
				perk["effect"]["status"].String() = "planned";
	}
	perkState.validate();

	const auto rankLookup = [this](const std::string & skillId)
	{
		return firstHero->getPerkSkillRank(skillId);
	};
	const auto offer = firstHero->getPerkState().prepareOffer(rankLookup, 0);
	ASSERT_EQ(offer.size(), 1u);
	const auto networkOffer = std::ranges::find_if(offer, [](const auto & candidate)
	{
		return candidate.selection.perkId == ESTATE_NETWORK;
	});
	ASSERT_NE(networkOffer, offer.end());
	ASSERT_EQ(networkOffer->requiredRank, MasteryLevel::ADVANCED);

	const auto ai = AIFactory::createAdventureAI("Nullkiller2");
	ASSERT_NE(ai, nullptr);
	auto transport = std::make_shared<EstatesAIChoiceClient>();
	const auto callback = makeCallback(PLAYER, transport.get());
	ai->initGameInterface(std::make_shared<EstatesAIEnvironment>(gameState()), callback);
	auto answer = transport->completed.get_future();
	std::vector<SecondarySkill> skills;
	ai->heroGotLevel(firstHero, PrimarySkill::ATTACK, skills, offer, QueryID(42));
	const auto ready = answer.wait_for(std::chrono::seconds(10));
	ai->finish();
	ASSERT_EQ(ready, std::future_status::ready);
	const int selected = answer.get();
	ASSERT_GE(selected, 0);
	ASSERT_LT(static_cast<size_t>(selected), offer.size());
	EXPECT_EQ(offer[static_cast<size_t>(selected)].selection.perkId, ESTATE_NETWORK);
}

TEST_F(NewHorizonsEstatesIncomeAITest, PublishedInvestorIncomeFeedsTheSharedForecastAndStaysFixedMidweek)
{
	startGame();
	for(auto * hero : {firstHero, secondHero})
		selectInvestorForFixture(hero);

	auto * mutablePlayerState = gameState()->getPlayerState(PLAYER);
	const auto * constPlayerState = mutablePlayerState;
	mutablePlayerState->resources[EGameResID::GOLD] = 10000;
	const int firstHeroIncomeBeforePublication = firstHero->dailyIncome()[EGameResID::GOLD];
	const int secondHeroIncomeBeforePublication = secondHero->dailyIncome()[EGameResID::GOLD];
	const auto forecastBeforePublication = NK2AI::BuildAnalyzer::calculateDailyIncome(
		constPlayerState->getOwnedObjects(), constPlayerState->getTowns());

	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	handler.onNewTurn(); // Day zero -> one publishes the weekly Investor snapshot.
	ASSERT_EQ(gameState()->day, 1u);
	EXPECT_EQ(firstHero->getNewHorizonsInvestorDailyGold(), 100);
	EXPECT_EQ(secondHero->getNewHorizonsInvestorDailyGold(), 100);
	EXPECT_EQ(firstHero->dailyIncome()[EGameResID::GOLD], firstHeroIncomeBeforePublication + 100);
	EXPECT_EQ(secondHero->dailyIncome()[EGameResID::GOLD], secondHeroIncomeBeforePublication + 100);

	const auto forecastAfterPublication = NK2AI::BuildAnalyzer::calculateDailyIncome(
		constPlayerState->getOwnedObjects(), constPlayerState->getTowns());
	EXPECT_EQ(forecastAfterPublication[EGameResID::GOLD], forecastBeforePublication[EGameResID::GOLD] + 200);

	// The treasury can change midweek, but it cannot re-quote the saved snapshot.
	mutablePlayerState->resources[EGameResID::GOLD] = 25000;
	handler.onNewTurn();
	ASSERT_EQ(gameState()->day, 2u);
	EXPECT_EQ(firstHero->getNewHorizonsInvestorDailyGold(), 100);
	EXPECT_EQ(secondHero->getNewHorizonsInvestorDailyGold(), 100);
	const auto forecastMidweek = NK2AI::BuildAnalyzer::calculateDailyIncome(
		constPlayerState->getOwnedObjects(), constPlayerState->getTowns());
	EXPECT_EQ(forecastMidweek[EGameResID::GOLD], forecastAfterPublication[EGameResID::GOLD]);
}

TEST_F(NewHorizonsEstatesIncomeAITest, InvestorRefreshesNextWeekAndClearsWhenHolderFallsBelowAdvanced)
{
	startGame();
	selectInvestorForFixture(firstHero);
	selectInvestorForFixture(secondHero);

	auto * playerState = gameState()->getPlayerState(PLAYER);
	playerState->resources[EGameResID::GOLD] = 5000;
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	handler.onNewTurn(); // Day zero -> one captures exactly one full treasury tranche.
	ASSERT_EQ(gameState()->day, 1u);
	EXPECT_EQ(firstHero->getNewHorizonsInvestorDailyGold(), 50);
	EXPECT_EQ(secondHero->getNewHorizonsInvestorDailyGold(), 50);

	firstHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(ESTATES_SKILL)),
		MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	for(int day = 0; day < 6; ++day)
		handler.onNewTurn(); // Reach day seven without refreshing the weekly snapshot.
	ASSERT_EQ(gameState()->day, 7u);

	playerState->resources[EGameResID::GOLD] = 25000;
	handler.onNewTurn(); // Day eight applies the capped next-week snapshot.
	ASSERT_EQ(gameState()->day, 8u);
	EXPECT_EQ(firstHero->getNewHorizonsInvestorDailyGold(), 0);
	EXPECT_EQ(secondHero->getNewHorizonsInvestorDailyGold(), 250);
}

TEST_F(NewHorizonsEstatesIncomeAITest, NullkillerSelectsTheOfferedAdvancedInvestorPerk)
{
	startGame();
	prepareInvestorOfferForFixture(firstHero);
	restrictFixturePerkOffersToInvestor(firstHero);

	const auto rankLookup = [this](const std::string & skillId)
	{
		return firstHero->getPerkSkillRank(skillId);
	};
	const auto offer = firstHero->getPerkState().prepareOffer(rankLookup, 0);
	ASSERT_EQ(offer.size(), 1u);
	ASSERT_EQ(offer.front().selection.perkId, INVESTOR);
	ASSERT_EQ(offer.front().requiredRank, MasteryLevel::ADVANCED);

	const auto ai = AIFactory::createAdventureAI("Nullkiller2");
	ASSERT_NE(ai, nullptr);
	auto transport = std::make_shared<EstatesAIChoiceClient>();
	const auto callback = makeCallback(PLAYER, transport.get());
	ai->initGameInterface(std::make_shared<EstatesAIEnvironment>(gameState()), callback);
	auto answer = transport->completed.get_future();
	std::vector<SecondarySkill> skills;
	ai->heroGotLevel(firstHero, PrimarySkill::ATTACK, skills, offer, QueryID(43));
	const auto ready = answer.wait_for(std::chrono::seconds(10));
	ai->finish();
	ASSERT_EQ(ready, std::future_status::ready);
	const int selected = answer.get();
	ASSERT_GE(selected, 0);
	ASSERT_LT(static_cast<size_t>(selected), offer.size());
	EXPECT_EQ(offer[static_cast<size_t>(selected)].selection.perkId, INVESTOR);
}

TEST_F(NewHorizonsEstatesIncomeAITest, NullkillerResourceViewIncludesTheAuthoritativeWeekStartGrant)
{
	startGame();
	const int estatesSkill = SecondarySkill::decode(ESTATES_SKILL);
	ASSERT_GE(estatesSkill, 0);
	firstHero->setSecSkillLevel(SecondarySkill(estatesSkill), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	firstHero->applyPerkSelection({ESTATES_SKILL, TAX_COLLECTOR});
	firstHero->applyPerkSelection({ESTATES_SKILL, ESTATE_NETWORK});
	ASSERT_TRUE(firstHero->hasActivePerk(ESTATES_SKILL, ESTATE_NETWORK));

	auto * playerState = gameState()->getPlayerState(PLAYER);
	playerState->resources[EGameResID::WOOD] = 0;
	playerState->resources[EGameResID::ORE] = 0;
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	handler.onNewTurn(); // Initial week-start grant is applied before AI planning.
	EXPECT_EQ(gameState()->day, 1u);
	EXPECT_EQ(playerState->resources[EGameResID::WOOD], 3);
	EXPECT_EQ(playerState->resources[EGameResID::ORE], 3);

	const auto gateway = makeGateway(PLAYER);
	const auto freeResources = gateway->nullkiller->getFreeResources();
	EXPECT_EQ(freeResources[EGameResID::WOOD], 3);
	EXPECT_EQ(freeResources[EGameResID::ORE], 3);
}
