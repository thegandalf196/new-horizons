/*
 * NewHorizonsStartingEquipmentTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "battles/HeroCommandFixture.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/networkPacks/ArtifactLocation.h"
#include "../../lib/entities/artifact/CArtifactInstance.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/gameState/TavernHeroesPool.h"
#include "../../lib/mapObjects/army/CSimpleArmy.h"
#include "../../lib/serializer/JsonDeserializer.h"
#include "../../lib/serializer/JsonSerializer.h"

namespace
{
enum class Equipment { DEFAULT, H3M_EMPTY, H3M_TENT, JSON_EMPTY, JSON_DEFAULT, JSON_ROUNDTRIP };

class NewHorizonsStartingEquipmentTest : public HeroCommandFixture,
	public ::testing::WithParamInterface<const char *>
{
protected:
	Equipment equipment = Equipment::DEFAULT;
	bool machineRowEnabled = false;
	bool explicitArmy = false;
	bool legacy = false;
	bool poolHero = false;

	HeroTypeID type() const { return HeroTypeID(HeroTypeID::decode(GetParam())); }
	CGHeroInstance * actor() const
	{
		return poolHero ? gameState()->getMap().tryGetFromHeroPool(type())
			: gameState()->getMap().getHero(type());
	}
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		if(legacy)
		{
			JsonNode empty;
			empty.setOverrideFlag(true);
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, empty);
			loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, empty);
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, empty);
		}
		JsonNode chances;
		chances.Vector().emplace_back(100);
		chances.Vector().emplace_back(machineRowEnabled ? 100 : 0);
		chances.Vector().emplace_back(0);
		loaded->overrideGameSetting(EGameSettings::HEROES_STARTING_STACKS_CHANCES, chances);
		if(equipment == Equipment::H3M_EMPTY)
		{
			const auto * hero = loaded->getHero(type());
			ASSERT_NE(hero, nullptr);
			EXPECT_TRUE(hero->getRawStartingSpellIds().contains(SpellID::SPELLBOOK_PRESET));
		}
		if(equipment == Equipment::JSON_EMPTY || equipment == Equipment::JSON_DEFAULT
			|| equipment == Equipment::JSON_ROUNDTRIP)
		{
			auto * hero = loaded->getHero(type());
			ASSERT_NE(hero, nullptr);
			JsonNode options;
			if(equipment != Equipment::JSON_DEFAULT)
				options["artifacts"].Struct();
			JsonDeserializer reader(nullptr, options);
			hero->serializeJsonArtifacts(reader, "artifacts", loaded);
			if(equipment == Equipment::JSON_ROUNDTRIP)
			{
				JsonNode saved;
				JsonSerializer writer(nullptr, saved);
				hero->serializeJsonArtifacts(writer, "artifacts", loaded);
				ASSERT_TRUE(saved.isStruct());
				ASSERT_TRUE(saved.Struct().contains("artifacts"));
				ASSERT_TRUE(saved["artifacts"].isStruct());
				EXPECT_TRUE(saved["artifacts"].Struct().empty());
				JsonDeserializer roundtrip(nullptr, saved);
				hero->serializeJsonArtifacts(roundtrip, "artifacts", loaded);
			}
		}
	}
	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, poolHero ? HeroTypeID(HeroTypeID::decode("core:christian")) : type(), PlayerColor(0))
			.heroExperience(0);
		if(equipment == Equipment::H3M_EMPTY)
			builder.heroEquipped({});
		if(equipment == Equipment::H3M_TENT)
			builder.heroEquipped({{ArtifactPosition::MACH3, ArtifactID::FIRST_AID_TENT}});
		if(explicitArmy)
			builder.heroGarrison({{CreatureID(CreatureID::decode("core:pikeman")), 17}});
		builder.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:aislinn")), PlayerColor(1));
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
		ASSERT_NE(actor(), nullptr);
	}
	void expectTent() const
	{
		const auto * tent = actor()->getArt(ArtifactPosition::MACH3);
		ASSERT_NE(tent, nullptr);
		EXPECT_EQ(tent->getTypeId(), ArtifactID::FIRST_AID_TENT);
	}
	void removeTent()
	{
		ASSERT_NO_FATAL_FAILURE(expectTent());
		gameHandler->removeArtifact(ArtifactLocation(actor()->id, ArtifactPosition::MACH3));
		ASSERT_EQ(actor()->getArt(ArtifactPosition::MACH3), nullptr);
	}
};

TEST_P(NewHorizonsStartingEquipmentTest, DefaultGuaranteesOriginalTentWhenMachineStackChanceIsZero)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto & rows = actor()->getHeroType()->initialArmy;
	ASSERT_EQ(rows.size(), 3);
	EXPECT_EQ(rows[1].creature.toCreature()->warMachine, ArtifactID::FIRST_AID_TENT);
	EXPECT_EQ(rows[1].minAmount, 1);
	EXPECT_EQ(rows[1].maxAmount, 1);
	expectTent();
}
TEST_P(NewHorizonsStartingEquipmentTest, DefaultEquipmentIsIndependentFromExplicitCreatureArmy)
{
	explicitArmy = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	expectTent();
	EXPECT_EQ(actor()->stacksCount(), 1);
	EXPECT_EQ(actor()->getStackCount(SlotID(0)), 17);
	EXPECT_EQ(actor()->getCreature(SlotID(0))->getId(), CreatureID(CreatureID::decode("core:pikeman")));
}
TEST_P(NewHorizonsStartingEquipmentTest, H3MExplicitEmptyDoesNotReceiveTheRandomizedTent)
{
	equipment = Equipment::H3M_EMPTY;
	machineRowEnabled = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(actor()->getArt(ArtifactPosition::MACH3), nullptr);
}
TEST_P(NewHorizonsStartingEquipmentTest, JsonExplicitEmptyDoesNotReceiveTheRandomizedTent)
{
	equipment = Equipment::JSON_EMPTY;
	machineRowEnabled = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(actor()->getArt(ArtifactPosition::MACH3), nullptr);
}
TEST_P(NewHorizonsStartingEquipmentTest, JsonOmittedEquipmentStillRequestsTheDefault)
{
	equipment = Equipment::JSON_DEFAULT;
	ASSERT_NO_FATAL_FAILURE(prepare());
	expectTent();
}
TEST_P(NewHorizonsStartingEquipmentTest, AuthoredEmptyJsonRoundtripDoesNotBecomeDefault)
{
	equipment = Equipment::JSON_ROUNDTRIP;
	machineRowEnabled = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(actor()->getArt(ArtifactPosition::MACH3), nullptr);
}
TEST_P(NewHorizonsStartingEquipmentTest, ExistingAuthoredTentAndReinitKeepTheSameInstance)
{
	equipment = Equipment::H3M_TENT;
	machineRowEnabled = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto id = actor()->getArtID(ArtifactPosition::MACH3);
	GameRandomizer randomizer(*gameState());
	ASSERT_NO_THROW(actor()->initHero(randomizer));
	EXPECT_EQ(actor()->getArtID(ArtifactPosition::MACH3), id);
}
TEST_P(NewHorizonsStartingEquipmentTest, ReinitAfterRemovalAndEmptyArmyNeverRegrantsTent)
{
	machineRowEnabled = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(removeTent());
	actor()->clearSlots();
	GameRandomizer randomizer(*gameState());
	ASSERT_NO_THROW(actor()->initHero(randomizer));
	EXPECT_EQ(actor()->getArt(ArtifactPosition::MACH3), nullptr);
}
TEST_P(NewHorizonsStartingEquipmentTest, WorldSaveLoadAndReinitNeverRegrantRemovedTent)
{
	machineRowEnabled = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(removeTent());
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	auto * hero = restored.getMap().getHero(type());
	ASSERT_NE(hero, nullptr);
	hero->clearSlots();
	GameRandomizer randomizer(restored);
	ASSERT_NO_THROW(hero->initHero(randomizer));
	EXPECT_EQ(hero->getArt(ArtifactPosition::MACH3), nullptr);
}
TEST_P(NewHorizonsStartingEquipmentTest, CrossoverAndReinitNeverRegrantRemovedTent)
{
	machineRowEnabled = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(removeTent());
	CampaignState campaign;
	const auto saved = campaign.crossoverSerialize(actor());
	auto hero = campaign.crossoverDeserialize(saved, &gameState()->getMap());
	ASSERT_NE(hero, nullptr);
	hero->clearSlots();
	GameRandomizer randomizer(*gameState());
	ASSERT_NO_THROW(hero->initHero(randomizer));
	EXPECT_EQ(hero->getArt(ArtifactPosition::MACH3), nullptr);
}
TEST_P(NewHorizonsStartingEquipmentTest, FreshPoolTentSurvivesHiringArmyPreviewWithoutDuplication)
{
	poolHero = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(expectTent());
	const auto id = actor()->getArtID(ArtifactPosition::MACH3);
	CSimpleArmy preview;
	GameRandomizer randomizer(*gameState());
	actor()->initArmy(randomizer.getDefault(), &preview);
	EXPECT_EQ(actor()->getArtID(ArtifactPosition::MACH3), id);
	auto taken = gameState()->heroesPool->takeHeroFromPool(type());
	ASSERT_NE(taken, nullptr);
	EXPECT_EQ(taken->getArtID(ArtifactPosition::MACH3), id);
}
TEST_P(NewHorizonsStartingEquipmentTest, LegacyRulesDoNotGainTheNewDefaultGuarantee)
{
	legacy = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(actor()->getArt(ArtifactPosition::MACH3), nullptr);
}
INSTANTIATE_TEST_SUITE_P(RionAndVerdish, NewHorizonsStartingEquipmentTest,
	::testing::Values("core:rion", "core:verdish"),
	[](const auto & info) { return std::string(info.param).substr(5); });
}
