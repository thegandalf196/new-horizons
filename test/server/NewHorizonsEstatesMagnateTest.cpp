/*
 * NewHorizonsEstatesMagnateTest.cpp, part of VCMI engine
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"
#include "../mock/GameHandlerTestServer.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "battles/FullGameSnapshotTypes.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/GameConstants.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForLobby.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../server/CGameHandler.h"
#include "../../server/queries/QueriesProcessor.h"
#include <optional>
#include <vstd/ContainerUtils.h>

namespace
{
const PlayerColor PLAYER(0);
constexpr auto ESTATES = "new-horizons:estates";
constexpr auto MAGNATE = "new-horizons:estates.magnate";
using MagnateIncome = newHorizonsEconomy::MagnateIncome;

ESerializationVersion beforeMagnate()
{
	return static_cast<ESerializationVersion>(static_cast<int>(ESerializationVersion::NEW_HORIZONS_MAGNATE) - 1);
}

class MagnateRecordingServer : public GameHandlerTestServer
{
public:
	explicit MagnateRecordingServer(std::shared_ptr<CGameState> state) : GameHandlerTestServer(std::move(state), PLAYER) {}
	std::optional<NewTurn> turn;
	std::optional<HeroVisit> visit;
	void applyPack(CPackForClient & pack) override
	{
		if(const auto * value = dynamic_cast<const NewTurn *>(&pack))
			turn = *value;
		if(const auto * value = dynamic_cast<const HeroVisit *>(&pack); value && value->starting)
			visit = *value;
		GameHandlerTestServer::applyPack(pack);
	}
};

class NewHorizonsEstatesMagnateTest : public TinyMapGameTest
{
protected:
	bool currentRules = true;
	CGTownInstance * firstTown = nullptr;
	CGTownInstance * lastTown = nullptr;
	CGHeroInstance * first = nullptr;
	CGHeroInstance * second = nullptr;
	std::unique_ptr<MagnateRecordingServer> server;
	std::unique_ptr<CGameHandler> handler;
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}
	void TearDown() override
	{
		handler.reset();
		server.reset();
		TinyMapGameTest::TearDown();
	}
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		JsonNode days;
		days.Integer() = 7;
		loaded->overrideGameSetting(EGameSettings::GENERAL_DAYS_PER_WEEK, days);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			currentRules ? JsonNode(JsonPath::builtin("config/newHorizonsMagic")) : JsonNode());
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			currentRules ? JsonNode(JsonPath::builtin("config/newHorizonsPerks")) : JsonNode());
	}
	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER).playerActive(PlayerColor(1))
			.town({7, 5, 0}, FactionID::CASTLE, PLAYER)
			.town({14, 5, 0}, FactionID::CASTLE, PLAYER)
			.townGarrison({})
			.town({26, 26, 0}, FactionID::CASTLE, PlayerColor(1))
			// Keep the initial hero off the town footprint; map initialization
			// otherwise moves it to the entrance via the H3M town-placement quirk.
			.hero({4, 12, 0}, HeroTypeID(0), PLAYER)
			.hero({4, 20, 0}, HeroTypeID(1), PLAYER);
		startWithMap(std::move(builder));
		firstTown = expectAt<CGTownInstance>({7, 5, 0});
		lastTown = expectAt<CGTownInstance>({14, 5, 0});
		first = findHeroAt({4, 12, 0});
		second = findHeroAt({4, 20, 0});
		ASSERT_NE(firstTown, nullptr);
		ASSERT_NE(lastTown, nullptr);
		ASSERT_NE(first, nullptr);
		ASSERT_NE(second, nullptr);
		server = std::make_unique<MagnateRecordingServer>(gameState());
		handler = std::make_unique<CGameHandler>(*server, gameState());
		advanceTo(1);
	}
	void selectMagnate(CGHeroInstance * hero)
	{
		const SecondarySkill skill(SecondarySkill::decode(ESTATES));
		hero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({ESTATES, "new-horizons:estates.merchantPrince"});
		hero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({ESTATES, "new-horizons:estates.resourceBroker"});
		hero->setSecSkillLevel(skill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({ESTATES, MAGNATE});
		ASSERT_TRUE(hero->hasActivePerk(ESTATES, MAGNATE));
	}
	void advanceTo(int day)
	{
		while(gameState()->day < day)
			handler->onNewTurn();
	}
	void visit(CGHeroInstance * hero, CGTownInstance * town)
	{
		if(auto * previous = hero->getVisitedTown())
			previous->setVisitingHero(nullptr);
		// Establish a legal entry position as fixture setup; the visit, receipt,
		// building effects and eventual award all use ordinary authority.
		hero->pos = town->visitablePos() + hero->getVisitableOffset();
		handler->objectVisited(town, hero);
		while(const auto query = handler->queries->topQuery(PLAYER))
			handler->queries->popQuery(query);
		if(town->getVisitingHero() == hero)
			town->setVisitingHero(nullptr);
	}
	int64_t ordinaryGold() const
	{
		int64_t result = 0;
		for(const auto * object : gameState()->getPlayerState(PLAYER)->getOwnedObjects())
			result += object->asOwnable()->dailyIncome()[EGameResID::GOLD];
		return result;
	}
};
}

TEST_F(NewHorizonsEstatesMagnateTest, MostRecentOwnedVisitAwards500BeforeFirstWeeklyIncome)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(selectMagnate(first));
	visit(first, firstTown);
	visit(first, lastTown);
	EXPECT_EQ(first->getNewHorizonsMagnateLastTown(), lastTown->id);
	EXPECT_EQ(first->getNewHorizonsMagnateVisitWeek(), 0);
	ASSERT_TRUE(server->visit);
	EXPECT_EQ(server->visit->newHorizonsMagnateOwnedTownVisitWeek, 0);
	advanceTo(7);
	const auto ordinary = ordinaryGold();
	const auto treasury = gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD];
	handler->onNewTurn();
	ASSERT_TRUE(server->turn);
	EXPECT_EQ(firstTown->getMagnateGoldBeforeHandicap(), 0);
	EXPECT_EQ(lastTown->getMagnateGoldBeforeHandicap(), 500);
	EXPECT_EQ(lastTown->getNewHorizonsMagnateIncome().startDay, 8);
	EXPECT_EQ(server->turn->playerIncome.at(PLAYER)[EGameResID::GOLD], ordinary + 500);
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD], treasury + ordinary + 500);
}

TEST_F(NewHorizonsEstatesMagnateTest, CaptureDoesNotInventOwnedVisitAndUnselectedHolderDoesNotAward)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(selectMagnate(first));
	visit(second, firstTown);
	handler->setOwner(lastTown, PlayerColor(1));
	visit(first, lastTown); // Owned only after onHeroVisit captures it.
	ASSERT_EQ(lastTown->getOwner(), PLAYER);
	ASSERT_TRUE(server->visit);
	EXPECT_EQ(server->visit->newHorizonsMagnateOwnedTownVisitWeek, -1);
	advanceTo(8);
	EXPECT_EQ(first->getNewHorizonsMagnateLastTown(), ObjectInstanceID::NONE);
	EXPECT_EQ(firstTown->getMagnateGoldBeforeHandicap(), 0);
	EXPECT_EQ(lastTown->getMagnateGoldBeforeHandicap(), 0);
}

TEST_F(NewHorizonsEstatesMagnateTest, ActualVisitBeforeAcquisitionCanQualifyWithoutInventingHistory)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	visit(first, lastTown);
	ASSERT_EQ(first->getNewHorizonsMagnateLastTown(), lastTown->id);
	ASSERT_FALSE(first->hasActivePerk(ESTATES, MAGNATE));
	ASSERT_NO_FATAL_FAILURE(selectMagnate(first));
	advanceTo(8);
	EXPECT_EQ(lastTown->getMagnateGoldBeforeHandicap(), 500);
}

TEST_F(NewHorizonsEstatesMagnateTest, MostRecentTownLostBeforeWeekStartDoesNotFallBack)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(selectMagnate(first));
	visit(first, firstTown);
	visit(first, lastTown);
	handler->setOwner(lastTown, PlayerColor(1));
	advanceTo(8);
	EXPECT_EQ(firstTown->getMagnateGoldBeforeHandicap(), 0);
	EXPECT_EQ(lastTown->getMagnateGoldBeforeHandicap(), 0);
}

TEST_F(NewHorizonsEstatesMagnateTest, TwoHoldersAdd1000ToSameTown)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(selectMagnate(first));
	ASSERT_NO_FATAL_FAILURE(selectMagnate(second));
	visit(first, lastTown);
	visit(second, lastTown);
	advanceTo(8);
	EXPECT_EQ(lastTown->getMagnateGoldBeforeHandicap(), 1000);
	EXPECT_EQ(lastTown->getNewHorizonsMagnateIncome().awardOwner, PLAYER);
}

TEST_F(NewHorizonsEstatesMagnateTest, SevenDaysThenExpiryWithoutNewVisit)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(selectMagnate(first));
	visit(first, lastTown);
	advanceTo(8);
	for(int day = 8; day <= 14; ++day)
	{
		advanceTo(day);
		EXPECT_EQ(lastTown->getMagnateGoldBeforeHandicap(), 500) << day;
	}
	advanceTo(15);
	EXPECT_EQ(lastTown->getMagnateGoldBeforeHandicap(), 0);
	EXPECT_TRUE(lastTown->getNewHorizonsMagnateIncome().empty());
}

TEST_F(NewHorizonsEstatesMagnateTest, RemovedGrantingHeroDoesNotCancelTownAward)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(selectMagnate(first));
	visit(first, lastTown);
	advanceTo(8);
	const auto id = first->id;
	ASSERT_TRUE(handler->removeObject(first, PLAYER));
	first = nullptr;
	ASSERT_EQ(gameState()->getHero(id), nullptr);
	advanceTo(9);
	EXPECT_EQ(lastTown->getMagnateGoldBeforeHandicap(), 500);
}

TEST_F(NewHorizonsEstatesMagnateTest, CapturePausesAndSameOwnerReacquisitionResumesOriginalClock)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(selectMagnate(first));
	visit(first, lastTown);
	advanceTo(8);
	handler->setOwner(lastTown, PlayerColor(1));
	EXPECT_EQ(lastTown->getMagnateGoldBeforeHandicap(), 0);
	advanceTo(10);
	handler->setOwner(lastTown, PLAYER);
	EXPECT_EQ(lastTown->getMagnateGoldBeforeHandicap(), 500);
	EXPECT_EQ(lastTown->getNewHorizonsMagnateIncome().startDay, 8);
	advanceTo(15);
	EXPECT_EQ(lastTown->getMagnateGoldBeforeHandicap(), 0);
}

TEST_F(NewHorizonsEstatesMagnateTest, CurrentSnapshotRoundTripPreservesVisitAndFixedTownAward)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(selectMagnate(first));
	visit(first, lastTown);
	advanceTo(8);
	const auto heroID = first->id;
	const auto townID = lastTown->id;
	const auto snapshot = lastTown->getNewHorizonsMagnateIncome();
	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	ASSERT_NE(restored.getHero(heroID), nullptr);
	ASSERT_NE(restored.getTown(townID), nullptr);
	EXPECT_EQ(restored.getHero(heroID)->getNewHorizonsMagnateLastTown(), townID);
	EXPECT_EQ(restored.getHero(heroID)->getNewHorizonsMagnateVisitWeek(), 0);
	EXPECT_EQ(restored.getTown(townID)->getNewHorizonsMagnateIncome(), snapshot);
	EXPECT_EQ(restored.getTown(townID)->getMagnateGoldBeforeHandicap(), 500);
}

TEST_F(NewHorizonsEstatesMagnateTest, LegacyRulesDoNotCaptureNewVisitHistory)
{
	currentRules = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	visit(first, lastTown);
	EXPECT_EQ(first->getNewHorizonsMagnateLastTown(), ObjectInstanceID::NONE);
	EXPECT_EQ(first->getNewHorizonsMagnateVisitWeek(), -1);
	advanceTo(8);
	EXPECT_EQ(lastTown->getMagnateGoldBeforeHandicap(), 0);
}

TEST_F(NewHorizonsEstatesMagnateTest, OldWritersRejectMeaningfulVisitAndNewTurnBeforePrefix)
{
	HeroVisit visit;
	visit.player = PLAYER;
	visit.heroId = ObjectInstanceID(0);
	visit.objId = ObjectInstanceID(1);
	visit.starting = true;
	visit.newHorizonsMagnateOwnedTownVisitWeek = 0;
	CMemorySerializer visitWriter;
	visitWriter.oser.version = beforeMagnate();
	EXPECT_THROW(visitWriter.oser & visit, std::runtime_error);
	EXPECT_TRUE(visitWriter.extractBuffer().empty());
	visit.newHorizonsMagnateOwnedTownVisitWeek = -1;
	CMemorySerializer ordinary;
	ordinary.oser.version = beforeMagnate();
	EXPECT_NO_THROW(ordinary.oser & visit);
	const auto ordinaryBytes = ordinary.extractBuffer();
	ASSERT_FALSE(ordinaryBytes.empty());
	CMemorySerializer incoming(ordinaryBytes);
	incoming.iser.version = beforeMagnate();
	HeroVisit decoded;
	EXPECT_NO_THROW(incoming.iser & decoded);
	EXPECT_EQ(decoded.newHorizonsMagnateOwnedTownVisitWeek, -1);
	NewTurn turn;
	turn.day = 8;
	turn.newHorizonsMagnateTownIncome.emplace(ObjectInstanceID(1), MagnateIncome{500, 8, PLAYER});
	CMemorySerializer turnWriter;
	turnWriter.oser.version = beforeMagnate();
	EXPECT_THROW(turnWriter.oser & turn, std::runtime_error);
	EXPECT_TRUE(turnWriter.extractBuffer().empty());
}

TEST_F(NewHorizonsEstatesMagnateTest, HeroTownMapWorldLobbyRejectOldPrefixAndOrdinaryControlsRemainWritable)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	LobbyStartGame lobby;
	lobby.initializedStartInfo = std::make_shared<StartInfo>(*gameState()->getStartInfo());
	lobby.initializedGameState = gameState();
	CMemorySerializer plain;
	plain.oser.version = beforeMagnate();
	EXPECT_NO_THROW(plain.oser & *gameState());
	EXPECT_FALSE(plain.extractBuffer().empty());
	visit(first, lastTown);
	lastTown->setNewHorizonsMagnateIncome({500, 8, PLAYER});
	auto reject = [](auto & value)
	{
		CMemorySerializer writer;
		writer.oser.version = beforeMagnate();
		EXPECT_THROW(writer.oser & value, std::runtime_error);
		EXPECT_TRUE(writer.extractBuffer().empty());
	};
	reject(*first);
	reject(*lastTown);
	reject(gameState()->getMap());
	reject(*gameState());
	reject(lobby);
}
