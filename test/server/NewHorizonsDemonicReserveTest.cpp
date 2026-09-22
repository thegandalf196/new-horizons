/*
 * NewHorizonsDemonicReserveTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"

#include <gtest/gtest.h>

#include "../../lib/GameConstants.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../server/CGameHandler.h"
#include "../mock/GameHandlerTestServer.h"
#include "../mock/TinyH3MBuilder.h"
#include "../mock/TinyMapGameTest.h"

namespace
{
class NewHorizonsDemonicReserveTest : public TinyMapGameTest
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
	}

	static CreatureID creature(const char * id)
	{
		return CreatureID(CreatureID::decode(id));
	}
};
}

TEST_F(NewHorizonsDemonicReserveTest, TransfersAreOwnedValidatedAndCountPreserving)
{
	const auto imp = creature("core:imp");
	const auto gog = creature("core:gog");
	const auto pikeman = creature("core:pikeman");
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:fiona")), PlayerColor(0))
		.heroGarrison({{imp, 10}, {gog, 1}});
	startWithMap(std::move(builder));

	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:demonicGating")),
		MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler gameHandler(server, gameState());

	ASSERT_TRUE(gameHandler.arrangeDemonicReserve(hero->id, SlotID(0), imp, 4, true, PlayerColor(0)));
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 6);
	EXPECT_EQ(hero->getDemonicReserveCount(imp), 4);

	EXPECT_FALSE(gameHandler.arrangeDemonicReserve(hero->id, SlotID(0), pikeman, 1, true, PlayerColor(0)));
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 6);
	EXPECT_EQ(hero->getDemonicReserveCount(imp), 4);

	ASSERT_TRUE(gameHandler.arrangeDemonicReserve(hero->id, SlotID(), imp, 3, false, PlayerColor(0)));
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 9);
	EXPECT_EQ(hero->getDemonicReserveCount(imp), 1);
	EXPECT_EQ(hero->getStackCount(SlotID(0)) + hero->getDemonicReserveCount(imp), 10);
}

TEST_F(NewHorizonsDemonicReserveTest, CannotMoveTheOnlyActiveStackIntoReserve)
{
	const auto imp = creature("core:imp");
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:fiona")), PlayerColor(0))
		.heroGarrison({{imp, 5}});
	startWithMap(std::move(builder));

	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:demonicGating")),
		MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler gameHandler(server, gameState());

	EXPECT_FALSE(gameHandler.arrangeDemonicReserve(hero->id, SlotID(0), imp, 5, true, PlayerColor(0)));
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 5);
	EXPECT_TRUE(hero->getDemonicReserve().empty());
}

TEST(NewHorizonsDemonicReserveWire, SnapshotRoundTripsAndRejectsOlderWire)
{
	SetNewHorizonsDemonicReserve outgoing;
	outgoing.heroId = ObjectInstanceID(12);
	outgoing.reserve[CreatureID(42)] = 17;

	CMemorySerializer serializer;
	serializer.oser.version = ESerializationVersion::CURRENT;
	serializer.iser.version = ESerializationVersion::CURRENT;
	serializer.oser & outgoing;
	SetNewHorizonsDemonicReserve incoming;
	serializer.iser & incoming;
	EXPECT_EQ(incoming.heroId, outgoing.heroId);
	EXPECT_EQ(incoming.reserve, outgoing.reserve);

	CMemorySerializer legacy;
	legacy.oser.version = ESerializationVersion::NEW_HORIZONS_MUSTER_PERKS;
	EXPECT_THROW(legacy.oser & outgoing, std::runtime_error);
}
