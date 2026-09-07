/*
 * NewHorizonsCreatureCategoryFixtureExportTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/VCMIDirs.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGCreature.h"
#include "../../../lib/mapObjects/army/CStackInstance.h"
#include "../../../lib/mapping/TerrainTile.h"
#include "../../../lib/pathfinder/PathfinderCache.h"
#include "../../../lib/pathfinder/PathfinderOptions.h"
#include "../../../lib/pathfinder/CGPathNode.h"
#include "../../../lib/texts/CGeneralTextHandler.h"
#include <cstdlib>
#include <zlib.h>

class NewHorizonsCreatureCategoryFixtureExportTest : public BattleTestFixture
{
protected:
	void configurePlayer(PlayerSettings & settings) const override
	{
		BattleTestFixture::configurePlayer(settings);
		if(settings.color == PlayerColor(1))
			settings.connectedPlayerIDs.clear();
	}
};

TEST_F(NewHorizonsCreatureCategoryFixtureExportTest, ExportOrdinaryFourSlotArmyWithReachableNeutral)
{
	const auto * enabled = std::getenv("NH_EXPORT_CATEGORY_FIXTURE");
	if(!enabled || std::string(enabled) != "1")
		GTEST_SKIP() << "Build-owned opt-in export requires NH_EXPORT_CATEGORY_FIXTURE=1 and actual category activation";
	const std::string name = "NHCreatureCategories";
	const auto pixie = creatureByName("core:pixie");
	const auto elemental = creatureByName("core:airElemental");
	const auto phoenix = creatureByName("core:phoenix");
	const auto pikeman = creatureByName("core:pikeman");
	const std::array<CreatureID, 4> types = {pixie, elemental, phoenix, pikeman};
	const std::array<int, 4> counts = {20, 20, 2, 100};
	for(const auto type : types)
		ASSERT_NE(type, CreatureID::NONE);
	using Builder = TinyH3M::TinyH3MBuilder;
	Builder builder(EMapFormat::SOD);
	builder.size(36, false).name(name)
		.description("Ordinary creature-category diagnostic, not a balanced scenario. Red human Orrin starts "
			"with20Pixies,20AirElementals,2Phoenixes and100Pikemen. Under the explicitly activated partial "
			"Conflux classification they show Core, Elite, Champion and no category respectively. "
			"The map supplies no category override, custom bonuses, Commander or XP quest. "
			"Eight savage Pikemen at19,10 cannot join but may flee: choose ordinary pursuit if combat "
			"is authorized in a separate lease. Labels do not prove combat effects. No contradictory "
			"world/battle contexts or scripted AI are manufactured. This same ordinary map may later "
			"create a NEW category-absent normal save for a mapped-Pixie legacy control; do not overwrite "
			"existing controls or infer legacy absence from the unmapped Pikeman.")
		.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
		.town({8, 10, 0}, FactionID::CASTLE, PlayerColor(0)).townGarrison({})
		.town({30, 30, 0}, FactionID::CASTLE, PlayerColor(1)).townGarrison({})
		.hero({17, 10, 0}, HeroTypeID(0), PlayerColor(0)).heroExperience(0)
		.heroGarrison({{pixie, 20}, {elemental, 20}, {phoenix, 2}, {pikeman, 100}})
		.monster({19, 10, 0}, pikeman, 8, static_cast<int8_t>(CGCreature::Character::SAVAGE));
	const auto expected = builder.build();
	MapServiceTinyH3M parser(expected, nullptr);
	const auto header = parser.loadMapHeader(ResourcePath(name));
	ASSERT_NE(header, nullptr);
	ASSERT_EQ(header->version, EMapFormat::SOD);
	// Real H3M initialization; intentionally no mapLoaded/settings/stat override.
	startWithMap(builder);
	const auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	EXPECT_EQ(hero->level, 1u);
	EXPECT_EQ(hero->exp, 0);
	EXPECT_FALSE(hero->getCommander());
	EXPECT_TRUE(gameState()->getPlayerState(PlayerColor(0))->isHuman());
	EXPECT_FALSE(gameState()->getPlayerState(PlayerColor(1))->isHuman());
	EXPECT_EQ(findHeroByOwner(PlayerColor(1)), nullptr);
	ASSERT_EQ(hero->stacksCount(), 4);
	for(size_t slot = 0; slot < types.size(); ++slot)
	{
		const auto * stack = hero->getStackPtr(SlotID(static_cast<int>(slot)));
		ASSERT_NE(stack, nullptr);
		EXPECT_EQ(stack->getCreatureID(), types[slot]);
		EXPECT_EQ(stack->getCount(), counts[slot]);
	}
	const JsonNode canonical(JsonPath::builtin("config/newHorizonsCreatureCategories"));
	ASSERT_EQ(gameState()->getCreatureCategoryRules().getRules(), canonical)
		<< "Requires real private activation, not fixture-injected categories";
	// This opt-in native exporter is run in the private English context. Compare
	// actual resolved text, not merely absence of an untranslated identifier.
	ASSERT_EQ(CGeneralTextHandler::getPreferredLanguage(), "english");
	const JsonNode englishTexts(JsonPath::builtin("config/newHorizonsCreatureCategoryTexts"));
	const std::array<newHorizonsCreatures::CreatureCategory, 3> categories = {
		newHorizonsCreatures::CreatureCategory::CORE, newHorizonsCreatures::CreatureCategory::ELITE,
		newHorizonsCreatures::CreatureCategory::CHAMPION};
	for(size_t slot = 0; slot < categories.size(); ++slot)
	{
		const auto view = gameState()->getCreatureCategory(types[slot]);
		ASSERT_TRUE(view);
		EXPECT_EQ(view->category, categories[slot]);
		for(const auto & text : {view->nameTextId, view->descriptionTextId})
		{
			const auto translated = LIBRARY->generaltexth->translate(text);
			EXPECT_FALSE(translated.empty());
			EXPECT_NE(translated, text) << "Actual activated translation, not just a JSON text definition";
			ASSERT_TRUE(englishTexts[text].isString());
			EXPECT_EQ(translated, englishTexts[text].String()) << "Requires the actual private English context";
		}
	}
	EXPECT_FALSE(gameState()->getCreatureCategory(pikeman));
	const auto * neutral = dynamic_cast<const CGCreature *>(findObjectAt({19, 10, 0}));
	ASSERT_NE(neutral, nullptr);
	EXPECT_EQ(neutral->initialCharacter, CGCreature::Character::SAVAGE);
	EXPECT_EQ(neutral->getStackCount(SlotID(0)), 8);
	const auto destination = neutral->visitablePos();
	ASSERT_TRUE(gameState()->getTile(destination)->isLand());
	PathfinderCache paths(gameState().get(), PathfinderOptions(*gameState()));
	const auto route = paths.getPathsInfo(hero);
	CGPath path;
	ASSERT_TRUE(route->getPath(path, destination));
	const auto * node = route->getPathInfo(destination);
	ASSERT_NE(node, nullptr);
	EXPECT_EQ(node->turns, 0) << "Neutral must be reachable in the current turn";
	EXPECT_EQ(node->action, EPathNodeAction::BATTLE);
	ASSERT_EQ(builder.buildAndDump(name), expected);
	const auto output = VCMIDirs::get().userCachePath() / "testMaps" / (name + ".h3m");
	std::unique_ptr<gzFile_s, decltype(&gzclose)> input(gzopen(output.string().c_str(), "rb"), &gzclose);
	ASSERT_NE(input, nullptr);
	std::vector<uint8_t> actual;
	std::array<uint8_t, 4096> chunk;
	int count = 0;
	while((count = gzread(input.get(), chunk.data(), chunk.size())) > 0)
		actual.insert(actual.end(), chunk.begin(), chunk.begin() + count);
	ASSERT_EQ(count, 0);
	ASSERT_TRUE(gzeof(input.get()));
	ASSERT_EQ(gzclose(input.release()), Z_OK);
	ASSERT_EQ(actual, expected);
	MapServiceTinyH3M exported(actual, nullptr);
	ASSERT_NE(exported.loadMap(ResourcePath(name), gameState().get()), nullptr);
}
