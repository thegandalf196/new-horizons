/*
 * NewHorizonsAcademicStudyTest.cpp, part of VCMI engine
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
#include "../../lib/entities/hero/CHeroHandler.h"
#include "../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../lib/mapObjects/NewHorizonsAcademicStudy.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../server/CGameHandler.h"
#include "../../server/queries/QueriesProcessor.h"
#include <vstd/ContainerUtils.h>

namespace
{
const PlayerColor PLAYER(0);
constexpr auto LEARNING = "new-horizons:learning";
constexpr auto ACADEMIC = "new-horizons:learning.academicStudy";
class StudyRecordingServer : public GameHandlerTestServer
{
public:
	explicit StudyRecordingServer(std::shared_ptr<CGameState> state) : GameHandlerTestServer(std::move(state), PLAYER) {}
	CGHeroInstance * recipient = nullptr;
	CGTownInstance * destination = nullptr;
	int markers = 0;
	bool markerBeforeExperience = false;
	void applyPack(CPackForClient & pack) override
	{
		if(const auto * marker = dynamic_cast<const ChangeObjectVisitors *>(&pack);
			marker && marker->mode == ChangeObjectVisitors::VISITOR_ADD_HERO_ONLY)
			++markers;
		if(const auto * experience = dynamic_cast<const SetHeroExperience *>(&pack);
			experience && recipient && destination && experience->id == recipient->id)
			markerBeforeExperience = recipient->visitedObjects.contains(destination->id);
		GameHandlerTestServer::applyPack(pack);
	}
};
class NewHorizonsAcademicStudyTest : public TinyMapGameTest
{
protected:
	bool currentRules = true;
	CGHeroInstance * hero = nullptr;
	CGHeroInstance * second = nullptr;
	std::array<CGTownInstance *, 4> towns{};
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons content";
	}
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			currentRules ? JsonNode(JsonPath::builtin("config/newHorizonsMagic")) : JsonNode());
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			currentRules ? JsonNode(JsonPath::builtin("config/newHorizonsPerks")) : JsonNode());
	}
	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER)
			.hero({4, 4, 0}, HeroTypeID(0), PLAYER)
			.hero({4, 20, 0}, HeroTypeID(1), PLAYER);
		for(int index = 0; index < 4; ++index)
			builder.town({8 + 6 * index, 10, 0}, FactionID::TOWER, PLAYER).townGarrison({});
		startWithMap(std::move(builder));
		hero = findHeroAt({4, 4, 0});
		second = findHeroAt({4, 20, 0});
		ASSERT_NE(hero, nullptr);
		ASSERT_NE(second, nullptr);
		for(int index = 0; index < 4; ++index)
		{
			towns[index] = expectAt<CGTownInstance>({8 + 6 * index, 10, 0});
			ASSERT_NE(towns[index], nullptr);
			setGuild(towns[index], std::array<int, 4>{0, 1, 3, 5}[index]);
		}
	}
	void setGuild(CGTownInstance * town, int level)
	{
		for(int index = 0; index < 5; ++index)
			town->removeBuilding(BuildingID(BuildingID::MAGES_GUILD_1 + index));
		for(int index = 0; index < level; ++index)
			town->addBuilding(BuildingID(BuildingID::MAGES_GUILD_1 + index));
		ASSERT_EQ(town->mageGuildLevel(), level);
	}
	void selectAcademic(CGHeroInstance * target)
	{
		const SecondarySkill skill(SecondarySkill::decode(LEARNING));
		target->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		target->applyPerkSelection({LEARNING, "new-horizons:learning.eagleEye"});
		target->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		target->applyPerkSelection({LEARNING, ACADEMIC});
		ASSERT_TRUE(target->hasActivePerk(LEARNING, ACADEMIC));
	}
	void leave(CGHeroInstance * target)
	{
		if(auto * previous = target->getVisitedTown())
			previous->setVisitingHero(nullptr);
	}
};
}

TEST_F(NewHorizonsAcademicStudyTest, ZeroOneThreeFiveBuiltGuildLevelsUseActualRecipientLearning)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(selectAcademic(hero));
	hero->level = 20;
	hero->exp = LIBRARY->heroh->reqExp(20);
	StudyRecordingServer server(gameState());
	CGameHandler handler(server, gameState());
	for(int index = 0; index < 4; ++index)
	{
		leave(hero);
		const int levels = std::array<int, 4>{0, 1, 3, 5}[index];
		const auto before = hero->exp;
		const auto expected = hero->calculateXp(250 * levels);
		EXPECT_EQ(newHorizonsLearning::academicStudyRawExperience(*hero, *towns[index]), 250 * levels);
		handler.heroVisitCastle(towns[index], hero);
		EXPECT_EQ(hero->exp, before + expected);
		EXPECT_TRUE(hero->visitedObjects.contains(towns[index]->id));
	}
	EXPECT_EQ(server.markers, 4);
}

TEST_F(NewHorizonsAcademicStudyTest, RepeatAndLaterGuildBuildCannotAwardAgain)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(selectAcademic(hero));
	StudyRecordingServer server(gameState());
	CGameHandler handler(server, gameState());
	handler.heroVisitCastle(towns[0], hero);
	const auto before = hero->exp;
	setGuild(towns[0], 5);
	leave(hero);
	handler.heroVisitCastle(towns[0], hero);
	EXPECT_EQ(hero->exp, before);
	EXPECT_EQ(server.markers, 1);
	EXPECT_EQ(newHorizonsLearning::academicStudyExperience(*hero, *towns[0]), 0);
}

TEST_F(NewHorizonsAcademicStudyTest, ProspectivePreAcquisitionVisitConsumesOnlyThatTown)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	StudyRecordingServer server(gameState());
	CGameHandler handler(server, gameState());
	const auto before = hero->exp;
	handler.heroVisitCastle(towns[1], hero);
	EXPECT_EQ(hero->exp, before);
	ASSERT_NO_FATAL_FAILURE(selectAcademic(hero));
	leave(hero);
	handler.heroVisitCastle(towns[1], hero);
	EXPECT_EQ(hero->exp, before);
	leave(hero);
	const auto expected = hero->calculateXp(750);
	handler.heroVisitCastle(towns[2], hero);
	EXPECT_EQ(hero->exp, before + expected);
}

TEST_F(NewHorizonsAcademicStudyTest, MarkerPrecedesLevelUpAndDoesNotPollutePlayerTeamOrBuildingHistory)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(selectAcademic(hero));
	hero->level = 1;
	hero->exp = 0;
	StudyRecordingServer server(gameState());
	server.recipient = hero;
	server.destination = towns[3];
	CGameHandler handler(server, gameState());
	// Exercise the actual level-up dialog path, not a deferred UI-unready query.
	handler.onAdvInterfaceReady(PLAYER);
	const auto playerVisits = gameState()->getPlayerState(PLAYER)->visitedObjects;
	const auto globalVisits = gameState()->getPlayerState(PLAYER)->visitedObjectsGlobal;
	const auto teamScouted = gameState()->getPlayerTeam(PLAYER)->scoutedObjects;
	const auto expected = hero->calculateXp(1250);
	handler.heroVisitCastle(towns[3], hero);
	EXPECT_EQ(hero->exp, expected);
	EXPECT_TRUE(server.markerBeforeExperience);
	EXPECT_GT(hero->level, 1u);
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->visitedObjects, playerVisits);
	EXPECT_TRUE(std::ranges::equal(gameState()->getPlayerState(PLAYER)->visitedObjectsGlobal,
		globalVisits, [](const auto & lhs, const auto & rhs)
		{ return lhs.id == rhs.id && lhs.subID == rhs.subID; }));
	EXPECT_EQ(gameState()->getPlayerTeam(PLAYER)->scoutedObjects, teamScouted);
	// An internal repeated town/building callback does not count as arrival.
	handler.heroVisitCastle(towns[3], hero);
	EXPECT_EQ(hero->exp, expected);
	EXPECT_EQ(server.markers, 1);
}

TEST_F(NewHorizonsAcademicStudyTest, EachHeroHasIndependentTownHistoryAndCurrentSavePreservesIt)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(selectAcademic(hero));
	ASSERT_NO_FATAL_FAILURE(selectAcademic(second));
	StudyRecordingServer server(gameState());
	CGameHandler handler(server, gameState());
	handler.heroVisitCastle(towns[1], hero);
	leave(hero);
	const auto expected = second->calculateXp(250);
	const auto secondBefore = second->exp;
	handler.heroVisitCastle(towns[1], second);
	EXPECT_EQ(second->exp, secondBefore + expected);
	const auto heroID = hero->id;
	const auto townID = towns[1]->id;
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	ASSERT_NE(restored.getHero(heroID), nullptr);
	ASSERT_NE(restored.getTown(townID), nullptr);
	EXPECT_TRUE(restored.getHero(heroID)->visitedObjects.contains(townID));
	EXPECT_EQ(newHorizonsLearning::academicStudyExperience(*restored.getHero(heroID), *restored.getTown(townID)), 0);
}

TEST_F(NewHorizonsAcademicStudyTest, LegacyUnselectedVisitDoesNotAddNewMarkerOrExperience)
{
	currentRules = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	StudyRecordingServer server(gameState());
	CGameHandler handler(server, gameState());
	const auto before = hero->exp;
	handler.heroVisitCastle(towns[3], hero);
	EXPECT_EQ(hero->exp, before);
	EXPECT_FALSE(hero->visitedObjects.contains(towns[3]->id));
	EXPECT_EQ(server.markers, 0);
}

TEST_F(NewHorizonsAcademicStudyTest, HeroOnlyPacketRoundtripAndOldFormatPrefixGuardPreserveLegacyMode)
{
	ChangeObjectVisitors marker(ChangeObjectVisitors::VISITOR_ADD_HERO_ONLY, ObjectInstanceID(1), ObjectInstanceID(0));
	const auto oldVersion = static_cast<ESerializationVersion>(
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_ACADEMIC_STUDY) - 1);
	CMemorySerializer old;
	old.oser.version = oldVersion;
	EXPECT_THROW(old.oser & marker, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	const auto copied = CMemorySerializer::deepCopy<CPackForClient>(marker);
	const auto * decoded = dynamic_cast<const ChangeObjectVisitors *>(copied.get());
	ASSERT_NE(decoded, nullptr);
	EXPECT_EQ(decoded->mode, ChangeObjectVisitors::VISITOR_ADD_HERO_ONLY);
	EXPECT_EQ(decoded->object, marker.object);
	EXPECT_EQ(decoded->hero, marker.hero);
	marker.mode = ChangeObjectVisitors::VISITOR_ADD_HERO;
	CMemorySerializer legacy;
	legacy.oser.version = oldVersion;
	EXPECT_NO_THROW(legacy.oser & marker);
	EXPECT_FALSE(legacy.extractBuffer().empty());
}
