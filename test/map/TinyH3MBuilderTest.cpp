/*
 * TinyH3MBuilderTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"

#include "../mock/TinyH3MBuilder.h"
#include "../mock/mock_MapServiceTinyH3M.h"
#include "../mock/mock_Services.h"

#include "../../lib/StartInfo.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/callback/EditorCallback.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/entities/artifact/CArtifactInstance.h"
#include "../../lib/filesystem/CMemoryBuffer.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGResource.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/ObjectTemplate.h"
#include "../../lib/mapObjects/TownBuildingInstance.h"
#include "../../lib/mapObjects/army/CStackInstance.h"
#include "../../lib/mapObjects/Quest.h"
#include "../../lib/mapObjects/MiscObjects.h"
#include "../../lib/mapping/CCastleEvent.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/mapping/CMapHeader.h"
#include "../../lib/mapping/MapFormat.h"
#include "../../lib/mapping/MapFormatH3M.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/serializer/JsonDeserializer.h"
#include "../../lib/serializer/JsonSerializer.h"

namespace
{
auto loadHeader(std::vector<uint8_t> bytes)
{
	CMemoryBuffer buf;
	buf.write(bytes.data(), static_cast<si64>(bytes.size()));
	buf.seek(0);

	// modName "core" so readLocalizedString -> mapRegisterLocalizedString can resolve mod language
	CMapLoaderH3M loader("TinyH3MBuilderTest", "core", "ASCII", &buf);
	return loader.loadMapHeader();
}

// Returned aggregate keeps the backing buffer and EditorCallback alive for the lifetime of the CMap.
struct LoadedMap
{
	std::unique_ptr<CMap>           map;
	std::unique_ptr<CMemoryBuffer>  stream;
	std::unique_ptr<EditorCallback> cb;
};

LoadedMap loadMap(std::vector<uint8_t> bytes)
{
	LoadedMap r;
	r.stream = std::make_unique<CMemoryBuffer>();
	r.stream->write(bytes.data(), static_cast<si64>(bytes.size()));
	r.stream->seek(0);
	// Same callback the mapeditor uses — provides the non-null IGameInfoCallback that
	// per-type object handlers (Keymaster / Border Guard / ...) assert on in their factory.
	r.cb = std::make_unique<EditorCallback>(nullptr);

	CMapLoaderH3M loader("TinyH3MBuilderTest", "core", "ASCII", r.stream.get());
	r.map = loader.loadMap(r.cb.get());
	r.cb->setMap(r.map.get());
	return r;
}
}

TEST(TinyH3MBuilderTest, TwoLevelMapHasTwoLayers)
{
	auto bytes = TinyH3M::TinyH3MBuilder(EMapFormat::ROE)
		.size(36, /*twoLevel*/ true)
		.name("TwoLevelROE")
		.buildAndDump("TwoLevelMapHasTwoLayers");

	auto header = loadHeader(std::move(bytes));
	ASSERT_NE(header, nullptr);
	EXPECT_EQ(header->mapLayers.size(), 2u);
}

TEST(TinyH3MBuilderTest, EmptyROEFullLoad)
{
	auto bytes = TinyH3M::TinyH3MBuilder(EMapFormat::ROE)
		.size(36, /*twoLevel*/ false)
		.name("EmptyROEFull")
		.difficulty(EMapDifficulty::NORMAL)
		.buildAndDump("EmptyROEFullLoad");

	auto loaded = loadMap(std::move(bytes));
	ASSERT_NE(loaded.map, nullptr);
	EXPECT_EQ(loaded.map->width, 36);
	EXPECT_EQ(loaded.map->height, 36);
	EXPECT_EQ(loaded.map->levels(), 1);
	EXPECT_TRUE(loaded.map->getHeroesOnMap().empty());
}

TEST(TinyH3MBuilderTest, EmptySODFullLoad)
{
	auto bytes = TinyH3M::TinyH3MBuilder(EMapFormat::SOD)
		.size(36, /*twoLevel*/ false)
		.name("EmptySODFull")
		.difficulty(EMapDifficulty::NORMAL)
		.buildAndDump("EmptySODFullLoad");

	auto loaded = loadMap(std::move(bytes));
	ASSERT_NE(loaded.map, nullptr);
	EXPECT_EQ(loaded.map->width, 36);
	EXPECT_EQ(loaded.map->height, 36);
	EXPECT_EQ(loaded.map->levels(), 1);
	EXPECT_TRUE(loaded.map->getHeroesOnMap().empty());
}

TEST(TinyH3MBuilderTest, AllSupportedObjectsLoad)
{
	// One instance of every object kind the builder currently emits. Acceptance
	// bar is "CMapLoaderH3M::loadMap returns a non-null CMap" — per-type body
	// assertions live in dedicated tests.
	auto bytes = TinyH3M::TinyH3MBuilder(EMapFormat::SOD)
		.size(36, /*twoLevel*/ false)
		.name("AllObjects")
		.playerActive(PlayerColor(0))
		.randomTown ({ 5,  5, 0}, PlayerColor(0))
		.monster    ({10, 10, 0}, CreatureID(27), /*count*/ 7) // Gold Dragon
		.resource   ({11, 10, 0}, GameResID(GameResID::GOLD), /*amount*/ 1000)
		.artifact   ({12, 10, 0}, ArtifactID(7))               // Centaur Axe — first artifact with a map sprite
		.keymaster  ({15, 15, 0}, /*color*/ 0)
		.borderGuard({16, 15, 0}, /*color*/ 0)
		.borderGate ({17, 15, 0}, /*color*/ 0)
		.questGuard ({20, 20, 0})
		.seerHut    ({22, 22, 0})
		.buildAndDump("AllSupportedObjectsLoad");

	auto loaded = loadMap(std::move(bytes));
	ASSERT_NE(loaded.map, nullptr);
	EXPECT_EQ(loaded.map->width, 36);
	// 1 town + 1 monster + 1 resource + 1 artifact + 3 border + 1 quest guard + 1 seer = 9
	size_t live = 0;
	for(const auto & obj : loaded.map->objects)
		if(obj) ++live;
	EXPECT_EQ(live, 9u);
}

namespace
{
template<class T>
const T * findFirst(const CMap & map)
{
	for(const auto & obj : map.objects)
	{
		if(const auto * casted = dynamic_cast<const T *>(obj.get()))
			return casted;
	}
	return nullptr;
}

template<class T>
std::vector<const T *> findAll(const CMap & map)
{
	std::vector<const T *> out;
	for(const auto & obj : map.objects)
	{
		if(const auto * casted = dynamic_cast<const T *>(obj.get()))
			out.push_back(casted);
	}
	return out;
}
}

TEST(TinyH3MBuilderTest, HeroesPlacement)
{
	// Fixed hero (Orrin, type 0) owned by red, plus a random hero owned by blue.
	auto bytes = TinyH3M::TinyH3MBuilder(EMapFormat::SOD)
		.size(36, /*twoLevel*/ false)
		.name("HeroesPlacement")
		.playerActive(PlayerColor(0))
		.playerActive(PlayerColor(1))
		.hero      ({5, 5, 0}, HeroTypeID(0), PlayerColor(0))
		.randomHero({6, 6, 0}, PlayerColor(1))
		.buildAndDump("HeroesPlacement");

	auto loaded = loadMap(std::move(bytes));
	ASSERT_NE(loaded.map, nullptr);

	const auto heroes = findAll<CGHeroInstance>(*loaded.map);
	ASSERT_EQ(heroes.size(), 2u);

	const CGHeroInstance * fixed = nullptr;
	const CGHeroInstance * random = nullptr;
	for(const auto * h : heroes)
	{
		if(h->getOwner() == PlayerColor(0))
			fixed = h;
		else if(h->getOwner() == PlayerColor(1))
			random = h;
	}

	ASSERT_NE(fixed, nullptr);
	ASSERT_NE(random, nullptr);
	EXPECT_EQ(fixed->getHeroTypeID(), HeroTypeID(0));
	EXPECT_EQ(fixed->anchorPos(), int3(5, 5, 0));
	EXPECT_EQ(random->anchorPos(), int3(6, 6, 0));
	EXPECT_EQ(loaded.map->getObjectiveObjectFrom(fixed->anchorPos(), Obj::HERO), fixed);
}

TEST(TinyH3MBuilderTest, SpellScrollLoads)
{
	// SpellID 15 = Magic Arrow (always available, no expansion required).
	const SpellID spell{15};

	auto bytes = TinyH3M::TinyH3MBuilder(EMapFormat::SOD)
		.size(36, /*twoLevel*/ false)
		.name("ScrollMagicArrow")
		.scroll({10, 10, 0}, spell)
		.buildAndDump("SpellScrollLoads");

	auto loaded = loadMap(std::move(bytes));
	ASSERT_NE(loaded.map, nullptr);

	const auto * scroll = findFirst<CGArtifact>(*loaded.map);
	ASSERT_NE(scroll, nullptr);
	EXPECT_EQ(scroll->anchorPos(), int3(10, 10, 0));

	const auto * inst = scroll->getArtifactInstance();
	ASSERT_NE(inst, nullptr);
	EXPECT_EQ(inst->getScrollSpellID(), spell);
}

TEST(TinyH3MBuilderTest, HeroCustomisation)
{
	// Hero with garrison, experience, primary skills, and a backpack artifact.
	auto bytes = TinyH3M::TinyH3MBuilder(EMapFormat::SOD)
		.size(36, /*twoLevel*/ false)
		.name("HeroCustom")
		.playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0))
		.heroGarrison({{CreatureID(0), 10}, {CreatureID(1), 5}}) // 10 pikemen + 5 archers
		.heroExperience(40000)                                    // ~level 5
		.heroPrimary(5, 3, 1, 2)
		.heroBackpack({ArtifactID(7)})                            // Centaur Axe in backpack
		.buildAndDump("HeroCustomisation");

	auto loaded = loadMap(std::move(bytes));
	ASSERT_NE(loaded.map, nullptr);

	const auto * hero = findFirst<CGHeroInstance>(*loaded.map);
	ASSERT_NE(hero, nullptr);
	EXPECT_EQ(hero->exp, 40000);
	// Garrison: 7 slots, first two populated.
	EXPECT_TRUE(hero->hasStackAtSlot(SlotID(0)));
	EXPECT_TRUE(hero->hasStackAtSlot(SlotID(1)));
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 10);
	EXPECT_EQ(hero->getStackCount(SlotID(1)), 5);
}

TEST(TinyH3MBuilderTest, DimensionDoorHeroLoadout)
{
	// SpellID 8 = Dimension Door. Keep this tiny generated map as an in-repo
	// DD fixture foundation instead of relying only on external test maps.
	const SpellID dimensionDoor{8};

	auto bytes = TinyH3M::TinyH3MBuilder(EMapFormat::SOD)
		.size(36, /*twoLevel*/ false)
		.name("DimensionDoorHero")
		.playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0))
		.heroPrimary(10, 10, 10, 50)
		.heroSecondarySkills({{SecondarySkill::AIR_MAGIC, 3}})
		.heroEquipped({{ArtifactPosition::SPELLBOOK, ArtifactID::SPELLBOOK}})
		.heroSpells({dimensionDoor})
		.buildAndDump("DimensionDoorHeroLoadout");

	auto loaded = loadMap(std::move(bytes));
	ASSERT_NE(loaded.map, nullptr);

	const auto * hero = findFirst<CGHeroInstance>(*loaded.map);
	ASSERT_NE(hero, nullptr);
	EXPECT_TRUE(hero->hasSpellbook());
	EXPECT_TRUE(hero->spellbookContainsSpell(dimensionDoor));
	EXPECT_EQ(hero->getSecSkillLevel(SecondarySkill::AIR_MAGIC), 3);
}

TEST(TinyH3MBuilderTest, KillCreatureQuest)
{
	// Monster + quest guard that targets the monster's wire identifier.
	TinyH3M::TinyH3MBuilder b(EMapFormat::SOD);
	b.size(36).name("KillQuest")
		.playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0))
		.monster({10, 10, 0}, CreatureID(27), 3); // Gold Dragons
	const auto target = b.lastHandle();
	ASSERT_NE(target.wireIdentifier, 0u);

	b.questGuard({15, 15, 0}, TinyH3M::TinyH3MBuilder::missionKillCreature(target)
		.withLastDay(10)
		.withFirstVisitText("Slay the dragons"));

	auto loaded = loadMap(b.buildAndDump("KillCreatureQuest"));
	ASSERT_NE(loaded.map, nullptr);

	const auto * guard = findFirst<QuestGuard>(*loaded.map);
	ASSERT_NE(guard, nullptr);
	EXPECT_EQ(guard->getQuest().lastDay, 10);
	// Loader resolves the uint32 wire id to an ObjectInstanceID in afterRead;
	// the resolved target lives in quest.mission.destroyedObjects once mapping completes.
	EXPECT_FALSE(guard->getQuest().mission.destroyedObjects.empty());
}

namespace
{
using TownArmy = std::vector<std::pair<CreatureID, uint16_t>>;

/// Exercises the H3M reader and the real new-game initialization, not editor-only loading.
class TinyH3MTownGarrisonTest : public ::testing::TestWithParam<EMapFormat>
{
protected:
	ServicesMock services;

	LoadedMap loadTown(const std::optional<TownArmy> & army)
	{
		TinyH3M::TinyH3MBuilder builder(GetParam());
		builder.size(36).name("TownArmyPersistence")
			.town({5, 5, 0}, FactionID::CASTLE, PlayerColor::NEUTRAL);
		if(army)
			builder.townGarrison(*army);
		return loadMap(builder.build());
	}

	CGTownInstance * loadedTown(LoadedMap & loaded) const
	{
		return dynamic_cast<CGTownInstance *>(loaded.map->objects.front().get());
	}

	std::unique_ptr<CGameState> initializeTown(const std::optional<TownArmy> & army, PlayerColor owner, int seed)
	{
		TinyH3M::TinyH3MBuilder builder(GetParam());
		builder.size(36).name("TownInitialGarrison")
			.playerActive(PlayerColor(0))
			.town({5, 5, 0}, FactionID::CASTLE, PlayerColor(0))
			.town({20, 20, 0}, FactionID::CASTLE, owner);
		if(army)
			builder.townGarrison(*army);
		builder.hero({8, 8, 0}, HeroTypeID(0), PlayerColor(0));

		MapServiceTinyH3M mapService(builder.build(), nullptr);
		StartInfo info;
		info.mapname = "tiny";
		info.mode = EStartMode::NEW_GAME;
		info.difficulty = 0;
		auto header = mapService.loadMapHeader(ResourcePath(info.mapname));
		auto & player = info.playerInfos[PlayerColor(0)];
		player.color = PlayerColor(0);
		player.connectedPlayerIDs.insert(static_cast<PlayerConnectionID>(0));
		player.name = "Player";
		player.bonus = PlayerStartingBonus::GOLD;
		player.castle = header->players[0].defaultCastle();
		player.hero = header->players[0].defaultHero();

		auto state = std::make_unique<CGameState>();
		state->preInit(&services);
		GameRandomizer randomizer(*state);
		randomizer.setSeed(seed);
		Load::ProgressAccumulator progress;
		state->init(&mapService, &info, randomizer, progress, false);
		return state;
	}

	const CGTownInstance * targetTown(const CGameState & state) const
	{
		// The fixture's second object is the target; the first is the human's starting town.
		return dynamic_cast<const CGTownInstance *>(state.getMap().objects.at(1).get());
	}
};

TEST_P(TinyH3MTownGarrisonTest, ParserDistinguishesArmyPresenceAndPreservesFollowingObjects)
{
	for(bool randomTown : {false, true})
	{
		SCOPED_TRACE(randomTown ? "random town" : "fixed town");
		auto bytesFor = [&](const std::optional<TownArmy> & army)
		{
			TinyH3M::TinyH3MBuilder builder(GetParam());
			builder.size(36).name("TownArmyWire");
			if(randomTown)
				builder.randomTown({5, 5, 0}, PlayerColor::NEUTRAL);
			else
				builder.town({5, 5, 0}, FactionID::CASTLE, PlayerColor::NEUTRAL);
			if(army)
				builder.townGarrison(*army);
			// A following object detects an incorrectly omitted/mis-sized army payload.
			builder.resource({15, 15, 0}, GameResID(GameResID::WOOD), 1234);
			return builder.build();
		};

		auto absentBytes = bytesFor(std::nullopt);
		auto emptyBytes = bytesFor(TownArmy{});
		// Exercise both ends of the seven-slot payload, including empty interior slots.
		TownArmy customArmy(7, {CreatureID::NONE, 0});
		customArmy.front() = {CreatureID(0), 17};
		customArmy.back() = {CreatureID(1), 23};
		auto customBytes = bytesFor(customArmy);
		const size_t armyBytes = GetParam() == EMapFormat::ROE ? 21 : 28;
		EXPECT_EQ(emptyBytes.size(), absentBytes.size() + armyBytes);
		EXPECT_EQ(customBytes.size(), emptyBytes.size());

		auto absent = loadMap(std::move(absentBytes));
		auto empty = loadMap(std::move(emptyBytes));
		auto custom = loadMap(std::move(customBytes));
		for(const CMap * map : {absent.map.get(), empty.map.get(), custom.map.get()})
		{
			ASSERT_NE(map, nullptr);
			const auto * town = findFirst<CGTownInstance>(*map);
			ASSERT_NE(town, nullptr);
			EXPECT_EQ(town->getOwner(), PlayerColor::NEUTRAL);
			EXPECT_EQ(town->formation, EArmyFormation::LOOSE);
			const auto * resource = findFirst<CGResource>(*map);
			ASSERT_NE(resource, nullptr);
			EXPECT_EQ(resource->getAmount(), 1234u);
		}
		EXPECT_EQ(findFirst<CGTownInstance>(*absent.map)->stacksCount(), 0);
		EXPECT_EQ(findFirst<CGTownInstance>(*empty.map)->stacksCount(), 0);
		EXPECT_FALSE(findFirst<CGTownInstance>(*absent.map)->customInitialGarrison);
		EXPECT_TRUE(findFirst<CGTownInstance>(*empty.map)->customInitialGarrison);
		const auto * town = findFirst<CGTownInstance>(*custom.map);
		EXPECT_TRUE(town->customInitialGarrison);
		ASSERT_EQ(town->stacksCount(), 2);
		EXPECT_EQ(town->getStack(SlotID(0)).getCreatureID(), CreatureID(0));
		EXPECT_EQ(town->getStackCount(SlotID(0)), 17);
		EXPECT_EQ(town->getStack(SlotID(6)).getCreatureID(), CreatureID(1));
		EXPECT_EQ(town->getStackCount(SlotID(6)), 23);
	}
}

TEST_P(TinyH3MTownGarrisonTest, RealInitializationHonorsExplicitEmptyWithoutChangingOtherArmies)
{
	// An unspecified neutral town may legitimately roll no guards. Find a bounded,
	// seeded positive control through real initialization, then replay that exact
	// seed with only the target's army-presence flag changed. Record it in test XML.
	std::optional<int> guardSeed;
	std::unique_ptr<CGameState> absent;
	for(int seed = 0; seed < 32; ++seed)
	{
		auto candidate = initializeTown(std::nullopt, PlayerColor::NEUTRAL, seed);
		const auto * town = targetTown(*candidate);
		ASSERT_NE(town, nullptr);
		if(town->stacksCount() > 0)
		{
			guardSeed = seed;
			absent = std::move(candidate);
			break;
		}
	}
	ASSERT_TRUE(guardSeed.has_value()) << "No seeded positive control found; do not weaken the empty-army assertion";
	RecordProperty("neutralGuardSeed", *guardSeed);
	SCOPED_TRACE("neutral guard seed " + std::to_string(*guardSeed));
	ASSERT_GT(targetTown(*absent)->stacksCount(), 0);

	auto repeat = initializeTown(std::nullopt, PlayerColor::NEUTRAL, *guardSeed);
	ASSERT_NE(targetTown(*repeat), nullptr);
	ASSERT_EQ(targetTown(*repeat)->getCreatureMap(), targetTown(*absent)->getCreatureMap());
	for(int slot = 0; slot < GameConstants::ARMY_SIZE; ++slot)
	{
		if(!targetTown(*absent)->slotEmpty(SlotID(slot)))
		{
			EXPECT_EQ(targetTown(*repeat)->getStackCount(SlotID(slot)), targetTown(*absent)->getStackCount(SlotID(slot)));
		}
	}

	auto empty = initializeTown(TownArmy{}, PlayerColor::NEUTRAL, *guardSeed);
	ASSERT_NE(targetTown(*empty), nullptr);
	EXPECT_EQ(targetTown(*empty)->stacksCount(), 0) << "Explicit empty H3M army must bypass initial neutral guards";

	auto custom = initializeTown(TownArmy{{CreatureID(0), 17}}, PlayerColor::NEUTRAL, *guardSeed);
	const auto * customTown = targetTown(*custom);
	ASSERT_NE(customTown, nullptr);
	ASSERT_EQ(customTown->stacksCount(), 1);
	EXPECT_EQ(customTown->getStack(SlotID(0)).getCreatureID(), CreatureID(0));
	EXPECT_EQ(customTown->getStackCount(SlotID(0)), 17);

	for(const std::optional<TownArmy> & army : {std::optional<TownArmy>{}, std::optional<TownArmy>{TownArmy{}}})
	{
		auto owned = initializeTown(army, PlayerColor(0), *guardSeed);
		ASSERT_NE(targetTown(*owned), nullptr);
		EXPECT_EQ(targetTown(*owned)->stacksCount(), 0) << "Owned towns never receive initial neutral guards";
	}
}

TEST_P(TinyH3MTownGarrisonTest, JsonPreservesExplicitEmptyAndLegacyDefaults)
{
	for(bool custom : {false, true})
	{
		auto source = loadTown(custom ? std::optional<TownArmy>{TownArmy{}} : std::nullopt);
		auto * town = loadedTown(source);
		ASSERT_NE(town, nullptr);
		ASSERT_EQ(town->customInitialGarrison, custom);
		JsonNode node; // A fresh node ensures omitted defaults cannot leave stale keys.
		JsonSerializer writer(nullptr, node);
		town->CGObjectInstance::serializeJson(writer);
		EXPECT_EQ(node["options"].Struct().count("customInitialGarrison"), custom ? 1u : 0u);
		EXPECT_EQ(node["options"].Struct().count("army"), 0u);

		auto destination = loadTown(std::nullopt);
		auto * restored = loadedTown(destination);
		ASSERT_NE(restored, nullptr);
		restored->customInitialGarrison = !custom;
		JsonDeserializer reader(nullptr, node);
		restored->CGObjectInstance::serializeJson(reader);
		EXPECT_EQ(restored->customInitialGarrison, custom);
		EXPECT_EQ(restored->stacksCount(), 0);
	}

	// Missing, false, null, non-bool, and a legacy empty army all retain legacy defaults.
	for(int variant = 0; variant < 5; ++variant)
	{
		SCOPED_TRACE(variant);
		JsonNode node;
		if(variant == 1)
			node["options"]["customInitialGarrison"].Bool() = false;
		if(variant == 2)
			node["options"]["customInitialGarrison"] = JsonNode();
		if(variant == 3)
			node["options"]["customInitialGarrison"].String() = "true";
		if(variant == 4)
			node["options"]["army"].Vector();
		auto destination = loadTown(std::nullopt);
		auto * restored = loadedTown(destination);
		ASSERT_NE(restored, nullptr);
		restored->customInitialGarrison = true;
		JsonDeserializer reader(nullptr, node);
		restored->CGObjectInstance::serializeJson(reader);
		EXPECT_FALSE(restored->customInitialGarrison);
		EXPECT_EQ(restored->stacksCount(), 0);
	}

	auto source = loadTown(TownArmy{{CreatureID(0), 17}});
	auto * town = loadedTown(source);
	ASSERT_NE(town, nullptr);
	JsonNode legacy;
	JsonSerializer writer(nullptr, legacy);
	town->CGObjectInstance::serializeJson(writer);
	legacy["options"].Struct().erase("customInitialGarrison");
	auto destination = loadTown(std::nullopt);
	auto * restored = loadedTown(destination);
	ASSERT_NE(restored, nullptr);
	restored->customInitialGarrison = true;
	JsonDeserializer reader(nullptr, legacy);
	restored->CGObjectInstance::serializeJson(reader);
	EXPECT_FALSE(restored->customInitialGarrison);
	ASSERT_EQ(restored->stacksCount(), 1);
	EXPECT_EQ(restored->getStack(SlotID(0)).getCreatureID(), CreatureID(0));
	EXPECT_EQ(restored->getStackCount(SlotID(0)), 17);
}

TEST_P(TinyH3MTownGarrisonTest, BinaryPreservesCurrentFlagAndDefaultsLegacyWithoutMutatingWriter)
{
	for(auto version : {ESerializationVersion::CURRENT, ESerializationVersion::RECORD_TEXTS_METASTRING})
	{
		SCOPED_TRACE(static_cast<int>(version));
		const std::vector<std::optional<TownArmy>> armies = {
			std::nullopt, TownArmy{}, TownArmy{{CreatureID(0), 17}}
		};
		for(const auto & army : armies)
		{
			auto source = loadTown(army);
			auto * town = loadedTown(source);
			ASSERT_NE(town, nullptr);
			const bool originalFlag = town->customInitialGarrison;
			ASSERT_EQ(originalFlag, army.has_value());
			CMemorySerializer memory;
			memory.oser.version = version;
			memory.iser.version = version;
			const uint32_t sentinel = 0x73ac490e;
			memory.oser & *town;
			memory.oser & sentinel;
			EXPECT_EQ(town->customInitialGarrison, originalFlag) << "Legacy writes must not mutate the source";

			auto destination = loadTown(std::nullopt);
			auto * restored = loadedTown(destination);
			ASSERT_NE(restored, nullptr);
			restored->customInitialGarrison = true;
			memory.iser.cb = destination.cb.get();
			memory.iser & *restored;
			uint32_t readSentinel = 0;
			memory.iser & readSentinel;
			EXPECT_EQ(readSentinel, sentinel) << "Town serialization must preserve the following field";
			EXPECT_EQ(restored->customInitialGarrison, version == ESerializationVersion::CURRENT && originalFlag);
			if(army && !army->empty())
			{
				ASSERT_EQ(restored->stacksCount(), 1);
				EXPECT_EQ(restored->getStack(SlotID(0)).getCreatureID(), CreatureID(0));
				EXPECT_EQ(restored->getStackCount(SlotID(0)), 17);
			}
			else
			{
				EXPECT_EQ(restored->stacksCount(), 0);
			}
		}
	}
}

INSTANTIATE_TEST_SUITE_P(OriginalFormats, TinyH3MTownGarrisonTest,
	::testing::Values(EMapFormat::ROE, EMapFormat::AB, EMapFormat::SOD));
}
