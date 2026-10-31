/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/mapObjects/CGCreature.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/NewHorizonsNeutralArmies.h"
#include "../../lib/networkPacks/PacksForLobby.h"
#include "../../lib/serializer/CMemorySerializer.h"

namespace
{
constexpr auto option = EGameSettings::CREATURES_NEUTRAL_ARMY_SIZE_PERCENT;
class NeutralArmyStartOptionTest : public TinyMapGameTest,
	public ::testing::WithParamInterface<int>
{
protected:
	Services * gameServices() override { return LIBRARY; }
	void configureWorldInitialization(StartInfo & info, GameRandomizer & rng) const override
	{
		if(GetParam() != 0) info.extraOptionsInfo.neutralArmySizePercent = GetParam();
		rng.setSeed(1337);
	}
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(option, JsonNode(75));
		JsonNode heroes(JsonPath::builtin("config/newHorizonsHeroes"));
		heroes["additionalSpecialties"].Struct().erase("ordinaryCreatureSpecialties");
		heroes.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, heroes);
	}
	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0)).heroGarrison({{CreatureID(0), 7}})
			.hero({8, 8, 0}, HeroTypeID(1), PlayerColor(1)).heroGarrison({{CreatureID(0), 7}})
			.monster({12, 6, 0}, CreatureID(0), 43);
		startWithMap(std::move(builder));
	}
};
}
TEST_P(NeutralArmyStartOptionTest, FreshCaptureScalesOnceAndLoadIgnoresAnotherLobbyChoice)
{
	const auto globalBefore = LIBRARY->engineSettings()->getValue(option);
	ASSERT_NO_FATAL_FAILURE(prepare());
	const int percent = GetParam() ? GetParam() : 75;
	const auto request = GetParam() ? std::optional<int32_t>(GetParam()) : std::nullopt;
	ASSERT_EQ(newHorizonsNeutralArmies::percentage(gameState()->getSettings()), percent);
	ASSERT_EQ(gameState()->getStartInfo()->extraOptionsInfo.neutralArmySizePercent, request);
	ASSERT_EQ(gameState()->getInitialStartInfo()->extraOptionsInfo.neutralArmySizePercent, request);
	const int count = (43 * percent + 99) / 100;
	ASSERT_NE(findFirst<CGCreature>(), nullptr);
	EXPECT_EQ(findFirst<CGCreature>()->getStackCount(SlotID(0)), count);
	ASSERT_NE(findHeroByOwner(PlayerColor(0)), nullptr);
	EXPECT_EQ(findHeroByOwner(PlayerColor(0))->getStackCount(SlotID(0)), 7);
	EXPECT_EQ(LIBRARY->engineSettings()->getValue(option), globalBefore);

	CGameState loaded;
	loaded.preInit(LIBRARY);
	ASSERT_NO_THROW(loaded.loadFromMemory(gameState()->saveToMemory()));
	StartInfo incoming = *loaded.getStartInfo();
	incoming.mode = EStartMode::LOAD_GAME;
	incoming.extraOptionsInfo.neutralArmySizePercent = percent == 1 ? 100 : 1;
	incoming.extraOptionsInfo.unlimitedReplay = !incoming.extraOptionsInfo.unlimitedReplay;
	loaded.updateOnLoad(incoming);
	EXPECT_EQ(loaded.getStartInfo()->extraOptionsInfo.neutralArmySizePercent, request);
	EXPECT_EQ(loaded.getStartInfo()->extraOptionsInfo.unlimitedReplay, incoming.extraOptionsInfo.unlimitedReplay);
	EXPECT_EQ(newHorizonsNeutralArmies::percentage(loaded.getSettings()), percent);
	ASSERT_FALSE(loaded.getMap().getObjects<CGCreature>().empty());
	EXPECT_EQ(loaded.getMap().getObjects<CGCreature>().front()->getStackCount(SlotID(0)), count);
	CGameState reread;
	reread.preInit(LIBRARY);
	ASSERT_NO_THROW(reread.loadFromMemory(loaded.saveToMemory()));
	EXPECT_EQ(newHorizonsNeutralArmies::percentage(reread.getSettings()), percent);
	EXPECT_EQ(reread.getMap().getObjects<CGCreature>().front()->getStackCount(SlotID(0)), count);
	EXPECT_EQ(reread.getStartInfo()->extraOptionsInfo.neutralArmySizePercent, request);

	for(const auto epoch : {static_cast<ESerializationVersion>(1144), static_cast<ESerializationVersion>(1145), static_cast<ESerializationVersion>(1146)})
	{
		CMemorySerializer old;
		old.oser.version = epoch;
		if(request)
		{
			EXPECT_THROW(gameState()->serialize(old.oser), std::runtime_error);
			EXPECT_TRUE(old.extractBuffer().empty());
		}
		else
		{
			ASSERT_NO_THROW(gameState()->serialize(old.oser));
			CGameState historic;
			historic.preInit(LIBRARY);
			ASSERT_NO_THROW(historic.loadReplaySnapshot(ReplayPayload{old.extractBuffer(), epoch}));
			EXPECT_FALSE(historic.getStartInfo()->extraOptionsInfo.neutralArmySizePercent);
			EXPECT_EQ(newHorizonsNeutralArmies::percentage(historic.getSettings()), 75);
			EXPECT_EQ(historic.getMap().getObjects<CGCreature>().front()->getStackCount(SlotID(0)), count);
		}
	}
	if(request)
	{
		// Isolate the saved initial-options owner using the existing mutable
		// scenario-options API; no live scaling is invoked by changing metadata.
		gameState()->getStartInfo()->extraOptionsInfo.neutralArmySizePercent.reset();
		LobbyStartGame begin;
		begin.initializedStartInfo = std::make_shared<StartInfo>(*gameState()->getStartInfo());
		begin.initializedGameState = gameState();
		CMemorySerializer current;
		ASSERT_NO_THROW(begin.serialize(current.oser));
		LobbyStartGame decoded;
		ASSERT_NO_THROW(decoded.serialize(current.iser));
		ASSERT_NE(decoded.initializedGameState, nullptr);
		EXPECT_FALSE(decoded.initializedGameState->getStartInfo()->extraOptionsInfo.neutralArmySizePercent);
		EXPECT_EQ(decoded.initializedGameState->getInitialStartInfo()->extraOptionsInfo.neutralArmySizePercent, request);
		for(const auto epoch : {static_cast<ESerializationVersion>(1144), static_cast<ESerializationVersion>(1145), static_cast<ESerializationVersion>(1146)})
		{
			CMemorySerializer worldWire;
			worldWire.oser.version = epoch;
			EXPECT_THROW(gameState()->serialize(worldWire.oser), std::runtime_error);
			EXPECT_TRUE(worldWire.extractBuffer().empty());
			CMemorySerializer lobbyWire;
			lobbyWire.oser.version = epoch;
			EXPECT_THROW(begin.serialize(lobbyWire.oser), std::runtime_error);
			EXPECT_TRUE(lobbyWire.extractBuffer().empty());
		}
		gameState()->getStartInfo()->extraOptionsInfo.neutralArmySizePercent = request;
	}
	EXPECT_EQ(LIBRARY->engineSettings()->getValue(option), globalBefore);

}
INSTANTIATE_TEST_SUITE_P(AbsentAndExplicitBoundaries, NeutralArmyStartOptionTest, ::testing::Values(0, 1, 50, 75, 100));
TEST(NeutralArmyStartOption, AbsentPreservesMapAndGlobalAndNonScenarioModesNeverApply)
{
	CMap map(nullptr);
	map.overrideGameSetting(option, JsonNode(75));
	StartInfo info;
	info.mode = EStartMode::NEW_GAME;
	const auto globalBefore = LIBRARY->engineSettings()->getValue(option);
	CMap defaults(nullptr);
	const auto inherited = newHorizonsNeutralArmies::percentage(defaults.getSettings());
	newHorizonsNeutralArmies::applyStartOption(defaults, info);
	EXPECT_EQ(newHorizonsNeutralArmies::percentage(defaults.getSettings()), inherited);
	EXPECT_EQ(LIBRARY->engineSettings()->getValue(option), globalBefore);
	newHorizonsNeutralArmies::applyStartOption(map, info);
	EXPECT_EQ(newHorizonsNeutralArmies::percentage(map.getSettings()), 75);
	for(const auto mode : {EStartMode::LOAD_GAME, EStartMode::CAMPAIGN})
	{
		info.mode = mode;
		info.extraOptionsInfo.neutralArmySizePercent = 1;
		newHorizonsNeutralArmies::applyStartOption(map, info);
		EXPECT_EQ(newHorizonsNeutralArmies::percentage(map.getSettings()), 75);
	}
	info.mode = EStartMode::NEW_GAME;
	map.battleOnly = true;
	newHorizonsNeutralArmies::applyStartOption(map, info);
	EXPECT_EQ(newHorizonsNeutralArmies::percentage(map.getSettings()), 75);
}
