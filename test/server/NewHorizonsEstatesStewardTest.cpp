/*
 * NewHorizonsEstatesStewardTest.cpp, part of VCMI engine
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"
#include "battles/FullGameSnapshotTypes.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/GameConstants.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../server/CGameHandler.h"
#include "../../server/IGameServer.h"
#include <optional>
#include <vstd/ContainerUtils.h>

namespace
{
const PlayerColor PLAYER(0);
constexpr auto ESTATES = "new-horizons:estates";
constexpr auto STEWARD = "new-horizons:estates.steward";

class StewardRecordingServer final : public IGameServer
{
	std::shared_ptr<CGameState> state;
	EServerState stateValue = EServerState::GAMEPLAY;
public:
	explicit StewardRecordingServer(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	std::optional<NewTurn> lastNewTurn;
	void setState(EServerState value) override { stateValue = value; }
	EServerState getState() const override { return stateValue; }
	bool isPlayerHost(const PlayerColor &) const override { return true; }
	bool hasPlayerAt(PlayerColor, GameConnectionID) const override { return true; }
	bool hasBothPlayersAtSameConnection(PlayerColor, PlayerColor) const override { return true; }
	void sendPack(CPackForClient &, GameConnectionID) override {}
	void applyPack(CPackForClient & pack) override
	{
		if(const auto * turn = dynamic_cast<const NewTurn *>(&pack))
			lastNewTurn = *turn;
		state->apply(pack);
	}
};

class NewHorizonsEstatesStewardTest : public TinyMapGameTest
{
protected:
	bool legacyPerks = false;
	int incomePercent = 100;
	CGTownInstance * town = nullptr;
	CGTownInstance * otherTown = nullptr;
	CGTownInstance * foreignTown = nullptr;
	CGHeroInstance * first = nullptr;
	CGHeroInstance * second = nullptr;
	CGHeroInstance * foreignHero = nullptr;
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons content";
	}
	void configurePlayer(PlayerSettings & settings) const override
	{
		settings.bonus = PlayerStartingBonus::GOLD;
		settings.handicap.percentIncome = incomePercent;
	}
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			legacyPerks ? JsonNode() : JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}
	void startGame()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER).playerActive(PlayerColor(1))
			.town({5, 5, 0}, FactionID::CASTLE, PLAYER)
			.town({9, 5, 0}, FactionID::CASTLE, PLAYER)
			.town({13, 5, 0}, FactionID::CASTLE, PlayerColor(1))
			.hero({30, 30, 0}, HeroTypeID(0), PLAYER)
			.hero({30, 26, 0}, HeroTypeID(1), PLAYER)
			.hero({30, 22, 0}, HeroTypeID(2), PlayerColor(1));
		startWithMap(std::move(builder));
		town = expectAt<CGTownInstance>({5, 5, 0});
		otherTown = expectAt<CGTownInstance>({9, 5, 0});
		foreignTown = expectAt<CGTownInstance>({13, 5, 0});
		first = findHeroAt({30, 30, 0});
		second = findHeroAt({30, 26, 0});
		foreignHero = findHeroAt({30, 22, 0});
		ASSERT_NE(town, nullptr);
		ASSERT_NE(otherTown, nullptr);
		ASSERT_NE(foreignTown, nullptr);
		ASSERT_NE(first, nullptr);
		ASSERT_NE(second, nullptr);
		ASSERT_NE(foreignHero, nullptr);
	}
	void selectSteward(CGHeroInstance * hero)
	{
		const SecondarySkill skill(SecondarySkill::decode(ESTATES));
		hero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({ESTATES, "new-horizons:estates.merchantPrince"});
		hero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({ESTATES, STEWARD});
		ASSERT_TRUE(hero->hasActivePerk(ESTATES, STEWARD));
	}
	int64_t ordinaryGold() const
	{
		int64_t total = 0;
		for(const auto * obj : gameState()->getPlayerState(PLAYER)->getOwnedObjects())
			total += obj->asOwnable()->dailyIncome()[EGameResID::GOLD];
		return total;
	}
	void verifyNextDay(int64_t expected)
	{
		StewardRecordingServer server(gameState());
		CGameHandler handler(server, gameState());
		while(gameState()->day < 1)
			handler.onNewTurn();
		server.lastNewTurn.reset();
		const auto dayBefore = gameState()->day;
		const auto goldBefore = gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD];
		handler.onNewTurn();
		ASSERT_TRUE(server.lastNewTurn);
		ASSERT_EQ(gameState()->day, dayBefore + 1);
		ASSERT_TRUE(server.lastNewTurn->playerIncome.contains(PLAYER));
		EXPECT_EQ(server.lastNewTurn->playerIncome.at(PLAYER)[EGameResID::GOLD], expected);
		EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD], goldBefore + expected);
	}
};
}

TEST_F(NewHorizonsEstatesStewardTest, VisitingHolderAdds250OnActualNextDay)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(selectSteward(first));
	const auto ordinary = ordinaryGold();
	town->setVisitingHero(first);
	ASSERT_EQ(first->getVisitedTown(), town);
	EXPECT_EQ(town->getStewardGoldBeforeHandicap(), 250);
	EXPECT_EQ(otherTown->getStewardGoldBeforeHandicap(), 0);
	ASSERT_NO_FATAL_FAILURE(verifyNextDay(ordinary + 250));
}

TEST_F(NewHorizonsEstatesStewardTest, GarrisonHolderAdds250OnActualNextDay)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(selectSteward(first));
	const auto ordinary = ordinaryGold();
	town->setGarrisonedHero(first);
	ASSERT_EQ(first->getVisitedTown(), town);
	EXPECT_EQ(town->getStewardGoldBeforeHandicap(), 250);
	ASSERT_NO_FATAL_FAILURE(verifyNextDay(ordinary + 250));
}

TEST_F(NewHorizonsEstatesStewardTest, TwoDistinctResidentsContribute500)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(selectSteward(first));
	ASSERT_NO_FATAL_FAILURE(selectSteward(second));
	const auto ordinary = ordinaryGold();
	town->setVisitingHero(first);
	town->setGarrisonedHero(second);
	EXPECT_EQ(town->getStewardGoldBeforeHandicap(), 500);
	ASSERT_NO_FATAL_FAILURE(verifyNextDay(ordinary + 500));
}

TEST_F(NewHorizonsEstatesStewardTest, DepartureBeforeDayEndAndAwayHolderGrantNothing)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(selectSteward(first));
	const auto ordinary = ordinaryGold();
	town->setVisitingHero(first);
	ASSERT_EQ(town->getStewardGoldBeforeHandicap(), 250);
	town->setVisitingHero(nullptr);
	ASSERT_EQ(first->getVisitedTown(), nullptr);
	EXPECT_EQ(town->getStewardGoldBeforeHandicap(), 0);
	ASSERT_NO_FATAL_FAILURE(verifyNextDay(ordinary));
}

TEST_F(NewHorizonsEstatesStewardTest, DuplicateResidentLinksCountTheSameHeroOnlyOnce)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(selectSteward(first));
	// Malformed duplicated links must not turn one selected holder into two.
	town->setVisitingHero(first);
	town->setGarrisonedHero(first);
	ASSERT_EQ(town->getVisitingHero(), town->getGarrisonHero());
	ASSERT_EQ(first->getVisitedTown(), town);
	EXPECT_EQ(town->getStewardGoldBeforeHandicap(), 250);
}

TEST_F(NewHorizonsEstatesStewardTest, ForeignResidentCannotRewardNonownedTown)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(selectSteward(first));
	const auto ordinary = ordinaryGold();
	foreignTown->setVisitingHero(first);
	ASSERT_NE(first->getOwner(), foreignTown->getOwner());
	EXPECT_EQ(foreignTown->getStewardGoldBeforeHandicap(), 0);
	ASSERT_NO_FATAL_FAILURE(verifyNextDay(ordinary));
}

TEST_F(NewHorizonsEstatesStewardTest, UnselectedResidentDoesNotGrantIncome)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	const auto ordinary = ordinaryGold();
	town->setVisitingHero(first);
	ASSERT_FALSE(first->hasActivePerk(ESTATES, STEWARD));
	EXPECT_EQ(town->getStewardGoldBeforeHandicap(), 0);
	ASSERT_NO_FATAL_FAILURE(verifyNextDay(ordinary));
}

TEST_F(NewHorizonsEstatesStewardTest, MissingSavedRegistryDoesNotUseInstalledSteward)
{
	legacyPerks = true;
	ASSERT_NO_FATAL_FAILURE(startGame());
	const auto ordinary = ordinaryGold();
	town->setVisitingHero(first);
	ASSERT_FALSE(first->hasActivePerk(ESTATES, STEWARD));
	EXPECT_EQ(town->getStewardGoldBeforeHandicap(), 0);
	ASSERT_NO_FATAL_FAILURE(verifyNextDay(ordinary));
}

TEST_F(NewHorizonsEstatesStewardTest, TownHandicapAppliesToCombinedIncomeAndMatchesPreview)
{
	incomePercent = 50;
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(selectSteward(first));
	const auto ordinary = ordinaryGold();
	const auto townBase = town->dailyIncome()[EGameResID::GOLD];
	town->setVisitingHero(first);
	EXPECT_EQ(town->getStewardGoldBeforeHandicap(), 250);
	const auto townWithSteward = town->dailyIncome()[EGameResID::GOLD];
	// A 250-Gold addition has an exact 125-Gold marginal at 50%, even
	// when the existing town subtotal is odd and handicap rounds upward.
	EXPECT_EQ(townWithSteward - townBase, 125);
	ASSERT_NO_FATAL_FAILURE(verifyNextDay(ordinary + townWithSteward - townBase));
}
