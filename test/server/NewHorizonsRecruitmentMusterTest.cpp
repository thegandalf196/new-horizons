/*
 * NewHorizonsRecruitmentMusterTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"

#include <gtest/gtest.h>

#include "../../lib/GameConstants.h"
#include "../../lib/entities/creature/NewHorizonsCreatureCategoryRules.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../server/CGameHandler.h"
#include "../mock/GameHandlerTestServer.h"
#include "../mock/TinyH3MBuilder.h"
#include "../mock/TinyMapGameTest.h"

namespace
{
class NewHorizonsRecruitmentMusterTest : public TinyMapGameTest
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
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		loaded->overrideGameSetting(EGameSettings::CREATURES_NEW_HORIZONS_CATEGORIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCreatureCategories")));
	}

	void startGame()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0))
			.town({12, 12, 0}, FactionID(FactionID::decode("core:castle")), PlayerColor(0))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:christian")), PlayerColor(0));
		startWithMap(std::move(builder));

		town = findFirst<CGTownInstance>();
		hero = findHeroByOwner(PlayerColor(0));
		ASSERT_NE(town, nullptr);
		ASSERT_NE(hero, nullptr);

		// Keep one representative row from each saved world category.  A zero
		// stock is intentional: Muster replenishes availability, not an army.
		town->creatures = {
			{0, {CreatureID(CreatureID::decode("core:pikeman"))}},
			{0, {CreatureID(CreatureID::decode("core:griffin"))}},
			{0, {CreatureID(CreatureID::decode("core:angel"))}}
		};
		town->setVisitingHero(hero);
		recruitment = SecondarySkill(SecondarySkill::decode("new-horizons:recruitment"));
		hero->setSecSkillLevel(recruitment, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	}

	void resetMusterMarker()
	{
		SetNewHorizonsMusterState reset;
		reset.heroId = hero->id;
		reset.targetId = town->id;
		reset.lastUseWeek = -1;
		gameState()->apply(reset);
	}

	static CreatureID creature(const char * id)
	{
		return CreatureID(CreatureID::decode(id));
	}

	CGTownInstance * town = nullptr;
	CGHeroInstance * hero = nullptr;
	SecondarySkill recruitment;
};
}

TEST_F(NewHorizonsRecruitmentMusterTest, RankAmountsAndWeeklyGuardsAreAuthoritativeAndAtomic)
{
	startGame();
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler gameHandler(server, gameState());

	const auto muster = [&](CreatureID target)
	{
		return gameHandler.musterCreatures(hero->id, town->id, target, PlayerColor(0));
	};
	const auto stock = [&](size_t row)
	{
		return town->creatures.at(row).first;
	};

	// Basic can only Muster Core, and adds exactly two.
	ASSERT_TRUE(muster(creature("core:pikeman")));
	EXPECT_EQ(stock(0), 2u);
	EXPECT_EQ(hero->getNewHorizonsMusterLastWeek(), 0);
	EXPECT_EQ(town->getNewHorizonsMusterLastWeek(), 0);

	const auto afterBasic = town->creatures;
	EXPECT_FALSE(muster(creature("core:pikeman")));
	EXPECT_EQ(town->creatures, afterBasic);

	// An invalid category/rank request is rejected before either marker or stock
	// changes.  This also guards against a client supplying an arbitrary amount.
	resetMusterMarker();
	const auto beforeElite = town->creatures;
	EXPECT_FALSE(muster(creature("core:griffin")));
	EXPECT_EQ(town->creatures, beforeElite);
	EXPECT_EQ(hero->getNewHorizonsMusterLastWeek(), -1);
	EXPECT_EQ(town->getNewHorizonsMusterLastWeek(), -1);

	// Advanced: four Core and one Elite.
	hero->setSecSkillLevel(recruitment, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	EXPECT_TRUE(muster(creature("core:pikeman")));
	EXPECT_EQ(stock(0), 6u);
	resetMusterMarker();
	EXPECT_TRUE(muster(creature("core:griffin")));
	EXPECT_EQ(stock(1), 1u);

	// Expert: six Core, two Elite, and one Champion.
	hero->setSecSkillLevel(recruitment, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	resetMusterMarker();
	EXPECT_TRUE(muster(creature("core:pikeman")));
	EXPECT_EQ(stock(0), 12u);
	resetMusterMarker();
	EXPECT_TRUE(muster(creature("core:griffin")));
	EXPECT_EQ(stock(1), 3u);
	resetMusterMarker();
	EXPECT_TRUE(muster(creature("core:angel")));
	EXPECT_EQ(stock(2), 1u);

	// The hero and town markers are independent of the stock and survive a
	// weekly boundary.  Day eight is the first day of absolute week one.
	gameState()->day = 8;
	EXPECT_TRUE(muster(creature("core:pikeman")));
	EXPECT_EQ(hero->getNewHorizonsMusterLastWeek(), 1);
	EXPECT_EQ(town->getNewHorizonsMusterLastWeek(), 1);

	const auto beforeWrongOwner = town->creatures;
	EXPECT_FALSE(gameHandler.musterCreatures(hero->id, town->id, creature("core:pikeman"), PlayerColor(1)));
	EXPECT_EQ(town->creatures, beforeWrongOwner);
	EXPECT_EQ(hero->getNewHorizonsMusterLastWeek(), 1);
	EXPECT_EQ(town->getNewHorizonsMusterLastWeek(), 1);

	// Save/load must preserve both usage markers, not just the replenished stock.
	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	const auto * restoredHero = restored.getHero(hero->id);
	const auto * restoredTown = restored.getTown(town->id);
	ASSERT_NE(restoredHero, nullptr);
	ASSERT_NE(restoredTown, nullptr);
	EXPECT_EQ(restoredHero->getNewHorizonsMusterLastWeek(), 1);
	EXPECT_EQ(restoredTown->getNewHorizonsMusterLastWeek(), 1);
}

TEST(NewHorizonsRecruitmentMusterWire, StateRoundTripsAndOlderSavesRejectAuthoredMarkers)
{
	SetNewHorizonsMusterState outgoing;
	outgoing.heroId = ObjectInstanceID(42);
	outgoing.targetId = ObjectInstanceID(77);
	outgoing.lastUseWeek = 9;

	CMemorySerializer wire;
	wire.oser.version = ESerializationVersion::CURRENT;
	wire.iser.version = ESerializationVersion::CURRENT;
	wire.oser & outgoing;

	SetNewHorizonsMusterState incoming;
	wire.iser & incoming;
	EXPECT_EQ(incoming.heroId, outgoing.heroId);
	EXPECT_EQ(incoming.targetId, outgoing.targetId);
	EXPECT_EQ(incoming.lastUseWeek, outgoing.lastUseWeek);

	// The packet itself is not silently discarded by an older serializer.  The
	// object-level save gates below are what protect the actual hero/dwelling
	// marker when writing a legacy save.
	EXPECT_EQ(ESerializationVersion::CURRENT, ESerializationVersion::NEW_HORIZONS_MUSTER);
}
