/*
 * NewHorizonsEstatesLandSurveyorTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"

#include <algorithm>
#include <optional>

#include "../../lib/CPlayerState.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/GameConstants.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/ResourceSet.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/entities/creature/NewHorizonsMusterRules.h"
#include "../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/MiscObjects.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../server/CGameHandler.h"
#include "../../server/IGameServer.h"
#include "../../server/queries/QueriesProcessor.h"
#include "../mock/GameHandlerTestServer.h"
#include "../mock/TinyMapGameTest.h"

namespace
{
const PlayerColor PLAYER(0);
constexpr auto ESTATES_SKILL = "new-horizons:estates";
constexpr auto LAND_SURVEYOR = "new-horizons:estates.landSurveyor";
constexpr MapObjectSubID WOOD_MINE_SUBID(0);

class RecordingGameServer final : public IGameServer
{
public:
	explicit RecordingGameServer(std::shared_ptr<CGameState> state)
		: state(std::move(state))
	{}

	void setState(EServerState value) override { stateValue = value; }
	EServerState getState() const override { return stateValue; }
	bool isPlayerHost(const PlayerColor &) const override { return true; }
	bool hasPlayerAt(PlayerColor, GameConnectionID) const override { return true; }
	bool hasBothPlayersAtSameConnection(PlayerColor, PlayerColor) const override { return true; }

	void applyPack(CPackForClient & pack) override
	{
		if(const auto * property = dynamic_cast<const SetObjectProperty *>(&pack);
			property && property->what == ObjProperty::NEW_HORIZONS_LAND_SURVEYOR_LAST_WEEK)
			lastLandSurveyorMarker = *property;
		state->apply(pack);
	}

	void sendPack(CPackForClient &, GameConnectionID) override {}

	std::optional<SetObjectProperty> lastLandSurveyorMarker;

private:
	EServerState stateValue = EServerState::GAMEPLAY;
	std::shared_ptr<CGameState> state;
};

class NewHorizonsEstatesLandSurveyorTest : public TinyMapGameTest
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
		JsonNode daysPerWeek;
		daysPerWeek.Integer() = 7;
		loaded->overrideGameSetting(EGameSettings::GENERAL_DAYS_PER_WEEK, daysPerWeek);
		JsonNode weeksPerMonth;
		weeksPerMonth.Integer() = 4;
		loaded->overrideGameSetting(EGameSettings::GENERAL_WEEKS_PER_MONTH, weeksPerMonth);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));

		// Land Surveyor is still planned in the shared rules. Activate it only in
		// this saved-world snapshot so the offer and accepted server path are testable.
		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		for(auto & perk : perkRules["skills"][ESTATES_SKILL]["perks"].Vector())
			if(perk["id"].String() == LAND_SURVEYOR)
				perk["effect"]["status"].String() = "active";
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));
	}

	void startGame()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER)
			.hero({4, 5, 0}, HeroTypeID(0), PLAYER)
			.hero({4, 24, 0}, HeroTypeID(1), PLAYER)
			.mine({10, 5, 0}, WOOD_MINE_SUBID, PlayerColor::NEUTRAL)
			.mine({18, 5, 0}, WOOD_MINE_SUBID, PlayerColor::NEUTRAL)
			.mine({26, 5, 0}, WOOD_MINE_SUBID, PlayerColor::NEUTRAL)
			.mine({28, 29, 0}, WOOD_MINE_SUBID, PLAYER)
			.mine({18, 24, 0}, WOOD_MINE_SUBID, PlayerColor::NEUTRAL);
		startWithMap(std::move(builder));

		firstHero = findHeroAt({4, 5, 0});
		secondHero = findHeroAt({4, 24, 0});
		firstMine = findObjectAt({10, 5, 0}) ? dynamic_cast<CGMine *>(findObjectAt({10, 5, 0})) : nullptr;
		secondMine = findObjectAt({18, 5, 0}) ? dynamic_cast<CGMine *>(findObjectAt({18, 5, 0})) : nullptr;
		thirdMine = findObjectAt({26, 5, 0}) ? dynamic_cast<CGMine *>(findObjectAt({26, 5, 0})) : nullptr;
		ownedMine = findObjectAt({28, 29, 0}) ? dynamic_cast<CGMine *>(findObjectAt({28, 29, 0})) : nullptr;
		unskilledMine = findObjectAt({18, 24, 0}) ? dynamic_cast<CGMine *>(findObjectAt({18, 24, 0})) : nullptr;
		ASSERT_NE(firstHero, nullptr);
		ASSERT_NE(secondHero, nullptr);
		ASSERT_NE(firstMine, nullptr);
		ASSERT_NE(secondMine, nullptr);
		ASSERT_NE(thirdMine, nullptr);
		ASSERT_NE(ownedMine, nullptr);
		ASSERT_NE(unskilledMine, nullptr);
	}

	static SecondarySkill estatesSkill()
	{
		const int decoded = SecondarySkill::decode(ESTATES_SKILL);
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	bool acceptLandSurveyor(CGameHandler & handler, CGHeroInstance * hero)
	{
		handler.changeSecSkill(hero, estatesSkill(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		if(hero->getSecSkillLevel(estatesSkill()) != MasteryLevel::BASIC)
			return false;

		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};
		for(uint64_t offerSeed = 0; offerSeed < 64; ++offerSeed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, offerSeed);
			const auto selected = std::ranges::find_if(offer, [](const auto & candidate)
			{
				return candidate.selection.perkId == LAND_SURVEYOR;
			});
			if(selected == offer.end())
				continue;
			if(selected->requiredRank != MasteryLevel::BASIC)
				return false;
			handler.levelUpHero(hero, offer, static_cast<size_t>(std::distance(offer.begin(), selected)), offerSeed, false);
			return hero->hasActivePerk(ESTATES_SKILL, LAND_SURVEYOR);
		}
		return false;
	}

	void captureMine(CGameHandler & handler, CGHeroInstance * hero, CGMine * mine)
	{
		handler.setMovePoints(hero->id, 20000);
		const int3 destination = hero->convertFromVisitablePos(mine->visitablePos());
		for(int step = 0; step < 64 && mine->getOwner() != hero->getOwner(); ++step)
		{
			ASSERT_NE(hero->pos, destination) << "Arrived without capturing the mine";
			int3 next = hero->pos;
			// Travel on the clear row below the mines, then approach the target.
			// Crossing an already-owned mine would open its garrison dialog.
			if(next.x != destination.x && next.y != destination.y + 1)
				next.y += destination.y + 1 > next.y ? 1 : -1;
			else if(next.x != destination.x)
				next.x += destination.x > next.x ? 1 : -1;
			else if(next.y != destination.y)
				next.y += destination.y > next.y ? 1 : -1;
			ASSERT_TRUE(handler.moveHero(hero->id, next, EMovementMode::STANDARD, false,
				hero->getOwner(), EPathfindingLayer::LAND)) << "Accepted adjacent movement step failed";
		}
		EXPECT_EQ(mine->getOwner(), hero->getOwner());
	}

	static int absoluteWeek(const CGameState & state)
	{
		const auto calendar = state.getCalendar();
		return newHorizonsMuster::absoluteWeek(calendar.getCurrentDay(), calendar.getDaysInWeek());
	}

	CGHeroInstance * firstHero = nullptr;
	CGHeroInstance * secondHero = nullptr;
	CGMine * firstMine = nullptr;
	CGMine * secondMine = nullptr;
	CGMine * thirdMine = nullptr;
	CGMine * ownedMine = nullptr;
	CGMine * unskilledMine = nullptr;
};
} // namespace

TEST_F(NewHorizonsEstatesLandSurveyorTest, LandSurveyorIsAnAcceptedBasicOffer)
{
	startGame();
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());

	ASSERT_TRUE(acceptLandSurveyor(handler, firstHero));

	EXPECT_EQ(firstHero->getNewHorizonsLandSurveyorLastWeek(), -1);
	EXPECT_FALSE(firstHero->hasUsedNewHorizonsLandSurveyor(0));
}

TEST_F(NewHorizonsEstatesLandSurveyorTest, FirstCapturedMinePaysThreeDaysOncePerHeroWeekAndPersists)
{
	startGame();
	RecordingGameServer server(gameState());
	CGameHandler handler(server, gameState());
	ASSERT_TRUE(acceptLandSurveyor(handler, firstHero));

	auto * playerState = gameState()->getPlayerState(PLAYER);
	const ResourceSet beforeFirstCapture = playerState->resources;
	captureMine(handler, firstHero, firstMine);
	const ResourceSet firstOutput = firstMine->dailyIncome() * 3;
	const int firstWeek = absoluteWeek(*gameState());
	ASSERT_TRUE(firstHero->hasUsedNewHorizonsLandSurveyor(firstWeek));
	EXPECT_EQ(playerState->resources - beforeFirstCapture, firstOutput);
	EXPECT_EQ(makeCallback(PLAYER)->getResourceAmount(), playerState->resources);
	ASSERT_TRUE(server.lastLandSurveyorMarker);
	EXPECT_EQ(server.lastLandSurveyorMarker->identifier.as<NumericID>().getNum(), firstWeek);

	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	auto * restoredHero = restored.getHero(firstHero->id);
	ASSERT_NE(restoredHero, nullptr);
	EXPECT_EQ(restoredHero->getNewHorizonsLandSurveyorLastWeek(), firstWeek);

	// Reapply the actual generic object-property packet to a loaded mirror.
	SetObjectProperty clearMarker;
	clearMarker.id = firstHero->id;
	clearMarker.what = ObjProperty::NEW_HORIZONS_LAND_SURVEYOR_LAST_WEEK;
	clearMarker.identifier = NumericID(-1);
	restored.apply(clearMarker);
	EXPECT_EQ(restoredHero->getNewHorizonsLandSurveyorLastWeek(), -1);
	auto markerCopy = CMemorySerializer::deepCopy<CPackForClient>(*server.lastLandSurveyorMarker);
	auto * replicatedMarker = dynamic_cast<SetObjectProperty *>(markerCopy.get());
	ASSERT_NE(replicatedMarker, nullptr);
	restored.apply(*replicatedMarker);
	EXPECT_EQ(restoredHero->getNewHorizonsLandSurveyorLastWeek(), firstWeek);

	const ResourceSet beforeSecondCapture = playerState->resources;
	captureMine(handler, firstHero, secondMine);
	EXPECT_TRUE(firstHero->hasUsedNewHorizonsLandSurveyor(firstWeek));
	EXPECT_EQ(playerState->resources, beforeSecondCapture);
	EXPECT_EQ(makeCallback(PLAYER)->getResourceAmount(), beforeSecondCapture);

	while(gameState()->day < 7)
		handler.onNewTurn();
	handler.onNewTurn();
	ASSERT_EQ(gameState()->day, 8u);
	const int nextWeek = absoluteWeek(*gameState());
	ASSERT_NE(nextWeek, firstWeek);
	const ResourceSet beforeNextWeekCapture = playerState->resources;
	captureMine(handler, firstHero, thirdMine);
	const ResourceSet nextOutput = thirdMine->dailyIncome() * 3;
	EXPECT_TRUE(firstHero->hasUsedNewHorizonsLandSurveyor(nextWeek));
	EXPECT_EQ(playerState->resources - beforeNextWeekCapture, nextOutput);
	EXPECT_EQ(makeCallback(PLAYER)->getResourceAmount(), playerState->resources);
}

TEST_F(NewHorizonsEstatesLandSurveyorTest, OwnMineVisitsAndCapturesWithoutThePerkDoNotPay)
{
	startGame();
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	ASSERT_TRUE(acceptLandSurveyor(handler, firstHero));

	auto * playerState = gameState()->getPlayerState(PLAYER);
	const ResourceSet beforeOwnVisit = playerState->resources;
	handler.objectVisited(ownedMine, firstHero);
	while(const auto query = handler.queries->topQuery(PLAYER))
		handler.queries->popQuery(query);
	EXPECT_EQ(playerState->resources, beforeOwnVisit);
	EXPECT_EQ(firstHero->getNewHorizonsLandSurveyorLastWeek(), -1);

	const ResourceSet beforeUnskilledCapture = playerState->resources;
	EXPECT_FALSE(secondHero->hasActivePerk(ESTATES_SKILL, LAND_SURVEYOR));
	captureMine(handler, secondHero, unskilledMine);
	EXPECT_EQ(playerState->resources, beforeUnskilledCapture);
	EXPECT_EQ(secondHero->getNewHorizonsLandSurveyorLastWeek(), -1);
}
