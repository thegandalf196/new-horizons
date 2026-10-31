/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/GameSettings.h"
#include "../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../../lib/entities/hero/NewHorizonsSpecialtySerialization.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/serializer/CMemorySerializer.h"

namespace
{
using Version = ESerializationVersion;
using newHorizonsHeroes::SpecialtySerializationContext;
using newHorizonsHeroes::validateCapturedSpecialtiesSerialization;
constexpr std::array<std::pair<const char *, int>, 4> extensions{{
	{"prismaticScreenReductionPercent", 15}, {"evasiveFlightReductionPercent", 20},
	{"ridersExploitDamagePercent", 20}, {"tidalRenewalHealingHP", 10}}};
constexpr std::array<const char *, 13> previousKeys{{
	"massedSpearsDamagePercent", "packPursuitDamagePercent", "bloodScentDamagePercent",
	"livingBastionDefense", "chillingPresenceReductionPercent", "boneFormationDefense",
	"stoneSentinelReductionPercent", "ogreWarcallerDamagePercent", "undertowRetaliationPercent",
	"wyvernsPreyDamagePercent", "mobPressureDamagePercent", "wingbeatInitiativePercent",
	"measuredVolleyMdrPenetrationPercent"}};
JsonNode captured()
{
	JsonNode rules;
	rules["additionalSpecialties"]["version"].Integer() = 1;
	auto & policy = rules["additionalSpecialties"]["ordinaryCreatureSpecialties"];
	policy["version"].Integer() = 1;
	policy["closeQuartersDrill"].Integer() = 1;
	policy["marshSharpshooter"].Integer() = 1;
	for(const char * key : {"retaliationDamagePercent", "defendDefense", "maximumHealthPercent", "resistanceAuraPoints"})
		policy[key].Integer() = 0;
	return rules;
}
JsonNode & policy(JsonNode & rules)
{
	return rules["additionalSpecialties"]["ordinaryCreatureSpecialties"];
}
SpecialtySerializationContext context(Version version)
{
	CMemorySerializer wire;
	wire.oser.version = version;
	return SpecialtySerializationContext::fromHandler(wire.oser);
}
std::vector<std::byte> encode(GameSettings & settings, Version version)
{
	CMemorySerializer wire;
	wire.oser.version = version;
	settings.serialize(wire.oser);
	return wire.extractBuffer();
}
}

TEST(OrdinaryPolicyExtensionsSerialization, AppendOnlyEpochAndNamedHandlerFlag)
{
	static_assert(static_cast<int>(Version::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES) == 1144);
	static_assert(static_cast<int>(Version::NEW_HORIZONS_ADJACENCY_FILTERS) == 1145);
	static_assert(static_cast<int>(Version::NEW_HORIZONS_ORDINARY_CREATURE_POLICY_EXTENSIONS) == 1146);
	static_assert(Version::CURRENT >= Version::NEW_HORIZONS_ORDINARY_CREATURE_POLICY_EXTENSIONS);
	EXPECT_FALSE(SpecialtySerializationContext{}.ordinaryCreaturePolicyExtensions);
	EXPECT_FALSE(context(Version::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES).ordinaryCreaturePolicyExtensions);
	const auto previous = context(Version::NEW_HORIZONS_ADJACENCY_FILTERS);
	EXPECT_TRUE(previous.adjacencyFilters);
	EXPECT_FALSE(previous.ordinaryCreaturePolicyExtensions);
	EXPECT_TRUE(context(Version::NEW_HORIZONS_ORDINARY_CREATURE_POLICY_EXTENSIONS).ordinaryCreaturePolicyExtensions);
	EXPECT_TRUE(context(Version::CURRENT).ordinaryCreaturePolicyExtensions);
	CMemorySerializer wire;
	wire.iser.version = Version::NEW_HORIZONS_ADJACENCY_FILTERS;
	EXPECT_FALSE(SpecialtySerializationContext::fromHandler(wire.iser).ordinaryCreaturePolicyExtensions);
}

TEST(OrdinaryPolicyExtensionsSerialization, EveryNewKeyIncludingZeroRequires1146WithoutMutation)
{
	for(const auto & [key, value] : extensions)
		for(const int stored : {0, value})
		{
			auto rules = captured();
			policy(rules)[key].Integer() = stored;
			const auto before = rules;
			ASSERT_NO_THROW(validateCapturedSpecialtiesSerialization(rules, context(Version::CURRENT)));
			EXPECT_NO_THROW(newHorizonsHeroes::validateCurrentSpecialtyRules(rules));
			for(const auto epoch : {Version::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES, Version::NEW_HORIZONS_ADJACENCY_FILTERS})
				EXPECT_THROW(validateCapturedSpecialtiesSerialization(rules, context(epoch)), std::runtime_error);
			EXPECT_EQ(rules, before);
		}
}

TEST(OrdinaryPolicyExtensionsSerialization, ExactThirteenPreviousKeysKeepTheir1145Boundary)
{
	ASSERT_NO_THROW(validateCapturedSpecialtiesSerialization(captured(), context(Version::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES)));
	for(const char * key : previousKeys)
	{
		auto rules = captured();
		policy(rules)[key].Integer() = 0;
		EXPECT_THROW(validateCapturedSpecialtiesSerialization(rules, context(Version::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES)), std::runtime_error);
		EXPECT_NO_THROW(validateCapturedSpecialtiesSerialization(rules, context(Version::NEW_HORIZONS_ADJACENCY_FILTERS)));
		EXPECT_NO_THROW(validateCapturedSpecialtiesSerialization(rules, context(Version::CURRENT)));
	}
}

TEST(OrdinaryPolicyExtensionsSerialization, UnknownAndTwentyFourInvalidValuesRemainShapeErrors)
{
	for(const auto & [key, value] : extensions)
		for(int kind = 0; kind < 6; ++kind)
		{
			auto rules = captured();
			policy(rules)[key].Integer() = value;
			ASSERT_NO_THROW(validateCapturedSpecialtiesSerialization(rules, context(Version::CURRENT)));
			auto & bad = policy(rules)[key];
			bad = JsonNode();
			switch(kind)
			{
			case 0: bad.Bool() = false; break;
			case 1: break; // explicit null
			case 2: bad.Integer() = -1; break;
			case 3: bad.Integer() = 101; break;
			case 4: bad.Float() = 1.5; break;
			case 5: bad.String() = "20"; break;
			}
			EXPECT_THROW(validateCapturedSpecialtiesSerialization(rules, context(Version::CURRENT)), std::runtime_error);
		}
	auto unknown = captured();
	policy(unknown)["unreservedFutureOrdinaryKey"].Integer() = 0;
	EXPECT_THROW(validateCapturedSpecialtiesSerialization(unknown, context(Version::CURRENT)), std::runtime_error);
}

TEST(OrdinaryPolicyExtensionsSerialization, SettingsRejectBeforePrefixAndCurrentRoundtripKeepsZero)
{
	for(const auto & [key, value] : extensions)
		for(const int stored : {0, value})
		{
			auto rules = captured();
			policy(rules)[key].Integer() = stored;
			GameSettings source;
			source.addOverride(EGameSettings::HEROES_NEW_HORIZONS, rules);
			CMemorySerializer current(encode(source, Version::CURRENT));
			GameSettings restored;
			ASSERT_NO_THROW(restored.serialize(current.iser));
			EXPECT_EQ(restored.getValue(EGameSettings::HEROES_NEW_HORIZONS), rules);
			for(const auto epoch : {Version::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES, Version::NEW_HORIZONS_ADJACENCY_FILTERS})
			{
				CMemorySerializer old;
				old.oser.version = epoch;
				EXPECT_THROW(source.serialize(old.oser), std::runtime_error);
				EXPECT_TRUE(old.extractBuffer().empty());
			}
		}
}

TEST(OrdinaryPolicyExtensionsSerialization, Mislabeled1145ReadRejectsBeforeInstallingOverrides)
{
	for(const auto & [key, value] : extensions)
	{
		auto rules = captured();
		policy(rules)[key].Integer() = value;
		GameSettings source;
		source.addOverride(EGameSettings::HEROES_NEW_HORIZONS, rules);
		CMemorySerializer wire(encode(source, Version::CURRENT));
		wire.iser.version = Version::NEW_HORIZONS_ADJACENCY_FILTERS;
		GameSettings destination;
		destination.addOverride(EGameSettings::HEROES_NEW_HORIZONS, captured());
		const auto before = encode(destination, Version::CURRENT);
		EXPECT_THROW(destination.serialize(wire.iser), std::runtime_error);
		EXPECT_EQ(encode(destination, Version::CURRENT), before);
	}
}

TEST(OrdinaryPolicyExtensionsSerialization, MissingFourKeysKeep1145AndCurrentRawBytesAndAbsence)
{
	auto rules = captured();
	for(const char * key : previousKeys) policy(rules)[key].Integer() = 0;
	GameSettings source;
	source.addOverride(EGameSettings::HEROES_NEW_HORIZONS, rules);
	const auto oldBytes = encode(source, Version::NEW_HORIZONS_ADJACENCY_FILTERS);
	EXPECT_EQ(encode(source, Version::CURRENT), oldBytes);
	CMemorySerializer wire(oldBytes);
	wire.iser.version = Version::NEW_HORIZONS_ADJACENCY_FILTERS;
	GameSettings restored;
	ASSERT_NO_THROW(restored.serialize(wire.iser));
	EXPECT_EQ(restored.getValue(EGameSettings::HEROES_NEW_HORIZONS), rules);
	for(const auto & [key, value] : extensions)
		EXPECT_FALSE(restored.getValue(EGameSettings::HEROES_NEW_HORIZONS)["additionalSpecialties"]["ordinaryCreatureSpecialties"].Struct().contains(key));
}

TEST(OrdinaryPolicyExtensionsSerialization, FirstSixOnlyRawSettingsRetain1144Bytes)
{
	GameSettings source;
	source.addOverride(EGameSettings::HEROES_NEW_HORIZONS, captured());
	const auto bytes = encode(source, Version::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES);
	EXPECT_EQ(encode(source, Version::NEW_HORIZONS_ADJACENCY_FILTERS), bytes);
	EXPECT_EQ(encode(source, Version::CURRENT), bytes);
}

TEST(OrdinaryPolicyExtensionsSerialization, MapSettingsOnlyPreflightUsesTheNamedContext)
{
	for(const auto & [key, value] : extensions)
	{
		auto rules = captured();
		policy(rules)[key].Integer() = value;
		CMap map(nullptr);
		map.overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		EXPECT_NO_THROW(map.validateNewHorizonsSpecialtySerialization(context(Version::CURRENT)));
		EXPECT_THROW(map.validateNewHorizonsSpecialtySerialization(context(Version::NEW_HORIZONS_ADJACENCY_FILTERS)), std::runtime_error);
	}
}
