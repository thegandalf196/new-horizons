/*
 * NewHorizonsCreatureCategoryLoadEntrypointTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/entities/creature/NewHorizonsCreatureCategoryRules.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/campaign/CampaignState.h"
#include "../../../lib/gameState/TavernHeroesPool.h"
#include "../../../lib/gameState/CGameStateCampaign.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/entities/building/TownFortifications.h"
#include "../../../lib/rmg/CMapGenOptions.h"
#include "../../../lib/mapObjects/MiscObjects.h"
#include "../../../lib/mapObjects/ObjectTemplate.h"
#include "../../../lib/mapObjects/TownBuildingInstance.h"
#include "../../../lib/mapping/CCastleEvent.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/Updaters.h"
#include <stdexcept>

namespace
{
using newHorizonsCreatures::CreatureCategoryRules;
constexpr auto missingCategoryCreature = "core:missingEntrypointCategoryCreature";

// Produces native binary input, not an alternate loader. Every field except one
// typed category payload passes through the real BinarySerializer unchanged.
class EntrypointCategoryWriter
{
	BinarySerializer & writer;
	JsonNode category;
public:
	using Version = ESerializationVersion;
	static constexpr bool saving = true;
	static constexpr bool loadingGamestate = false;
	int replacements = 0;
	EntrypointCategoryWriter(BinarySerializer & writer, JsonNode category)
		: writer(writer), category(std::move(category)) {}
	bool hasFeature(Version version) const { return writer.hasFeature(version); }
	template<typename T> EntrypointCategoryWriter & operator&(T & value)
	{
		writer & value;
		return *this;
	}
	EntrypointCategoryWriter & operator&(CreatureCategoryRules &)
	{
		++replacements;
		writer & category;
		return *this;
	}
};
}

class NewHorizonsCreatureCategoryLoadEntrypointTest : public HeroCommandFixture
{
protected:
	void mapLoaded(CMap * map) override
	{
		useCommands = false;
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::CREATURES_NEW_HORIZONS_CATEGORIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCreatureCategories")));
	}
};

TEST_F(NewHorizonsCreatureCategoryLoadEntrypointTest, ValidOverlayMatchesRealSnapshotBytesAndLoadsThroughProductionEntrypoint)
{
	startGame();
	// saveToMemory excludes the replay log. Prime a separate state from that
	// canonical snapshot so the overlay never accidentally writes a live log.
	CGameState source;
	source.preInit(LIBRARY);
	source.loadFromMemory(gameState()->saveToMemory());
	const auto category = source.getCreatureCategoryRules().getRules();
	CMemorySerializer encoded;
	EntrypointCategoryWriter writer(encoded.oser, category);
	source.serialize(writer);
	ASSERT_EQ(writer.replacements, 1);
	const auto bytes = encoded.extractBuffer();
	ASSERT_EQ(bytes, source.saveToMemory()) << "The replacement writer must preserve the real entrypoint format";
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(bytes);
	const CreatureID pixie(CreatureID::decode("core:pixie"));
	ASSERT_TRUE(restored.getCreatureCategory(pixie));
	EXPECT_EQ(restored.getCreatureCategory(pixie), source.getCreatureCategory(pixie));
	EXPECT_EQ(restored.getCreatureCategoryRules().getRules(), category);
}

TEST_F(NewHorizonsCreatureCategoryLoadEntrypointTest, MalformedCurrentPayloadIsRejectedInsideActualLoadFromMemory)
{
	startGame();
	CGameState source;
	source.preInit(LIBRARY);
	source.loadFromMemory(gameState()->saveToMemory());
	auto changed = source.getCreatureCategoryRules().getRules();
	changed["creatures"]["core:pixie"].String() = "champion";
	changed["creatures"][missingCategoryCreature].String() = "elite";
	ASSERT_NO_THROW(CreatureCategoryRules{changed}) << "The failure must be canonical entity validation, not JSON shape";
	CMemorySerializer encoded;
	EntrypointCategoryWriter writer(encoded.oser, changed);
	source.serialize(writer);
	ASSERT_EQ(writer.replacements, 1);
	CGameState target;
	target.preInit(LIBRARY);
	target.loadFromMemory(source.saveToMemory());
	const CreatureID pixie(CreatureID::decode("core:pixie"));
	const auto before = target.getCreatureCategory(pixie);
	ASSERT_TRUE(before);
	ASSERT_EQ(before->category, newHorizonsCreatures::CreatureCategory::CORE);
	bool rejected = false;
	try
	{
		// Unlike the older direct-header wire test, this enters the production
		// CGameState.cpp method, which sets loadingGamestate/callback itself.
		target.loadFromMemory(encoded.extractBuffer());
	}
	catch(const std::runtime_error & error)
	{
		rejected = true;
		EXPECT_EQ(std::string(error.what()),
			std::string("Unknown or noncanonical creature category identifier: ") + missingCategoryCreature);
	}
	EXPECT_TRUE(rejected);
	EXPECT_EQ(target.getCreatureCategory(pixie), before);
	EXPECT_EQ(target.getCreatureCategoryRules().getRules(), source.getCreatureCategoryRules().getRules());
	// This asserts category publication only. loadFromMemory intentionally clears
	// battles and can replace earlier fields before failure: no whole-world rollback.
}
