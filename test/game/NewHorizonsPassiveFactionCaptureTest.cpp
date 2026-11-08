/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../lib/networkPacks/PacksForLobby.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/GameSettings.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

class NewHorizonsPassiveFactionCaptureTest : public TinyMapGameTest
{
protected:
	bool corrupt = false;
	JsonNode authored;
	Services * gameServices() override { return LIBRARY; }
	void mapLoaded(CMap * map) override
	{
		TinyMapGameTest::mapLoaded(map);
		authored = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
		// Capture the marks-based version-two policy without version-three points.
		authored["metamagicPolicy"] = JsonNode(JsonMap{});
		for(const auto * key : {"metamagicPolicy", "divineMandatePolicy", "demonicGatingPolicy", "bloodragePolicy"})
			authored[key]["version"].Integer() = 2;
		if(corrupt)
			authored["divineMandatePolicy"]["version"].String() = "2";
		authored.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, authored);
	}
	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("Passive faction capture")
			.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0)).heroGarrison({{CreatureID(0), 1}})
			.hero({8, 8, 0}, HeroTypeID(1), PlayerColor(1)).heroGarrison({{CreatureID(0), 1}});
		startWithMap(std::move(builder));
	}
};

TEST_F(NewHorizonsPassiveFactionCaptureTest, FreshWorldAndHeroesCaptureAllThreePolicies)
{
	ASSERT_NO_THROW(prepare());
	EXPECT_EQ(gameState()->getHeroPerkRules(), authored);
	for(const auto owner : {PlayerColor(0), PlayerColor(1)})
	{
		const auto * hero = findHeroByOwner(owner);
		ASSERT_NE(hero, nullptr);
		EXPECT_EQ(hero->getPerkState().rules, authored);
		EXPECT_NO_THROW(hero->getPerkState().validate());
	}
}

TEST_F(NewHorizonsPassiveFactionCaptureTest, MalformedAuthoredPolicyCannotStartWorld)
{
	corrupt = true;
	EXPECT_THROW(prepare(), std::runtime_error);
}

TEST_F(NewHorizonsPassiveFactionCaptureTest, FullParentWritersRejectBeforeTheirFirstField)
{
	ASSERT_NO_THROW(prepare());
	auto rejects = [](auto & value)
	{
		for(const auto epoch : {ESerializationVersion::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES,
			ESerializationVersion::NEW_HORIZONS_OPTIONAL_QUICK_STUDY})
		{
			CMemorySerializer bytes; bytes.oser.version = epoch;
			EXPECT_THROW(value.serialize(bytes.oser), std::runtime_error);
			EXPECT_TRUE(bytes.extractBuffer().empty());
		}
	};
	rejects(*gameState());
	rejects(*map());
	const auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	rejects(*const_cast<CGHeroInstance *>(hero));
	LobbyStartGame lobby; lobby.initializedGameState = gameState();
	rejects(lobby);
}
