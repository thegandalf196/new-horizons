/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../OverkillRandomPrayerRules.h"
#include "../PerkStateFixture.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../lib/GameSettings.h"
#include "../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <functional>
#include <string_view>

namespace
{
JsonNode passiveRules()
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsPerks"));
	// These epoch-1155 fixtures explicitly retain historical marks-based capture.
	for(const auto * key : {"metamagicPolicy", "divineMandatePolicy", "demonicGatingPolicy", "bloodragePolicy"})
	{
		rules[key] = JsonNode(JsonMap{});
		rules[key]["version"].Integer() = 2;
	}
	rules.setOverrideFlag(true);
	return rules;
}
auto settingsBytes(GameSettings & settings)
{
	CMemorySerializer bytes;
	bytes.oser.version = ESerializationVersion::CURRENT;
	settings.serialize(bytes.oser);
	return bytes.extractBuffer();
}
std::vector<JsonNode> malformedPolicies(bool metamagic = false)
{
	std::vector<JsonNode> result{JsonNode(), JsonNode(JsonMap{})};
	JsonNode value;
	value.Bool() = true;
	result.push_back(value);
	value = JsonNode(); value["version"] = JsonNode(); result.push_back(value);
	value = JsonNode(); value["version"].String() = "2"; result.push_back(value);
	value = JsonNode(); value["version"].Float() = 2.0; result.push_back(value);
	for(const int version : {0, 1, metamagic ? 4 : 3})
	{
		value = JsonNode(); value["version"].Integer() = version; result.push_back(value);
	}
	value = JsonNode(); value["version"].Integer() = 2;
	value["unexpected"].Bool() = true; result.push_back(value);
	return result;
}
template<typename Value> void rejectsOldPrefix(Value & value)
{
	for(const auto epoch : {ESerializationVersion::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES,
		ESerializationVersion::NEW_HORIZONS_OPTIONAL_QUICK_STUDY})
	{
		CMemorySerializer bytes;
		bytes.oser.version = epoch;
		EXPECT_THROW(value.serialize(bytes.oser), std::runtime_error);
		EXPECT_TRUE(bytes.extractBuffer().empty());
	}
}
}

TEST(NewHorizonsPassiveFactionPolicy, PresentMalformedPoliciesRejectWithoutMutation)
{
	for(const auto * key : {"metamagicPolicy", "divineMandatePolicy", "demonicGatingPolicy", "bloodragePolicy"})
		for(const auto & malformed : malformedPolicies(std::string_view(key) == "metamagicPolicy"))
		{
			auto rules = passiveRules(); rules[key] = malformed;
			const auto before = rules;
			EXPECT_THROW(newHorizonsHeroes::validatePerkRules(rules), std::runtime_error) << key;
			newHorizonsHeroes::PerkState state; state.replaceRules(rules);
			EXPECT_THROW(state.validate(), std::runtime_error) << key;
			EXPECT_EQ(rules, before);
		}
}

TEST(NewHorizonsPassiveFactionPolicy, CurrentMetamagicPointsCannotLeakIntoHistoricalMarksCapture)
{
	auto rules = passiveRules();
	rules["metamagicPolicy"] = JsonNode(JsonMap{});
	rules["metamagicPolicy"]["version"].Integer() = 3;
	EXPECT_NO_THROW(newHorizonsHeroes::validatePerkRules(rules));
	rules["metamagicPolicy"]["adeptAdditionalPoints"].Integer() = 1;
	EXPECT_NO_THROW(newHorizonsHeroes::validatePerkRules(rules));
	// Merely relabelling current data as historical must still reject.
	rules["metamagicPolicy"]["version"].Integer() = 2;
	EXPECT_THROW(newHorizonsHeroes::validatePerkRules(rules), std::runtime_error);
}

TEST(NewHorizonsPassiveFactionPolicy, MetamagicOnlyAllowsBoundedIntegerAdeptMarks)
{
	for(const int marks : {0, 5})
	{
		auto rules = passiveRules(); rules["metamagicPolicy"]["adeptAdditionalMarks"].Integer() = marks;
		EXPECT_NO_THROW(newHorizonsHeroes::validatePerkRules(rules));
	}
	for(const int marks : {-1, 6})
	{
		auto rules = passiveRules(); rules["metamagicPolicy"]["adeptAdditionalMarks"].Integer() = marks;
		EXPECT_THROW(newHorizonsHeroes::validatePerkRules(rules), std::runtime_error);
	}
	for(const auto * key : {"metamagicPolicy", "divineMandatePolicy", "demonicGatingPolicy", "bloodragePolicy"})
	{
		auto rules = passiveRules(); rules[key]["adeptAdditionalMarks"].Float() = 2.0;
		EXPECT_THROW(newHorizonsHeroes::validatePerkRules(rules), std::runtime_error);
	}
}

TEST(NewHorizonsPassiveFactionPolicy, SettingsAdmissionRejectsBeforeChangingOtherOverrides)
{
	GameSettings settings;
	const auto valid = passiveRules();
	settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS_PERKS, valid);
	for(const auto & malformed : malformedPolicies())
	{
		auto rules = valid; rules["divineMandatePolicy"] = malformed;
		EXPECT_THROW(settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS_PERKS, rules), std::runtime_error);
		JsonNode input; input["heroes"]["newHorizonsPerks"] = rules;
		input["combat"]["moraleExtraDamagePercent"].Integer() = 75;
		const auto before = settingsBytes(settings);
		EXPECT_THROW(settings.loadOverrides(input), std::runtime_error);
		EXPECT_EQ(settingsBytes(settings), before);
		EXPECT_EQ(settings.getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS), valid);
	}
}

TEST(NewHorizonsPassiveFactionPolicy, OldWritersRefuseBeforePrefixAnd1155RoundTrips)
{
	newHorizonsHeroes::PerkState state; state.replaceRules(passiveRules());
	GameSettings settings;
	settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS_PERKS, state.getRules());
	rejectsOldPrefix(state); rejectsOldPrefix(settings);
	CMemorySerializer bytes;
	bytes.oser.version = bytes.iser.version = ESerializationVersion::NEW_HORIZONS_PASSIVE_FACTION_SKILLS;
	ASSERT_NO_THROW(state.serialize(bytes.oser));
	newHorizonsHeroes::PerkState restored;
	ASSERT_NO_THROW(restored.serialize(bytes.iser));
	EXPECT_EQ(restored.getRules(), state.getRules());
	for(const auto * key : {"metamagicPolicy", "divineMandatePolicy", "demonicGatingPolicy", "bloodragePolicy"})
		perkStateFixture::editRules(state, [&](JsonNode & rules) { rules.Struct().erase(key); });
	EXPECT_NO_THROW(newHorizonsHeroes::validateWeightedPerkSerialization(state.getRules(), true, true, true, true, false));
}

TEST(NewHorizonsPassiveFactionPolicy, ForgedOldReaderPreservesExistingSettingsAndPerkState)
{
	auto historical = passiveRules();
	for(const auto * key : {"metamagicPolicy", "divineMandatePolicy", "demonicGatingPolicy", "bloodragePolicy"})
		historical.Struct().erase(key);
	for(const auto epoch : {ESerializationVersion::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES,
		ESerializationVersion::NEW_HORIZONS_OPTIONAL_QUICK_STUDY})
	{
		newHorizonsHeroes::PerkState state; state.replaceRules(historical);
		CMemorySerializer forgedState; forgedState.iser.version = epoch;
		auto rules = passiveRules(); forgedState.oser & rules;
		EXPECT_THROW(state.serialize(forgedState.iser), std::runtime_error);
		EXPECT_EQ(state.getRules(), historical);
		GameSettings settings; settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS_PERKS, historical);
		const auto before = settingsBytes(settings);
		JsonNode raw; raw["heroes"]["newHorizonsPerks"] = passiveRules();
		CMemorySerializer forgedSettings; forgedSettings.iser.version = epoch;
		forgedSettings.oser & raw;
		EXPECT_THROW(settings.serialize(forgedSettings.iser), std::runtime_error);
		EXPECT_EQ(settingsBytes(settings), before);
	}
}

TEST(NewHorizonsPassiveFactionPolicy, DoubleCastAndChosenPrayersAdmitBesideTheirCapturedPredecessors)
{
	const JsonNode installed(JsonPath::builtin("config/newHorizonsPerks"));
	ASSERT_EQ(installed["metamagicPolicy"]["version"].Integer(), 7);
	ASSERT_EQ(installed["divineMandatePolicy"]["version"].Integer(), 4);
	EXPECT_NO_THROW(newHorizonsHeroes::validatePassiveFactionPolicies(installed));
	EXPECT_NO_THROW(newHorizonsHeroes::validatePassiveFactionPolicies(overkillRandomPrayerRules()));
	const auto rejects = [&](const char * key, const std::function<void(JsonNode &)> & mutate)
	{
		auto rules = installed;
		mutate(rules[key]);
		EXPECT_THROW(newHorizonsHeroes::validatePassiveFactionPolicies(rules), std::runtime_error)
			<< key << rules[key].toCompactString();
	};
	rejects("metamagicPolicy", [](JsonNode & p) { p.Struct().erase("halonAdditionalUses"); });
	rejects("metamagicPolicy", [](JsonNode & p) { p["removalChancePercent"].Vector() = {JsonNode(0)}; });
	rejects("metamagicPolicy", [](JsonNode & p) { p["usesPerCombatByRank"].Vector()[2].Integer() = 0; });
	rejects("divineMandatePolicy", [](JsonNode & p) { p.Struct().erase("chosen"); });
	rejects("divineMandatePolicy", [](JsonNode & p) { p["chosen"].Bool() = false; });
	rejects("divineMandatePolicy", [](JsonNode & p) { p["chosen"].Integer() = 1; });
	rejects("divineMandatePolicy", [](JsonNode & p) { p["version"].Integer() = 3; });
	rejects("divineMandatePolicy", [](JsonNode & p) { p["version"].Integer() = 5; });
	auto random = overkillRandomPrayerRules();
	random["divineMandatePolicy"]["chosen"].Bool() = true;
	EXPECT_THROW(newHorizonsHeroes::validatePassiveFactionPolicies(random), std::runtime_error);
}
