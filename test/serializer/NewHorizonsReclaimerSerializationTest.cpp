/*
 * Part of VCMI / New Horizons. Authors: listed in AUTHORS.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../lib/StartInfo.h"
#include "../hero/NewHorizonsHeroRulesFixture.h"
#include "../../lib/entities/hero/NewHorizonsNecromancy.h"
#include "../../lib/entities/hero/NewHorizonsReclaimer.h"
#include "../../lib/GameSettings.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <limits>
namespace
{
using Version = ESerializationVersion;
using newHorizonsNecromancy::NecromancyResult;
NecromancyResult result()
{
	NecromancyResult value;
	value.active = value.applied = value.darkConversionChosen = true;
	value.skeletonsOffered = 12;
	value.skeletonsRaised = value.zombiesRaised = 3;
	value.reclaimerZombiesRaised = 2;
	return value;
}
JsonNode policy()
{
	auto rules = testHeroRules();
	auto & row = rules["reclamationSpecialty"];
	row["version"].Integer() = 1;
	row["conversionPercentPerLevel"].Integer() = 0;
	row["conversionMaximumPercent"].Integer() = 0;
	return rules;
}
std::vector<std::byte> encode(NecromancyResult value, Version version = Version::CURRENT)
{
	CMemorySerializer wire;
	wire.oser.version = version;
	value.serialize(wire.oser);
	return wire.extractBuffer();
}
void rejectsBeforePrefixes(NecromancyResult value, Version version)
{
	CMemorySerializer direct, enclosing;
	direct.oser.version = enclosing.oser.version = version;
	EXPECT_THROW(value.serialize(direct.oser), std::runtime_error);
	EXPECT_TRUE(direct.extractBuffer().empty());
	BattleResultsApplied packet;
	packet.battleID = BattleID(0);
	packet.necromancy = value;
	EXPECT_THROW(packet.serialize(enclosing.oser), std::runtime_error);
	EXPECT_TRUE(enclosing.extractBuffer().empty());
}
}
TEST(NewHorizonsReclaimerSerialization, CurrentResultRoundtripAndCopyRetainTypedSubset)
{
	const auto source = result();
	const auto expected = encode(source);
	CMemorySerializer wire(expected);
	NecromancyResult decoded;
	ASSERT_NO_THROW(decoded.serialize(wire.iser));
	EXPECT_EQ(decoded.reclaimerZombiesRaised, 2);
	EXPECT_EQ(encode(decoded), expected);
	auto sibling = decoded;
	decoded.reclaimerZombiesRaised = 1;
	EXPECT_EQ(sibling.reclaimerZombiesRaised, 2);
}
TEST(NewHorizonsReclaimerSerialization, OldDirectAndBattleResultsAppliedWritersRejectBeforeAnyPrefix)
{
	for(const auto epoch : {Version::NEW_HORIZONS_ADDITIONAL_SPECIALTIES,
		Version::NEW_HORIZONS_REPLAY_PAYLOAD_VERSIONS, Version::NEW_HORIZONS_RECOVERY_SPECIALTIES})
		rejectsBeforePrefixes(result(), epoch);
}
TEST(NewHorizonsReclaimerSerialization, HistoricalReadResetsReusedSubsetWithoutChangingLegacySummary)
{
	auto historical = result();
	historical.reclaimerZombiesRaised = 0;
	const auto bytes = encode(historical, Version::NEW_HORIZONS_RECOVERY_SPECIALTIES);
	CMemorySerializer old(bytes);
	old.iser.version = Version::NEW_HORIZONS_RECOVERY_SPECIALTIES;
	auto reused = result();
	ASSERT_NO_THROW(reused.serialize(old.iser));
	EXPECT_EQ(reused.reclaimerZombiesRaised, 0);
	EXPECT_EQ(encode(reused, Version::NEW_HORIZONS_RECOVERY_SPECIALTIES), bytes);
}
TEST(NewHorizonsReclaimerSerialization, ZeroSubsetDoesNotRetightenHistoricalSummaryValidity)
{
	NecromancyResult historical;
	historical.eligibleCasualties = -1; // Previously accepted, outside new positive-subset contract.
	EXPECT_TRUE(historical.isReclaimerSummaryValid());
	EXPECT_NO_THROW(encode(historical, Version::NEW_HORIZONS_RECOVERY_SPECIALTIES));
	EXPECT_NO_THROW(encode(historical));
}
TEST(NewHorizonsReclaimerSerialization, InvalidNegativeOverflowConservationAndFlagsRejectBothPrefixes)
{
	for(const int kind : {0,1,2,3,4,5,6,7})
	{
		auto bad = result();
		switch(kind)
		{
		case 0: bad.reclaimerZombiesRaised = -1; break;
		case 1: bad.reclaimerZombiesRaised = 4; break;
		case 2: ++bad.skeletonsOffered; break;
		case 3: bad.active = false; break;
		case 4: bad.applied = false; break;
		case 5: bad.blockedByArmyCapacity = true; break;
		case 6: bad.darkConversionChosen = false; break;
		case 7: bad.wightsRaised = std::numeric_limits<int32_t>::max(); break;
		}
		rejectsBeforePrefixes(bad, Version::CURRENT);
	}
}
TEST(NewHorizonsReclaimerSerialization, WideNearMaximumConservationIsValidWithoutNarrowing)
{
	auto value = result();
	value.skeletonsOffered = std::numeric_limits<int32_t>::max();
	value.zombiesRaised = value.skeletonsOffered / 3;
	value.skeletonsRaised = value.skeletonsOffered % 3;
	value.reclaimerZombiesRaised = 1;
	EXPECT_TRUE(value.isReclaimerSummaryValid());
	EXPECT_NO_THROW(encode(value));
}
TEST(NewHorizonsReclaimerSerialization, InvalidCurrentSuffixRejectsUsingActualVariableLengthEncoding)
{
	auto zero = result();
	zero.reclaimerZombiesRaised = 0;
	auto bytes = encode(zero);
	CMemorySerializer encodedZero, encodedNegative;
	int32_t empty = 0, negative = -1;
	encodedZero.oser & empty;
	encodedNegative.oser & negative;
	const auto zeroSuffix = encodedZero.extractBuffer();
	ASSERT_GE(bytes.size(), zeroSuffix.size());
	ASSERT_TRUE(std::equal(zeroSuffix.rbegin(), zeroSuffix.rend(), bytes.rbegin()));
	bytes.resize(bytes.size() - zeroSuffix.size());
	const auto badSuffix = encodedNegative.extractBuffer();
	bytes.insert(bytes.end(), badSuffix.begin(), badSuffix.end());
	CMemorySerializer malformed(bytes);
	NecromancyResult decoded;
	EXPECT_THROW(decoded.serialize(malformed.iser), std::runtime_error);
}
TEST(NewHorizonsReclaimerSerialization, CurrentBattleResultsAppliedRoundtripRetainsSubset)
{
	BattleResultsApplied source;
	source.battleID = BattleID(0);
	source.necromancy = result();
	CMemorySerializer wire;
	ASSERT_NO_THROW(source.serialize(wire.oser));
	BattleResultsApplied decoded;
	ASSERT_NO_THROW(decoded.serialize(wire.iser));
	EXPECT_EQ(decoded.necromancy.reclaimerZombiesRaised, 2);
	EXPECT_EQ(encode(decoded.necromancy), encode(source.necromancy));
}
TEST(NewHorizonsReclaimerSerialization, GlobalAndResolvedPolicyCapturePreserveZeroAndAbsence)
{
	auto source = policy();
	ASSERT_NO_THROW(newHorizonsHeroes::validateHeroRules(source, true));
	const auto captured = newHorizonsHeroes::resolveHeroRules(source, HeroClassID(0));
	EXPECT_EQ(captured["reclamationSpecialty"], source["reclamationSpecialty"]);
	source["reclamationSpecialty"]["conversionMaximumPercent"].Integer() = 100;
	EXPECT_EQ(captured["reclamationSpecialty"]["conversionMaximumPercent"].Integer(), 0);
	source.Struct().erase("reclamationSpecialty");
	const auto absent = newHorizonsHeroes::resolveHeroRules(source, HeroClassID(0));
	EXPECT_TRUE(absent["reclamationSpecialty"].isNull());
}
TEST(NewHorizonsReclaimerSerialization, PolicySettingsCurrentRoundtripAndOldPrefixRefusal)
{
	GameSettings source;
	source.addOverride(EGameSettings::HEROES_NEW_HORIZONS, policy());
	CMemorySerializer old;
	old.oser.version = Version::NEW_HORIZONS_RECOVERY_SPECIALTIES;
	EXPECT_THROW(source.serialize(old.oser), std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	CMemorySerializer current;
	ASSERT_NO_THROW(source.serialize(current.oser));
	GameSettings decoded;
	ASSERT_NO_THROW(decoded.serialize(current.iser));
	EXPECT_EQ(decoded.getValue(EGameSettings::HEROES_NEW_HORIZONS), policy());
}

TEST(NewHorizonsReclaimerSerialization, HistoricalBattleResultsAppliedReadClearsReusedSubset)
{
	BattleResultsApplied source;
	source.battleID = BattleID(0);
	source.necromancy = result();
	source.necromancy.reclaimerZombiesRaised = 0;
	CMemorySerializer old;
	old.oser.version = old.iser.version = Version::NEW_HORIZONS_RECOVERY_SPECIALTIES;
	ASSERT_NO_THROW(source.serialize(old.oser));
	BattleResultsApplied reused;
	reused.necromancy = result();
	ASSERT_NO_THROW(reused.serialize(old.iser));
	EXPECT_EQ(reused.necromancy.reclaimerZombiesRaised, 0);
	EXPECT_EQ(encode(reused.necromancy), encode(source.necromancy));
}
