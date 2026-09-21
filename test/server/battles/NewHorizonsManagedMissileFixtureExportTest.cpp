/*
 * NewHorizonsManagedMissileFixtureExportTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../../mock/TinyMapGameTest.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/pathfinder/PathfinderCache.h"
#include "../../../lib/pathfinder/PathfinderOptions.h"
#include "../../../lib/pathfinder/CGPathNode.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/callback/EditorCallback.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/entities/hero/CHeroHandler.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/filesystem/CZipLoader.h"
#include "../../../lib/constants/StringConstants.h"
#include "../../../lib/mapping/CMapService.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapObjects/CGCreature.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include <boost/filesystem.hpp>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>

namespace
{
void assertNoTestModReferences(const JsonNode & node)
{
	if(node.isString())
		EXPECT_EQ(node.String().find("vcmi-test"), std::string::npos);
	if(node.isStruct())
		for(const auto & [key, value] : node.Struct())
		{
			EXPECT_EQ(key.find("vcmi-test"), std::string::npos);
			assertNoTestModReferences(value);
		}
	if(node.isVector())
		for(const auto & value : node.Vector())
			assertNoTestModReferences(value);
}

class MissileFixtureMapService final : public CMapService
{
	std::vector<uint8_t> bytes;
	MapListener & listener;
public:
	MissileFixtureMapService(std::vector<uint8_t> bytes, MapListener & listener)
		: bytes(std::move(bytes)), listener(listener) {}
	std::unique_ptr<CMap> loadMap(const ResourcePath & name, IGameInfoCallback * cb) const override
	{
		auto result = CMapService::loadMap(bytes.data(), static_cast<int>(bytes.size()), name.getName(), "core", "english", cb);
		listener.mapLoaded(result.get()); // Observe only; no settings/spell injection.
		return result;
	}
	std::unique_ptr<CMapHeader> loadMapHeader(const ResourcePath & name, bool /*isEditor*/ = false) const override
	{
		return CMapService::loadMapHeader(bytes.data(), static_cast<int>(bytes.size()), name.getName(), "core", "english");
	}
};
}

// Separate, UNREGISTERED export lane. Never join mandatory managed19/transport11.
class NewHorizonsManagedMissileFixtureExportTest : public TinyMapGameTest
{
protected:
	std::unique_ptr<MissileFixtureMapService> authoredService;
	Services * gameServices() override { return LIBRARY; }
	void TearDown() override
	{
		TinyMapGameTest::TearDown();
		authoredService.reset();
	}
};

TEST_F(NewHorizonsManagedMissileFixtureExportTest, ExportOrdinaryGuildLearningAndBattleMap)
{
	const auto * enabled = std::getenv("NH_EXPORT_MANAGED_MISSILE_FIXTURE");
	if(!enabled || std::string(enabled) != "1")
		GTEST_SKIP() << "Separate Build-owned export requires explicit NH_EXPORT_MANAGED_MISSILE_FIXTURE=1";
	const auto * profile = std::getenv("NH_REQUIRE_MANAGED_MISSILE_PROFILE");
	ASSERT_NE(profile, nullptr);
	ASSERT_EQ(std::string(profile), "1");
	const auto * playerContent = std::getenv("NH_REQUIRE_PLAYER_CONTENT_PROFILE");
	ASSERT_NE(playerContent, nullptr);
	ASSERT_EQ(std::string(playerContent), "1");
	ASSERT_FALSE(vstd::contains(LIBRARY->modh->getActiveMods(), std::string("vcmi-test")));
	for(const auto & hero : LIBRARY->heroh->objects)
		if(hero)
			ASSERT_NE(hero->getModScope(), "vcmi-test");
	for(const auto & creature : LIBRARY->creh->objects)
		if(creature)
			ASSERT_NE(creature->getModScope(), "vcmi-test");
	const auto * directory = std::getenv("NH_MANAGED_MISSILE_FIXTURE_DIR");
	ASSERT_NE(directory, nullptr);
	const boost::filesystem::path folder(directory);
	ASSERT_TRUE(folder.is_absolute());
	ASSERT_TRUE(boost::filesystem::is_directory(folder));
	const auto output = folder / "NHManagedMissileGuild.vmap";
	ASSERT_FALSE(boost::filesystem::exists(output)) << "Never overwrite an offered map";

	const SpellID missile(SpellID::decode(GameConstants::NEW_HORIZONS_MAGIC_MISSILE));
	ASSERT_NE(missile, SpellID(SpellID::NONE));
	ASSERT_NE(missile, SpellID(SpellID::MAGIC_ARROW));
	ASSERT_EQ(missile.toSpell()->getJsonKey(), GameConstants::NEW_HORIZONS_MAGIC_MISSILE);
	int common = 0;
	for(const auto & spell : LIBRARY->spellh->objects)
		if(spell && spell->isCommonHeroSpell())
			++common;
	ASSERT_EQ(common, 70);
	const auto installed = LIBRARY->settingsHandler->getValue(EGameSettings::MAGIC_NEW_HORIZONS);
	ASSERT_EQ(installed["rulesetVersion"].Integer(), 2);
	ASSERT_EQ(installed["spells"].Struct().size(), 70u);

	const CreatureID pike(CreatureID::decode("core:pikeman"));
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name("NHManagedMissileGuild")
		.description("Private learning/cast fixture, not a balanced scenario. Red starts outside its guild with an empty spellbook, Spell Power24 and100 Pikemen. Visit your built level1 guild to learn Magic Missile through ordinary play. The nearby100 Pikemen never flee or grow. Blue computer has a distant town. Seed verification is not proof of acquiring/casting/saving through the player UI.")
		.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
		.town({8, 10, 0}, FactionID::CASTLE, PlayerColor(0)).townGarrison({})
		.town({30, 30, 0}, FactionID::CASTLE, PlayerColor(1)).townGarrison({})
		.hero({10, 10, 0}, HeroTypeID(0), PlayerColor(0))
		.heroExperience(0).heroPrimary(20, 15, 24, 20).heroSecondarySkills({})
		.heroGarrison({{pike, 100}}).heroSpells({})
		.heroEquipped({{ArtifactPosition::SPELLBOOK, ArtifactID::SPELLBOOK}})
		.monster({14, 10, 0}, pike, 100, 4);
	MapServiceTinyH3M initial(builder.build(), nullptr); // H3M conversion only.
	// Match the editor's authoring lifecycle: callback outlives its map and
	// resolves equipped artifact IDs against that map, not the uninitialized game.
	EditorCallback authoring(nullptr);
	auto authored = initial.loadMap(ResourcePath("NHManagedMissileGuild"), &authoring);
	ASSERT_NE(authored, nullptr);
	authoring.setMap(authored.get());
	ASSERT_FALSE(authored->getMagicOverride().has_value());
	authored->players.at(1).canHumanPlay = false;
	authored->players.at(1).canComputerPlay = true;
	authored->allowedSpells.insert(missile);
	// Serialize an explicit allowed-hero whitelist without test-registry IDs.
	// Preserve the original core eligibility choices, not installed defaults.
	vstd::erase_if(authored->allowedHeroes, [](HeroTypeID hero)
	{
		return hero.toHeroType()->getModScope() == "vcmi-test";
	});
	ASSERT_TRUE(authored->allowedHeroes.count(HeroTypeID(0)));
	ASSERT_TRUE(authored->allowedHeroes.count(HeroTypeID(1)));
	for(auto * town : authored->getObjects<CGTownInstance>())
		if(town->getOwner() == PlayerColor(0))
		{
			town->addBuilding(BuildingID::MAGES_GUILD_1);
			town->obligatorySpells = {missile};
			town->possibleSpells.clear();
		}
	for(auto * creature : authored->getObjects<CGCreature>())
	{
		creature->neverFlees = true;
		creature->notGrowingTeam = true;
	}
	CMapService writer;
	writer.saveMap(authored, output);
	{
		CZipLoader archive("", output);
		const auto entries = archive.getFilteredFiles([](const ResourcePath & resource)
		{
			return resource.getType() == EResType::JSON;
		});
		ASSERT_FALSE(entries.empty());
		for(const auto & resource : entries)
		{
			SCOPED_TRACE(resource.getName());
			const auto data = archive.load(resource)->readAll();
			const JsonNode json(reinterpret_cast<const std::byte *>(data.first.get()), data.second, resource.getName());
			assertNoTestModReferences(json);
		}
		const auto data = archive.load(JsonPath::builtin("header.json"))->readAll();
		const JsonNode writtenHeader(reinterpret_cast<const std::byte *>(data.first.get()), data.second, "header.json");
		ASSERT_EQ(writtenHeader["versionMajor"].Integer(), 3);
		ASSERT_FALSE(writtenHeader.Struct().contains("newHorizonsMagic"));
		ASSERT_FALSE(writtenHeader["allowedHeroes"]["anyOf"].Vector().empty());
	}
	std::ifstream input(output.string(), std::ios::binary);
	ASSERT_TRUE(input.is_open());
	std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
	ASSERT_FALSE(bytes.empty());
	ASSERT_LE(bytes.size(), static_cast<size_t>(std::numeric_limits<int>::max()));
	authoredService = std::make_unique<MissileFixtureMapService>(std::move(bytes), *this);
	StartInfo info;
	info.mapname = "NHManagedMissileGuild";
	info.mode = EStartMode::NEW_GAME;
	info.difficulty = static_cast<ui8>(EMapDifficulty::EASY);
	const auto header = authoredService->loadMapHeader(ResourcePath(info.mapname));
	ASSERT_NE(header, nullptr);
	ASSERT_FALSE(header->players.at(1).canHumanPlay);
	ASSERT_TRUE(header->players.at(1).canComputerPlay);
	for(int index = 0; index < static_cast<int>(header->players.size()); ++index)
	{
		const auto & player = header->players[index];
		if(!player.canHumanPlay && !player.canComputerPlay)
			continue;
		auto & settings = info.playerInfos[PlayerColor(index)];
		settings.color = PlayerColor(index);
		if(index == 0)
			settings.connectedPlayerIDs.insert(static_cast<PlayerConnectionID>(index));
		settings.name = "Player";
		settings.castle = player.defaultCastle();
		settings.hero = player.defaultHero();
		settings.bonus = PlayerStartingBonus::GOLD;
	}
	GameRandomizer randomizer(*gameState());
	Load::ProgressAccumulator progress;
	ASSERT_NO_THROW(gameState()->init(authoredService.get(), &info, randomizer, progress, false));
	ASSERT_EQ(gameState()->getMagicRules(), installed);
	ASSERT_FALSE(map()->getMagicOverride().has_value());
	ASSERT_TRUE(map()->allowedSpells.count(missile));
	for(const auto hero : map()->allowedHeroes)
		ASSERT_NE(hero.toHeroType()->getModScope(), "vcmi-test");
	const auto * human = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(human, nullptr);
	ASSERT_EQ(human->anchorPos(), int3(10, 10, 0));
	ASSERT_EQ(human->stacksCount(), 1);
	ASSERT_EQ(human->getStackCount(SlotID(0)), 100);
	ASSERT_EQ(human->getCreature(SlotID(0))->getId(), pike);
	RecordProperty("hero_anchor", human->anchorPos().toString());
	RecordProperty("hero_visitable", human->visitablePos().toString());
	RecordProperty("initial_movement", human->movementPointsRemaining());
	ASSERT_TRUE(human->hasSpellbook());
	ASSERT_TRUE(human->getSpellsInSpellbook().empty()) << "No starting-spell autoaddition or seeded acquisition";
	ASSERT_EQ(human->getEffectPower(missile.toSpell()), 24);
	ASSERT_EQ(human->getEffectPowerDivisor(missile.toSpell()), 10);
	ASSERT_TRUE(human->getSourcesForSpell(missile).empty());
	ASSERT_TRUE(gameState()->isAllowed(missile));
	PathfinderCache paths(gameState().get(), PathfinderOptions(*gameState()));
	const auto route = paths.getPathsInfo(human);
	bool guildFound = false;
	for(const auto townId : map()->getAllTowns())
	{
		const auto * town = gameState()->getTown(townId);
		if(town->getOwner() != PlayerColor(0))
			continue;
		guildFound = true;
		ASSERT_TRUE(town->hasBuilt(BuildingID::MAGES_GUILD_1));
		ASSERT_TRUE(vstd::contains(town->spells.at(0), missile));
		ASSERT_EQ(town->anchorPos(), int3(8, 10, 0));
		RecordProperty("guild_anchor", town->anchorPos().toString());
		RecordProperty("guild_visitable", town->visitablePos().toString());
		CGPath path;
		ASSERT_TRUE(route->getPath(path, town->visitablePos()));
		const auto * node = route->getPathInfo(town->visitablePos());
		ASSERT_NE(node, nullptr);
		ASSERT_EQ(node->turns, 0);
		ASSERT_EQ(node->action, EPathNodeAction::VISIT);
		RecordProperty("initial_to_guild_moves_remaining", node->moveRemains);
	}
	ASSERT_TRUE(guildFound);
	const auto * neutral = dynamic_cast<const CGCreature *>(findObjectAt({14, 10, 0}));
	ASSERT_NE(neutral, nullptr);
	ASSERT_EQ(neutral->anchorPos(), int3(14, 10, 0));
	ASSERT_EQ(neutral->CCreatureSet::stacksCount(), 1);
	ASSERT_EQ(neutral->getStackCount(SlotID(0)), 100);
	ASSERT_EQ(neutral->CCreatureSet::getCreature(SlotID(0))->getId(), pike);
	ASSERT_TRUE(neutral->neverFlees);
	ASSERT_TRUE(neutral->notGrowingTeam);
	RecordProperty("neutral_anchor", neutral->anchorPos().toString());
	RecordProperty("neutral_visitable", neutral->visitablePos().toString());
	CGPath battlePath;
	ASSERT_TRUE(route->getPath(battlePath, neutral->visitablePos()));
	const auto * battleNode = route->getPathInfo(neutral->visitablePos());
	ASSERT_NE(battleNode, nullptr);
	ASSERT_EQ(battleNode->turns, 0);
	ASSERT_EQ(battleNode->action, EPathNodeAction::BATTLE);
	RecordProperty("initial_to_neutral_moves_remaining", battleNode->moveRemains);
	// Both paths start at the initial hero: not sequential post-guild movement.
	EXPECT_TRUE(gameState()->getPlayerState(PlayerColor(0))->isHuman());
	EXPECT_FALSE(gameState()->getPlayerState(PlayerColor(1))->isHuman());
	EXPECT_EQ(LIBRARY->settingsHandler->getValue(EGameSettings::MAGIC_NEW_HORIZONS), installed);
	RecordProperty("exported_map", output.string());
	// No hero visit, learning, cast, battle or save is claimed by this seed gate.
	// Proposed ordinary continuation is POST-BATTLE save/fresh restart, not an
	// active-battle full-save acceptance claim. SP24/divisor10 = 2.4 legacy units.
}
