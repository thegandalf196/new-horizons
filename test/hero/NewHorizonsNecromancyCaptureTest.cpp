/*
 * NewHorizonsNecromancyCaptureTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../../lib/GameSettings.h"
#include "../../lib/entities/hero/NewHorizonsNecromancyPolicy.h"
#include "../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../lib/gameState/CGameState.h"
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
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_REVISED_ORDERS;
	EXPECT_THROW(value.serialize(old.oser), std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
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
	current[newHorizonsNecromancy::POLICY_KEY]["version"].Integer() = 2;
	EXPECT_THROW(newHorizonsHeroes::validatePerkRules(current), std::runtime_error);
	const auto historical = capturedNecromancy(false);
	EXPECT_FALSE(newHorizonsNecromancy::policy(historical));
	EXPECT_NO_THROW(newHorizonsNecromancy::validatePolicySerialization(historical, false));
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

	CMemorySerializer forged;
	forged.iser.version = ESerializationVersion::NEW_HORIZONS_REVISED_ORDERS;
	forged.oser & current.rules;
	EXPECT_THROW(restored.serialize(forged.iser), std::runtime_error);
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
	CMemorySerializer forged;
	forged.iser.version = ESerializationVersion::NEW_HORIZONS_REVISED_ORDERS;
	forged.oser & raw;
	EXPECT_THROW(target.serialize(forged.iser), std::runtime_error);
	EXPECT_EQ(target.getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS), historical);
	CMemorySerializer old;
	old.oser.version = old.iser.version = ESerializationVersion::NEW_HORIZONS_REVISED_ORDERS;
	ASSERT_NO_THROW(target.serialize(old.oser));
	ASSERT_NO_THROW(decoded.serialize(old.iser));
	EXPECT_EQ(decoded.getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS), historical);
}

TEST(NewHorizonsNecromancyCapture, HeroAndEnclosingMapWritersRefuseBeforeTheirPrefixes)
{
	auto hero = std::make_shared<CGHeroInstance>(nullptr);
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
	map.heroesPool.push_back(hero);
	rejectsBeforePrefix(map);
	map.heroesPool.clear();
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
