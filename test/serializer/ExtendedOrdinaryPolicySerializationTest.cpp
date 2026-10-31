/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/entities/hero/NewHorizonsSpecialtySerialization.h"
#include "../../lib/GameSettings.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <array>
namespace
{
using Version = ESerializationVersion;
constexpr auto admitted = Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES;
constexpr auto old = Version::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES;
constexpr std::array<const char *, 17> keys{{"scorchingImpactDamageReductionPercent",
	"cruelOverseerDefenseReduction",
	"lanceFormationDefensePerHex",
	"lanceFormationMaximumDefense",
	"stormWingsDefenseIgnorePercent",
	"stormWingsMinimumFlightDistance",
	"consecratedTargetDefenseIgnorePercent",
	"featheredFeintPhysicalReductionIgnorePercent",
	"demonicSolidarityReductionPercent",
	"ironHideReductionPercent",
	"graveDustHealingReductionBasisPoints",
	"cinderReprisalDamagePercent",
	"emberbrandDamagePercent",
	"toxicStingChancePercent",
	"gnollHarriersDamagePercent",
	"prismaticVanguardPhysicalDamagePercent",
	"siegeSapperCreatureDefenseIgnorePercent"}};
JsonNode captured()
{
	JsonNode rules; rules["additionalSpecialties"]["version"].Integer() = 1;
	auto & p = rules["additionalSpecialties"]["ordinaryCreatureSpecialties"];
	p["version"].Integer() = 1; p["closeQuartersDrill"].Integer() = p["marshSharpshooter"].Integer() = 1;
	for(const auto * key : {"retaliationDamagePercent", "defendDefense", "maximumHealthPercent", "resistanceAuraPoints"})
		p[key].Integer() = 0;
	return rules;
}
JsonNode & policy(JsonNode & rules) { return rules["additionalSpecialties"]["ordinaryCreatureSpecialties"]; }
void insert(JsonNode & rules, const char * key, int value)
{
	auto & p = policy(rules); p[key].Integer() = value;
	const std::string name(key);
	if(name == "lanceFormationDefensePerHex") p["lanceFormationMaximumDefense"].Integer() = 1;
	if(name == "lanceFormationMaximumDefense") p["lanceFormationDefensePerHex"].Integer() = 0;
	if(name == "stormWingsDefenseIgnorePercent") p["stormWingsMinimumFlightDistance"].Integer() = 1;
	if(name == "stormWingsMinimumFlightDistance") p["stormWingsDefenseIgnorePercent"].Integer() = 0;
}
auto context(Version version)
{
	CMemorySerializer wire; wire.oser.version = version;
	return newHorizonsHeroes::SpecialtySerializationContext::fromHandler(wire.oser);
}
void validate(const JsonNode & rules, Version version)
{
	newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(version));
}
std::vector<std::byte> encode(GameSettings & source, Version version)
{
	CMemorySerializer wire; wire.oser.version = version; source.serialize(wire.oser); return wire.extractBuffer();
}
}
TEST(ExtendedOrdinaryPolicySerialization, PresenceIncludingZeroAndCompanionDomainsAreReadOnly)
{
	for(const auto * key : keys)
	{
		const bool positive = std::string(key) == "lanceFormationMaximumDefense" || std::string(key) == "stormWingsMinimumFlightDistance";
		auto rules = captured(); insert(rules, key, positive ? 1 : 0); const auto original = rules;
		EXPECT_NO_THROW(validate(rules, admitted)); EXPECT_THROW(validate(rules, old), std::runtime_error);
		EXPECT_EQ(rules, original);
		auto upper = captured();
		insert(upper, key, std::string(key) == "graveDustHealingReductionBasisPoints" ? 10000 :
			std::string(key) == "stormWingsMinimumFlightDistance" ? 187 : 100);
		EXPECT_NO_THROW(validate(upper, admitted));
		GameSettings settings; settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, rules);
		CMemorySerializer legacy; legacy.oser.version = old;
		EXPECT_THROW(settings.serialize(legacy.oser), std::runtime_error); EXPECT_TRUE(legacy.extractBuffer().empty());
		const auto bytes = encode(settings, admitted); CMemorySerializer reader(bytes); reader.iser.version = admitted;
		GameSettings restored; ASSERT_NO_THROW(restored.serialize(reader.iser));
		EXPECT_EQ(restored.getValue(EGameSettings::HEROES_NEW_HORIZONS), original);
	}
}
TEST(ExtendedOrdinaryPolicySerialization, FrozenKeyDomainsAndPairPresenceRejectMalformedSnapshots)
{
	for(const auto * key : keys)
	{
		for(const int value : {-1, std::string(key) == "graveDustHealingReductionBasisPoints" ? 10001 :
			std::string(key) == "stormWingsMinimumFlightDistance" ? 188 : 101})
		{
			auto rules = captured(); insert(rules, key, value); EXPECT_THROW(validate(rules, admitted), std::runtime_error);
		}
		auto rules = captured(); insert(rules, key, 1); policy(rules)[key].String() = "1";
		EXPECT_THROW(validate(rules, admitted), std::runtime_error);
	}
	for(const auto * key : {"lanceFormationDefensePerHex", "lanceFormationMaximumDefense",
		"stormWingsDefenseIgnorePercent", "stormWingsMinimumFlightDistance"})
	{
		auto rules = captured(); policy(rules)[key].Integer() = 1;
		EXPECT_THROW(validate(rules, admitted), std::runtime_error);
	}
}
TEST(ExtendedOrdinaryPolicySerialization, HistoricalAbsenceAndLegacyBytesStayExactWithoutDefaults)
{
	auto rules = captured(); const auto original = rules;
	EXPECT_NO_THROW(validate(rules, old));
	EXPECT_NO_THROW(validate(rules, admitted)); EXPECT_EQ(rules, original);
	EXPECT_FALSE(newHorizonsHeroes::SpecialtySerializationContext{}.extendedOrdinarySpecialties);
	EXPECT_FALSE(context(old).extendedOrdinarySpecialties); EXPECT_TRUE(context(admitted).extendedOrdinarySpecialties);
	GameSettings source; source.addOverride(EGameSettings::HEROES_NEW_HORIZONS, rules);
	EXPECT_EQ(encode(source, old), encode(source, admitted));
	for(const auto * key : keys) EXPECT_FALSE(policy(rules).Struct().contains(key));
	policy(rules)["unresolvedPyreCoefficient"].Integer() = 0;
	EXPECT_THROW(validate(rules, admitted), std::runtime_error);
}

TEST(ExtendedOrdinaryPolicySerialization, ReusedSettingsReadReplacesFutureSnapshotWithHistoricalAbsence)
{
	GameSettings historical; historical.addOverride(EGameSettings::HEROES_NEW_HORIZONS, captured());
	const auto bytes = encode(historical, old);
	auto future = captured(); insert(future, "graveDustHealingReductionBasisPoints", 0);
	GameSettings reused; reused.addOverride(EGameSettings::HEROES_NEW_HORIZONS, future);
	CMemorySerializer reader(bytes); reader.iser.version = old;
	ASSERT_NO_THROW(reused.serialize(reader.iser));
	EXPECT_EQ(reused.getValue(EGameSettings::HEROES_NEW_HORIZONS), captured());
}
