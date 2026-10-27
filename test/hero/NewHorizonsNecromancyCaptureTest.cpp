/*
 * NewHorizonsNecromancyCaptureTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../server/battles/HistoricalNecromancy087Fixture.h"
#include "../../lib/StartInfo.h"
#include "../../lib/GameSettings.h"
#include "../../lib/entities/hero/NewHorizonsNecromancyPolicy.h"
#include "../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/gameState/QuestInfo.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/serializer/CMemorySerializer.h"

namespace
{
JsonNode capturedNecromancy(bool current = true)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsPerks"));
	if(!current)
		rules.Struct().erase(newHorizonsNecromancy::POLICY_KEY);
	rules.setOverrideFlag(true);
	return rules;
}

template<typename Value>
void rejectsBeforePrefix(Value & value)
{
	for(const auto version : {ESerializationVersion::NEW_HORIZONS_REVISED_ORDERS,
		ESerializationVersion::NEW_HORIZONS_SKELETON_NECROMANCY})
	{
		CMemorySerializer old;
		old.oser.version = version;
		try
		{
			value.serialize(old.oser);
			FAIL() << "Expected captured Necromancy policy rejection";
		}
		catch(const std::runtime_error & error)
		{
			EXPECT_STREQ(error.what(), "Cannot discard captured Necromancy policy");
		}
		EXPECT_TRUE(old.extractBuffer().empty());
	}
}
}

TEST(NewHorizonsNecromancyCapture, RegistryRejectsMalformedPresentPolicyAndRetainsAbsentHistory)
{
	auto current = capturedNecromancy();
	ASSERT_TRUE(newHorizonsNecromancy::policy(current));
	EXPECT_NO_THROW(newHorizonsHeroes::validatePerkRules(current));
	for(const auto & invalid : {JsonNode(), JsonNode(JsonMap{})})
	{
		auto forged = current;
		forged[newHorizonsNecromancy::POLICY_KEY] = invalid;
		EXPECT_THROW(newHorizonsHeroes::validatePerkRules(forged), std::runtime_error);
	}
	current[newHorizonsNecromancy::POLICY_KEY]["version"].Integer() = 3;
	EXPECT_THROW(newHorizonsHeroes::validatePerkRules(current), std::runtime_error);
	const auto historical = capturedNecromancy(false);
	EXPECT_FALSE(newHorizonsNecromancy::policy(historical));
	EXPECT_NO_THROW(newHorizonsNecromancy::validatePolicySerialization(historical, false, false));
}

TEST(NewHorizonsNecromancyCapture, PerkStateRefusesLossyWriterAndRoundTripsBothPolicyEpochs)
{
	newHorizonsHeroes::PerkState current;
	current.rules = capturedNecromancy();
	rejectsBeforePrefix(current);
	CMemorySerializer wire;
	ASSERT_NO_THROW(current.serialize(wire.oser));
	newHorizonsHeroes::PerkState restored;
	ASSERT_NO_THROW(restored.serialize(wire.iser));
	EXPECT_EQ(restored.rules, current.rules);

	newHorizonsHeroes::PerkState historical;
	historical.rules = capturedNecromancy(false);
	CMemorySerializer old;
	old.oser.version = old.iser.version = ESerializationVersion::NEW_HORIZONS_REVISED_ORDERS;
	ASSERT_NO_THROW(historical.serialize(old.oser));
	// Reusing an object must erase its previously present policy.
	ASSERT_NO_THROW(restored.serialize(old.iser));
	EXPECT_EQ(restored.rules, historical.rules);
	EXPECT_FALSE(newHorizonsNecromancy::policy(restored.rules));

	for(const auto version : {ESerializationVersion::NEW_HORIZONS_REVISED_ORDERS,
		ESerializationVersion::NEW_HORIZONS_SKELETON_NECROMANCY})
	{
		CMemorySerializer forged;
		forged.iser.version = version;
		forged.oser & current.rules;
		try
		{
			restored.serialize(forged.iser);
			FAIL() << "Expected captured Necromancy policy rejection";
		}
		catch(const std::runtime_error & error)
		{
			EXPECT_STREQ(error.what(), "Cannot discard captured Necromancy policy");
		}
	}
}

TEST(NewHorizonsNecromancyCapture, SettingsReaderRejectsBeforeReplacingExistingOverrides)
{
	GameSettings current;
	const auto rules = capturedNecromancy();
	current.addOverride(EGameSettings::HEROES_NEW_HORIZONS_PERKS, rules);
	rejectsBeforePrefix(current);
	CMemorySerializer wire;
	ASSERT_NO_THROW(current.serialize(wire.oser));
	GameSettings decoded;
	ASSERT_NO_THROW(decoded.serialize(wire.iser));
	EXPECT_EQ(decoded.getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS), rules);

	const auto historical = capturedNecromancy(false);
	GameSettings target;
	target.addOverride(EGameSettings::HEROES_NEW_HORIZONS_PERKS, historical);
	JsonNode raw;
	raw["heroes"]["newHorizonsPerks"] = rules;
	for(const auto version : {ESerializationVersion::NEW_HORIZONS_REVISED_ORDERS,
		ESerializationVersion::NEW_HORIZONS_SKELETON_NECROMANCY})
	{
		CMemorySerializer forged;
		forged.iser.version = version;
		forged.oser & raw;
		try
		{
			target.serialize(forged.iser);
			FAIL() << "Expected captured Necromancy policy rejection";
		}
		catch(const std::runtime_error & error)
		{
			EXPECT_STREQ(error.what(), "Cannot discard captured Necromancy policy");
		}
		EXPECT_EQ(target.getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS), historical);
	}
	CMemorySerializer old;
	old.oser.version = old.iser.version = ESerializationVersion::NEW_HORIZONS_REVISED_ORDERS;
	ASSERT_NO_THROW(target.serialize(old.oser));
	ASSERT_NO_THROW(decoded.serialize(old.iser));
	EXPECT_EQ(decoded.getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS), historical);
}

TEST(NewHorizonsNecromancyCapture, HeroAndEnclosingMapWritersRefuseBeforeTheirPrefixes)
{
	auto hero = std::make_shared<CGHeroInstance>(nullptr);
	hero->setHeroType(HeroTypeID(0));
	// Explicit fixture capture; production continues to capture through its world.
	const_cast<newHorizonsHeroes::PerkState &>(hero->getPerkState()).rules = capturedNecromancy();
	rejectsBeforePrefix(*hero);
	CMap map(nullptr);
	map.width = map.height = 1;
	map.mapLayers = {MapLayerId::SURFACE};
	map.initTerrain();
	map.objects.push_back(hero);
	rejectsBeforePrefix(map);
	map.objects.clear();
	map.addToHeroPool(hero);
	ASSERT_EQ(map.tryGetFromHeroPool(hero->getHeroTypeID()), hero.get());
	rejectsBeforePrefix(map);
	ASSERT_EQ(map.tryTakeFromHeroPool(hero->getHeroTypeID()), hero);
	EXPECT_TRUE(map.getHeroesInPool().empty());
	map.objects.clear(); // Remove the null ID reservation left by the pool API.
	map.overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, capturedNecromancy());
	rejectsBeforePrefix(map);
	CMemorySerializer wire;
	ASSERT_NO_THROW(map.serialize(wire.oser));
	CMap restored(nullptr);
	ASSERT_NO_THROW(restored.serialize(wire.iser));
	EXPECT_EQ(restored.getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS), capturedNecromancy());
}

TEST(NewHorizonsNecromancyCapture, WorldCaptureRejectsLossyWriterBeforePrefix)
{
	CGameState world;
	const_cast<JsonNode &>(world.getHeroPerkRules()) = capturedNecromancy();
	rejectsBeforePrefix(world);
}

TEST(NewHorizonsNecromancyCapture, PublishedV1RulesRoundTripUnderTheirOriginalFormat)
{
	const auto published = necromancy087Test::rules();
	ASSERT_EQ(published[newHorizonsNecromancy::POLICY_KEY]["version"].Integer(), 1);
	ASSERT_NO_THROW(newHorizonsHeroes::validatePerkRules(published));
	EXPECT_NO_THROW(newHorizonsNecromancy::validatePolicySerialization(published, true, false));
	newHorizonsHeroes::PerkState state;
	state.rules = published;
	CMemorySerializer wire;
	wire.oser.version = wire.iser.version = ESerializationVersion::NEW_HORIZONS_SKELETON_NECROMANCY;
	ASSERT_NO_THROW(state.serialize(wire.oser));
	newHorizonsHeroes::PerkState restored;
	ASSERT_NO_THROW(restored.serialize(wire.iser));
	EXPECT_EQ(restored.rules, published);

	GameSettings settings;
	settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS_PERKS, published);
	CMemorySerializer settingsWire;
	settingsWire.oser.version = settingsWire.iser.version = ESerializationVersion::NEW_HORIZONS_SKELETON_NECROMANCY;
	ASSERT_NO_THROW(settings.serialize(settingsWire.oser));
	GameSettings restoredSettings;
	ASSERT_NO_THROW(restoredSettings.serialize(settingsWire.iser));
	EXPECT_EQ(restoredSettings.getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS), published);
}
