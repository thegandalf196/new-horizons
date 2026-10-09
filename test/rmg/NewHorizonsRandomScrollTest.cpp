/*
 * NewHorizonsRandomScrollTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"

#include "../../lib/GameLibrary.h"
#include "../../lib/GameConstants.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/entities/artifact/CArtifactInstance.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/rmg/CMapGenerator.h"
#include "../../lib/rmg/CMapGenOptions.h"
#include "../../lib/rmg/RmgMap.h"
#include "../../lib/rmg/TileInfo.h"
#include "../../lib/rmg/modificators/TreasurePlacer.h"
#include "../../lib/serializer/JsonDeserializer.h"
#include "../../lib/spells/NewHorizonsSpellAvailability.h"

#include <vstd/RNG.h>

namespace
{
class InspectableTreasurePlacer : public TreasurePlacer
{
public:
	using TreasurePlacer::TreasurePlacer;
	std::vector<ObjectInfo> & scrolls() { return objects.getPossibleObjects(); }
};

// Exercise the actual random-loot registration and generator without generating
// an entire map. The map owns its authored rules; no initialized game is needed.
class NewHorizonsRandomScrollTest : public testing::Test
{
protected:
	CRmgTemplate mapTemplate;
	CMapGenOptions options;
	std::unique_ptr<CMapGenerator> generator;
	std::unique_ptr<RmgMap> map;
	std::unique_ptr<Zone> zone;
	std::unique_ptr<InspectableTreasurePlacer> placer;

	void SetUp() override
	{
		const JsonNode templates(JsonPath::builtin("test/rmg/1.json"));
		JsonDeserializer reader(nullptr, templates["2SM2a"]);
		mapTemplate.setId("random-scroll-admission-test");
		mapTemplate.serializeJson(reader);
		options.setMapTemplate(&mapTemplate);
		generator = std::make_unique<CMapGenerator>(options, nullptr, 1701);
		map = std::make_unique<RmgMap>(options, nullptr);
		CRandomGenerator random(1701);
		zone = std::make_unique<Zone>(*map, *generator, random);
		zone->setType(ETemplateZoneType::TREASURE);
		placer = std::make_unique<InspectableTreasurePlacer>(*zone, *map, *generator);
	}

	void permitOnly(SpellID spell)
	{
		map->mapInstance->allowedSpells = {spell};
	}
};

TEST_F(NewHorizonsRandomScrollTest, GeneratedMapRulesExcludeBlockedRandomLootButPreserveAuthoredScroll)
{
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires the separate New Horizons native profile";
	const SpellID blocked(SpellID::STONE_SKIN);
	const auto rules = map->mapInstance->getSettings().getValue(EGameSettings::MAGIC_NEW_HORIZONS);
	ASSERT_TRUE(rules["spells"]["core:stoneSkin"]["heroAccess"].isBool());
	ASSERT_FALSE(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(rules, blocked));
	permitOnly(blocked);
	placer->addScrolls();
	EXPECT_TRUE(placer->scrolls().empty());
	// This fix must not remove compatibility definitions or authored artifacts.
	const auto * authored = map->mapInstance->createScroll(blocked);
	ASSERT_NE(authored, nullptr);
	EXPECT_EQ(authored->getScrollSpellID(), blocked);
}

TEST_F(NewHorizonsRandomScrollTest, EligibleSpellGeneratesAndMapBanLeavesNoGenerator)
{
	const SpellID eligible(SpellID::MAGIC_ARROW);
	ASSERT_TRUE(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(
		map->mapInstance->getSettings().getValue(EGameSettings::MAGIC_NEW_HORIZONS), eligible));
	permitOnly(eligible);
	placer->addScrolls();
	ASSERT_EQ(placer->scrolls().size(), 1);
	ASSERT_NE(placer->scrolls().front().generateObject(), nullptr);
	const auto * generated = map->mapInstance->getArtifactInstance(ArtifactInstanceID(0));
	ASSERT_NE(generated, nullptr);
	EXPECT_EQ(generated->getScrollSpellID(), eligible);

	placer->scrolls().clear();
	map->mapInstance->allowedSpells.clear();
	placer->addScrolls();
	EXPECT_TRUE(placer->scrolls().empty());
}

TEST_F(NewHorizonsRandomScrollTest, ExplicitLegacyTemplateContextRetainsOriginalRandomAdmission)
{
	const SpellID legacy(SpellID::STONE_SKIN);
	// An explicitly absent versioned context is a legacy template, not an
	// instruction to merge the installed New Horizons roster back into it.
	map->mapInstance->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode());
	ASSERT_TRUE(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(
		map->mapInstance->getSettings().getValue(EGameSettings::MAGIC_NEW_HORIZONS), legacy));
	permitOnly(legacy);
	placer->addScrolls();
	ASSERT_EQ(placer->scrolls().size(), 1);
	ASSERT_NE(placer->scrolls().front().generateObject(), nullptr);
	const auto * generated = map->mapInstance->getArtifactInstance(ArtifactInstanceID(0));
	ASSERT_NE(generated, nullptr);
	EXPECT_EQ(generated->getScrollSpellID(), legacy);
}
}
