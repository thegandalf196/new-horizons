/*
 * NewHorizonsSwampSeerSerializationTest.cpp, part of VCMI / New Horizons
 * Authors: listed in file AUTHORS in main folder
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
JsonNode capturedTiva()
{
	JsonNode result;
	auto & row = result["swampSeerSpecialty"];
	row["version"].Integer() = 1;
	row["learningPercentPerLevel"].Integer() = 0;
	row["learningMaximumPercent"].Integer() = 0;
	row["studyExperiencePerMagicPrimaryPoint"].Integer() = 0;
	return result;
}
SpecialtySerializationContext context(Version version)
{
	CMemorySerializer wire;
	wire.oser.version = version;
	return SpecialtySerializationContext::fromHandler(wire.oser);
}
std::vector<std::byte> encode(GameSettings & settings)
{
	CMemorySerializer wire;
	settings.serialize(wire.oser);
	return wire.extractBuffer();
}
}
TEST(NewHorizonsSwampSeerSerialization, SharesExistingRemainingCapabilityAndRejectsAnyPreviousPresence)
{
	for(const auto epoch : {Version::NEW_HORIZONS_ADDITIONAL_SPECIALTIES,
		Version::NEW_HORIZONS_REPLAY_PAYLOAD_VERSIONS, Version::NEW_HORIZONS_RECOVERY_SPECIALTIES})
	{
		auto rules = capturedTiva();
		EXPECT_THROW(validateCapturedSpecialtiesSerialization(rules, context(epoch)), std::runtime_error);
		rules["swampSeerSpecialty"] = JsonNode();
		EXPECT_THROW(validateCapturedSpecialtiesSerialization(rules, context(epoch)), std::runtime_error);
	}
	EXPECT_NO_THROW(validateCapturedSpecialtiesSerialization(capturedTiva(), context(Version::CURRENT)));
}
TEST(NewHorizonsSwampSeerSerialization, VersionOnlySingletonRejectsWhileVersionOnlyLearningTableRemainsValid)
{
	JsonNode rules;
	rules["learningExchangeSpecialties"]["version"].Integer() = 1;
	EXPECT_NO_THROW(validateCapturedSpecialtiesSerialization(rules, context(Version::CURRENT)));
	rules["swampSeerSpecialty"]["version"].Integer() = 1;
	EXPECT_THROW(validateCapturedSpecialtiesSerialization(rules, context(Version::CURRENT)), std::runtime_error);
}
TEST(NewHorizonsSwampSeerSerialization, CurrentRoundtripKeepsExplicitZeroWithoutInventingLearningCapture)
{
	GameSettings source;
	source.addOverride(EGameSettings::HEROES_NEW_HORIZONS, capturedTiva());
	CMemorySerializer wire(encode(source));
	GameSettings decoded;
	ASSERT_NO_THROW(decoded.serialize(wire.iser));
	EXPECT_EQ(decoded.getValue(EGameSettings::HEROES_NEW_HORIZONS), capturedTiva());
	EXPECT_TRUE(decoded.getValue(EGameSettings::HEROES_NEW_HORIZONS)["learningExchangeSpecialties"].isNull());
}
TEST(NewHorizonsSwampSeerSerialization, OldWriterRefusesBeforeAnySettingsPrefix)
{
	GameSettings settings;
	settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, capturedTiva());
	CMemorySerializer wire;
	wire.oser.version = Version::NEW_HORIZONS_RECOVERY_SPECIALTIES;
	EXPECT_THROW(settings.serialize(wire.oser), std::runtime_error);
	EXPECT_TRUE(wire.extractBuffer().empty());
}
TEST(NewHorizonsSwampSeerSerialization, OldReaderRejectsBeforeReplacingExistingSettings)
{
	GameSettings source;
	source.addOverride(EGameSettings::HEROES_NEW_HORIZONS, capturedTiva());
	CMemorySerializer wire(encode(source));
	wire.iser.version = Version::NEW_HORIZONS_RECOVERY_SPECIALTIES;
	GameSettings decoded;
	JsonNode historical;
	historical["schemaVersion"].Integer() = 1;
	decoded.addOverride(EGameSettings::HEROES_NEW_HORIZONS, historical);
	const auto before = encode(decoded);
	EXPECT_THROW(decoded.serialize(wire.iser), std::runtime_error);
	EXPECT_EQ(encode(decoded), before);
}
TEST(NewHorizonsSwampSeerSerialization, HistoricalAbsentReadClearsReusedCaptureAndResaveDoesNotRecapture)
{
	GameSettings decoded;
	decoded.addOverride(EGameSettings::HEROES_NEW_HORIZONS, capturedTiva());
	GameSettings historical;
	JsonNode absent;
	absent["schemaVersion"].Integer() = 1;
	historical.addOverride(EGameSettings::HEROES_NEW_HORIZONS, absent);
	CMemorySerializer old;
	old.oser.version = old.iser.version = Version::NEW_HORIZONS_RECOVERY_SPECIALTIES;
	ASSERT_NO_THROW(historical.serialize(old.oser));
	ASSERT_NO_THROW(decoded.serialize(old.iser));
	EXPECT_TRUE(decoded.getValue(EGameSettings::HEROES_NEW_HORIZONS)["swampSeerSpecialty"].isNull());
	CMemorySerializer current(encode(decoded));
	GameSettings restored;
	ASSERT_NO_THROW(restored.serialize(current.iser));
	EXPECT_TRUE(restored.getValue(EGameSettings::HEROES_NEW_HORIZONS)["swampSeerSpecialty"].isNull());
}
TEST(NewHorizonsSwampSeerSerialization, MapAndLegacyWrapperUseTheSamePolicyGuard)
{
	CMap map(nullptr);
	map.overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, capturedTiva());
	EXPECT_THROW(map.validateNewHorizonsSpecialtySerialization(context(Version::NEW_HORIZONS_RECOVERY_SPECIALTIES)), std::runtime_error);
	EXPECT_THROW(map.validateNewHorizonsIndividualSpecialtiesSerialization(true, true, true, true, true), std::runtime_error);
	EXPECT_NO_THROW(map.validateNewHorizonsSpecialtySerialization(context(Version::CURRENT)));
}
