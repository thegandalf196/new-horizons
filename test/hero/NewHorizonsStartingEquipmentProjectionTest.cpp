/*
 * NewHorizonsStartingEquipmentProjectionTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include <limits>
#include "../../lib/GameLibrary.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/entities/hero/HeroStartingProjection.h"
#include "../../lib/entities/hero/CHeroClass.h"
#include "../../lib/entities/hero/CHeroHandler.h"
#include "../../lib/filesystem/ResourcePath.h"
using namespace newHorizonsHeroes;
namespace
{
StartingHeroContext configured()
{
	StartingHeroContext result;
	result.development = JsonNode(JsonPath::builtin("config/newHorizonsHeroes"));
	result.capabilities = JsonNode(JsonPath::builtin("config/newHorizonsCapabilities"));
	result.perks = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
	result.magic = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
	return result;
}
const CHero & prototype(const char * key)
{
	return *HeroTypeID(HeroTypeID::decode(key)).toHeroType();
}
size_t tentIndex(const StartingHeroProjection & projection)
{
	for(size_t index = 0; index < projection.army.size(); ++index)
		if(projection.army[index].creature.toCreature()->warMachine == ArtifactID::FIRST_AID_TENT)
			return index;
	throw std::runtime_error("Projected Tent missing");
}
}
TEST(NewHorizonsStartingEquipmentProjection, DefaultOnlyTentProbabilityBecomesCertain)
{
	for(const char * key : {"core:rion", "core:verdish"})
	{
		SCOPED_TRACE(key);
		const auto view = projectStartingHero(prototype(key), configured());
		ASSERT_EQ(view.firstAidTentChance, 100);
		const auto chances = startingArmyChances(view, {}, {100, 88, 25});
		ASSERT_EQ(chances.size(), 3);
		EXPECT_EQ(chances[tentIndex(view)], 100);
		EXPECT_EQ(chances.front(), 100);
		EXPECT_EQ(chances.back(), 25);
	}
}
TEST(NewHorizonsStartingEquipmentProjection, AuthoredEmptyAndPresentEquipmentAreDistinct)
{
	for(const char * key : {"core:rion", "core:verdish"})
		for(bool present : {false, true})
		{
			StartingHeroOverrides authored;
			authored.defaultEquipment = false;
			authored.authoredFirstAidTent = present;
			const auto view = projectStartingHero(prototype(key), configured(), authored);
			EXPECT_EQ(view.firstAidTentChance, present ? 100 : 0);
			const auto chances = startingArmyChances(view, authored, {100, 88, 25});
			EXPECT_EQ(chances[tentIndex(view)], present ? 100 : 0);
			EXPECT_EQ(chances.back(), 25);
		}
}
TEST(NewHorizonsStartingEquipmentProjection, DefaultEquipmentSurvivesAnAuthoredCreatureArmy)
{
	StartingHeroOverrides authored;
	authored.army = std::vector<CHero::InitialArmyStack>{{17, 17, CreatureID(CreatureID::decode("core:pikeman"))}};
	const auto view = projectStartingHero(prototype("core:rion"), configured(), authored);
	ASSERT_EQ(view.army.size(), 2);
	EXPECT_EQ(view.army.front().minAmount, 17);
	EXPECT_EQ(view.army.front().maxAmount, 17);
	EXPECT_EQ(view.firstAidTentChance, 100);
	EXPECT_EQ(startingArmyChances(view, authored, {100, 88, 25}), (std::vector<int>{100, 100}));
}
TEST(NewHorizonsStartingEquipmentProjection, CapturedLegacyAndOtherHeroesDoNotInventGuarantee)
{
	const auto & hero = prototype("core:rion");
	auto captured = configured();
	captured.development = resolveHeroRules(captured.development, hero.heroClass->getId());
	captured.capabilities = resolveCapabilityRules(captured.capabilities, hero.heroClass->getId());
	captured.resolved = true;
	const auto view = projectStartingHero(hero, captured);
	EXPECT_FALSE(view.firstAidTentChance);
	EXPECT_EQ(startingArmyChances(view, {}, {100, 88, 25}), (std::vector<int>{100, 88, 25}));
	EXPECT_FALSE(guaranteesDefaultFirstAidTent(hero.getId(), JsonNode()));
	const auto ordinary = projectStartingHero(prototype("core:christian"), configured());
	EXPECT_FALSE(ordinary.firstAidTentChance);
	EXPECT_EQ(startingArmyChances(ordinary, {}, {100, 88, 25}), (std::vector<int>{100, 88, 25}));
}
TEST(NewHorizonsStartingEquipmentProjection, UnknownAuthoredEquipmentNeverClaimsCertainty)
{
	StartingHeroOverrides authored;
	authored.defaultEquipment = false;
	const auto view = projectStartingHero(prototype("core:rion"), configured(), authored);
	EXPECT_FALSE(view.firstAidTentChance);
}
TEST(NewHorizonsStartingEquipmentProjection, MissingRandomAndUnregisteredHeroIdentityAreSafe)
{
	const auto rules = configured().development;
	EXPECT_FALSE(guaranteesDefaultFirstAidTent(HeroTypeID(HeroTypeID::NONE), rules));
	EXPECT_FALSE(guaranteesDefaultFirstAidTent(HeroTypeID(HeroTypeID::RANDOM), rules));
	EXPECT_FALSE(guaranteesDefaultFirstAidTent(HeroTypeID(std::numeric_limits<int32_t>::max()), rules));
}

TEST(NewHorizonsStartingEquipmentProjection, SaveCampaignAndOngoingWorldDoNotClaimFreshDefaults)
{
	auto context = configured();
	context.freshEquipmentDefaults = false;
	const auto view = projectStartingHero(prototype("core:rion"), context);
	EXPECT_FALSE(view.firstAidTentChance);
	EXPECT_EQ(startingArmyChances(view, {}, {100, 88, 25}), (std::vector<int>{100, 88, 25}));
}
