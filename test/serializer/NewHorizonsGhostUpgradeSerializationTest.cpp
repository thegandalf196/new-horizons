/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../../lib/GameSettings.h"
#include "../../lib/entities/hero/NewHorizonsGhostSpecialty.h"
#include "../../lib/entities/hero/NewHorizonsSpecialtySerialization.h"
#include "../../lib/serializer/CMemorySerializer.h"
namespace
{
JsonNode ghostRules()
{
	JsonNode rules;
	rules["ghostSpecialty"]["version"].Integer() = 1;
	return rules;
}
}
TEST(NewHorizonsGhostUpgradeSerialization, CurrentSettingsRoundTripPreservesCapture)
{
	GameSettings settings;
	const auto rules = ghostRules();
	settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, rules);
	CMemorySerializer wire;
	settings.serialize(wire.oser);
	GameSettings restored;
	restored.serialize(wire.iser);
	EXPECT_EQ(restored.getValue(EGameSettings::HEROES_NEW_HORIZONS), rules);
}
TEST(NewHorizonsGhostUpgradeSerialization, OldWriterRejectsBeforeSettingsPrefix)
{
	static_assert(ESerializationVersion::NEW_HORIZONS_GHOST_UPGRADE > ESerializationVersion::NEW_HORIZONS_SERPENTS_EMBRACE);
	GameSettings settings;
	settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, ghostRules());
	CMemorySerializer wire;
	wire.oser.version = ESerializationVersion::NEW_HORIZONS_SERPENTS_EMBRACE;
	EXPECT_THROW(settings.serialize(wire.oser), std::runtime_error);
	EXPECT_TRUE(wire.extractBuffer().empty());
}
TEST(NewHorizonsGhostUpgradeSerialization, AbsenceIsValidAndNeverFilledFromDefaults)
{
	JsonNode old;
	const auto before = old;
	EXPECT_NO_THROW(newHorizonsHeroes::validateGhostSpecialtySerialization(old, false));
	EXPECT_EQ(old, before);
	EXPECT_FALSE(newHorizonsHeroes::usesGhostUpgrade(old));
}
TEST(NewHorizonsGhostUpgradeSerialization, RejectsMixedIdentityAndMalformedVersion)
{
	for(const int bad : {-1, 0, 2})
	{
		auto rules = ghostRules();
		rules["ghostSpecialty"]["version"].Integer() = bad;
		EXPECT_THROW(newHorizonsHeroes::validateGhostSpecialtySerialization(rules, true), std::runtime_error);
	}
	auto mixed = ghostRules();
	mixed["reclamationSpecialty"]["version"].Integer() = 1;
	EXPECT_THROW(newHorizonsHeroes::validateGhostSpecialtySerialization(mixed, true), std::runtime_error);
	auto unknown = ghostRules();
	unknown["ghostSpecialty"]["perCreatureKilled"].Bool() = true;
	EXPECT_THROW(newHorizonsHeroes::validateGhostSpecialtySerialization(unknown, true), std::runtime_error);
}
TEST(NewHorizonsGhostUpgradeSerialization, HistoricalReclaimerRoundTripDoesNotAcquireUpgradePolicy)
{
	GameSettings settings;
	JsonNode historical;
	historical["reclamationSpecialty"]["version"].Integer() = 1;
	historical["reclamationSpecialty"]["conversionPercentPerLevel"].Integer() = 5;
	historical["reclamationSpecialty"]["conversionMaximumPercent"].Integer() = 100;
	settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, historical);
	CMemorySerializer wire;
	wire.oser.version = wire.iser.version = ESerializationVersion::NEW_HORIZONS_SERPENTS_EMBRACE;
	settings.serialize(wire.oser);
	GameSettings loaded;
	loaded.serialize(wire.iser);
	EXPECT_EQ(loaded.getValue(EGameSettings::HEROES_NEW_HORIZONS), historical);
	EXPECT_TRUE(loaded.getValue(EGameSettings::HEROES_NEW_HORIZONS)["ghostSpecialty"].isNull());
}
