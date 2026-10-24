/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../../lib/GameSettings.h"
#include "../../lib/entities/hero/NewHorizonsSpecialtySerialization.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/serializer/CMemorySerializer.h"

namespace
{
using Version = ESerializationVersion;
using newHorizonsHeroes::SpecialtySerializationContext;
using newHorizonsHeroes::validateCapturedSpecialtiesSerialization;
JsonNode captured(bool withRow = true)
{
	JsonNode result;
	auto & table = result["learningExchangeSpecialties"];
	table["version"].Integer() = 1;
	if(withRow)
	{
		auto & row = table["ecumenicalScholar"];
		row["learningPercentPerLevel"].Integer() = 0;
		row["learningMaximumPercent"].Integer() = 0;
		row["additionalSpellsTaught"].Integer() = 0;
	}
	return result;
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

TEST(NewHorizonsSpecialtySerialization, NewCapabilityIsAppendOnlyAfterRecovery)
{
	static_assert(Version::NEW_HORIZONS_REMAINING_SPECIALTY_RIDERS > Version::NEW_HORIZONS_RECOVERY_SPECIALTIES);
	const auto previous = context(Version::NEW_HORIZONS_RECOVERY_SPECIALTIES);
	EXPECT_TRUE(previous.recovery);
	EXPECT_FALSE(previous.remainingRiders);
	EXPECT_TRUE(context(Version::CURRENT).remainingRiders);
}
TEST(NewHorizonsSpecialtySerialization, VersionOnlyAndZeroCapturesCannotBeLostToPreviousFormats)
{
	for(const auto epoch : {Version::NEW_HORIZONS_ADDITIONAL_SPECIALTIES,
		Version::NEW_HORIZONS_REPLAY_PAYLOAD_VERSIONS, Version::NEW_HORIZONS_RECOVERY_SPECIALTIES})
		for(const bool withRow : {false, true})
		{
			const auto rules = captured(withRow);
			EXPECT_THROW(validateCapturedSpecialtiesSerialization(rules, context(epoch)), std::runtime_error);
			EXPECT_NO_THROW(validateCapturedSpecialtiesSerialization(rules, context(Version::CURRENT)));
		}
}
TEST(NewHorizonsSpecialtySerialization, InvalidPresentCaptureRejectsEvenForCurrentContext)
{
	auto rules = captured();
	rules["learningExchangeSpecialties"]["unknown"].Integer() = 1;
	EXPECT_THROW(validateCapturedSpecialtiesSerialization(rules, context(Version::CURRENT)), std::runtime_error);
}
TEST(NewHorizonsSpecialtySerialization, HistoricalAbsenceIsReadOnlyAndNeverInstalled)
{
	JsonNode absent;
	const auto before = absent;
	EXPECT_NO_THROW(validateCapturedSpecialtiesSerialization(absent, context(Version::NEW_HORIZONS_RECOVERY_SPECIALTIES)));
	EXPECT_NO_THROW(validateCapturedSpecialtiesSerialization(absent, context(Version::CURRENT)));
	EXPECT_EQ(absent, before);
}
TEST(NewHorizonsSpecialtySerialization, SettingsOldWriterRejectsBeforeAnyPrefix)
{
	for(const bool withRow : {false, true})
	{
		GameSettings settings;
		settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, captured(withRow));
		CMemorySerializer wire;
		wire.oser.version = Version::NEW_HORIZONS_RECOVERY_SPECIALTIES;
		EXPECT_THROW(settings.serialize(wire.oser), std::runtime_error);
		EXPECT_TRUE(wire.extractBuffer().empty());
	}
}
TEST(NewHorizonsSpecialtySerialization, CurrentSettingsRoundtripKeepsVersionOnlyAndExplicitZero)
{
	for(const bool withRow : {false, true})
	{
		GameSettings settings;
		settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, captured(withRow));
		const auto bytes = encode(settings, Version::CURRENT);
		CMemorySerializer wire(bytes);
		GameSettings loaded;
		ASSERT_NO_THROW(loaded.serialize(wire.iser));
		EXPECT_EQ(loaded.getValue(EGameSettings::HEROES_NEW_HORIZONS), captured(withRow));
	}
}
TEST(NewHorizonsSpecialtySerialization, PreviousReaderRejectsPresentTableBeforeInstallingOverrides)
{
	GameSettings source;
	source.addOverride(EGameSettings::HEROES_NEW_HORIZONS, captured());
	const auto bytes = encode(source, Version::CURRENT);
	CMemorySerializer wire(bytes);
	wire.iser.version = Version::NEW_HORIZONS_RECOVERY_SPECIALTIES;
	GameSettings loaded;
	JsonNode historical;
	historical["schemaVersion"].Integer() = 1;
	loaded.addOverride(EGameSettings::HEROES_NEW_HORIZONS, historical);
	const auto before = encode(loaded, Version::CURRENT);
	EXPECT_THROW(loaded.serialize(wire.iser), std::runtime_error);
	EXPECT_EQ(encode(loaded, Version::CURRENT), before);
}
TEST(NewHorizonsSpecialtySerialization, AbsentTableHasIdenticalPreviousAndCurrentWireBytes)
{
	GameSettings settings;
	JsonNode historical;
	historical["schemaVersion"].Integer() = 1;
	settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, historical);
	const auto previous = encode(settings, Version::NEW_HORIZONS_RECOVERY_SPECIALTIES);
	EXPECT_EQ(encode(settings, Version::CURRENT), previous);
	CMemorySerializer wire(previous);
	wire.iser.version = Version::NEW_HORIZONS_RECOVERY_SPECIALTIES;
	GameSettings loaded;
	EXPECT_NO_THROW(loaded.serialize(wire.iser));
	EXPECT_EQ(loaded.getValue(EGameSettings::HEROES_NEW_HORIZONS),
		settings.getValue(EGameSettings::HEROES_NEW_HORIZONS));
}
TEST(NewHorizonsSpecialtySerialization, MapPreflightChecksOverridesAndLegacyWrapperFailsClosed)
{
	CMap map(nullptr);
	map.overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, captured());
	EXPECT_THROW(map.validateNewHorizonsSpecialtySerialization(context(Version::NEW_HORIZONS_RECOVERY_SPECIALTIES)), std::runtime_error);
	EXPECT_NO_THROW(map.validateNewHorizonsSpecialtySerialization(context(Version::CURRENT)));
	EXPECT_THROW(map.validateNewHorizonsIndividualSpecialtiesSerialization(true, true, true, true, true), std::runtime_error);
}
