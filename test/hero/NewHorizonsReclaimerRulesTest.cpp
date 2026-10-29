/*
 * Part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../../lib/entities/hero/NewHorizonsReclaimer.h"
#include "../../lib/entities/hero/NewHorizonsNecromancy.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/filesystem/ResourcePath.h"
#include <limits>
namespace
{
JsonNode configured()
{
	JsonNode value(JsonPath::builtin("config/newHorizonsHeroes"));
	value.Struct().erase("ghostSpecialty");
	auto & row = value["reclamationSpecialty"];
	row["version"].Integer() = 1;
	row["conversionPercentPerLevel"].Integer() = 5;
	row["conversionMaximumPercent"].Integer() = 100;
	return value;
}
auto ordinary(int living, int mana = 0, int limit = 100)
{
	return newHorizonsNecromancy::resolve(1, living, 0, false, false, false,
		true, true, mana, limit);
}
int64_t equivalents(const newHorizonsNecromancy::NecromancyResult & result)
{
	return static_cast<int64_t>(result.skeletonsRaised) + 3LL * result.zombiesRaised
		+ 6LL * result.wightsRaised + 12LL * result.boneDragonsRaised;
}
}
TEST(NewHorizonsReclaimerRules, OptionalPolicyRetainsAbsenceAndExplicitZero)
{
	auto value = configured();
	ASSERT_TRUE(newHorizonsHeroes::reclamationSpecialtyRules(value));
	EXPECT_EQ(newHorizonsHeroes::reclamationSpecialtyRules(value)->conversionPercentPerLevel, 5);
	value["reclamationSpecialty"]["conversionPercentPerLevel"].Integer() = 0;
	EXPECT_EQ(newHorizonsHeroes::reclamationSpecialtyRules(value)->conversionPercentPerLevel, 0);
	value.Struct().erase("reclamationSpecialty");
	EXPECT_FALSE(newHorizonsHeroes::reclamationSpecialtyRules(value));
	EXPECT_NO_THROW(newHorizonsHeroes::validateReclamationSpecialtySerialization(value, false));
}
TEST(NewHorizonsReclaimerRules, StrictPolicyRejectsUnsupportedMissingUnknownAndMalformedValues)
{
	EXPECT_THROW(newHorizonsHeroes::validateReclamationSpecialtySerialization(configured(), false), std::runtime_error);
	for(const auto * field : {"version", "conversionPercentPerLevel", "conversionMaximumPercent"})
	{
		auto missing = configured();
		missing["reclamationSpecialty"].Struct().erase(field);
		EXPECT_THROW(newHorizonsHeroes::validateReclamationSpecialtySerialization(missing, true), std::runtime_error);
		auto null = configured();
		null["reclamationSpecialty"][field] = JsonNode();
		EXPECT_THROW(newHorizonsHeroes::validateReclamationSpecialtySerialization(null, true), std::runtime_error);
		auto boolean = configured();
		boolean["reclamationSpecialty"][field].Bool() = true;
		EXPECT_THROW(newHorizonsHeroes::validateReclamationSpecialtySerialization(boolean, true), std::runtime_error);
	}
	auto unknown = configured();
	unknown["reclamationSpecialty"]["freeCorpsePreservation"].Bool() = true;
	EXPECT_THROW(newHorizonsHeroes::validateReclamationSpecialtySerialization(unknown, true), std::runtime_error);
	auto fractional = configured();
	fractional["reclamationSpecialty"]["conversionPercentPerLevel"].Float() = 1.5;
	EXPECT_THROW(newHorizonsHeroes::validateReclamationSpecialtySerialization(fractional, true), std::runtime_error);
	for(const auto invalid : {-1, 101})
	{
		auto maximum = configured();
		maximum["reclamationSpecialty"]["conversionMaximumPercent"].Integer() = invalid;
		EXPECT_THROW(newHorizonsHeroes::validateReclamationSpecialtySerialization(maximum, true), std::runtime_error);
	}
}
TEST(NewHorizonsReclaimerRules, ResidualFloorAndRemaindersConserveSkeletonEquivalents)
{
	const auto base = ordinary(400);
	const auto partial = newHorizonsNecromancy::reclaimResidualSkeletons(base, 25, 0, 100);
	EXPECT_EQ(partial.skeletonsRaised, 31);
	EXPECT_EQ(partial.reclaimerZombiesRaised, 3);
	EXPECT_EQ(partial.zombiesRaised, 3);
	EXPECT_EQ(equivalents(partial), 40);
	EXPECT_FALSE(partial.darkConversionChosen);
	const auto maximum = newHorizonsNecromancy::reclaimResidualSkeletons(ordinary(200), 100, 0, 100);
	EXPECT_EQ(maximum.skeletonsRaised, 2);
	EXPECT_EQ(maximum.zombiesRaised, 6);
	EXPECT_EQ(equivalents(maximum), 20);
}
TEST(NewHorizonsReclaimerRules, OrdinaryCoreEliteAndChampionConversionsRemainFirst)
{
	const auto base = newHorizonsNecromancy::resolve(2, 200, 60, false, false, true,
		true, true, 0, 100, 30, true, true, CreatureID::NONE, {}, {}, true, true);
	ASSERT_EQ(base.boneDragonsRaised, 1);
	ASSERT_EQ(base.zombiesRaised, 4);
	ASSERT_EQ(base.wightsRaised, 1);
	ASSERT_EQ(base.skeletonsRaised, 10);
	const auto enhanced = newHorizonsNecromancy::reclaimResidualSkeletons(base, 100, 0, 100);
	EXPECT_EQ(enhanced.boneDragonsRaised, base.boneDragonsRaised);
	EXPECT_EQ(enhanced.wightsRaised, base.wightsRaised);
	EXPECT_EQ(enhanced.zombiesRaised, 7);
	EXPECT_EQ(enhanced.reclaimerZombiesRaised, 3);
	EXPECT_EQ(enhanced.skeletonsRaised, 1);
	EXPECT_TRUE(enhanced.darkConversionChosen);
	EXPECT_EQ(equivalents(enhanced), base.skeletonsOffered);
}
TEST(NewHorizonsReclaimerRules, ZeroSmallInactiveAndBlockedResultsDoNotGainOutputs)
{
	for(const int living : {0, 10, 20})
	{
		const auto base = ordinary(living);
		const auto enhanced = newHorizonsNecromancy::reclaimResidualSkeletons(base, 100, 0, 100);
		EXPECT_EQ(enhanced.skeletonsRaised, base.skeletonsRaised);
		EXPECT_EQ(enhanced.zombiesRaised, 0);
	}
	const auto zero = newHorizonsNecromancy::reclaimResidualSkeletons(ordinary(400), 0, 0, 100);
	EXPECT_EQ(zero.skeletonsRaised, 40);
	EXPECT_EQ(zero.reclaimerZombiesRaised, 0);
	const auto inactive = newHorizonsNecromancy::resolve(0, 400, 0, false, false, false, true, true, 0, 100);
	EXPECT_FALSE(newHorizonsNecromancy::reclaimResidualSkeletons(inactive, 100, 0, 100).active);
	auto blocked = ordinary(400);
	newHorizonsNecromancy::blockRaisedResult(blocked);
	const auto unchanged = newHorizonsNecromancy::reclaimResidualSkeletons(blocked, 100, 0, 100);
	EXPECT_TRUE(unchanged.blockedByArmyCapacity);
	EXPECT_EQ(unchanged.skeletonsOffered, 40);
	EXPECT_EQ(unchanged.skeletonsRaised + unchanged.zombiesRaised, 0);
}
TEST(NewHorizonsReclaimerRules, BlackHarvestAndSingleOutputIdentityUseFinalHeadcount)
{
	const auto base = ordinary(120);
	ASSERT_EQ(base.manaRecovered, 1);
	const auto enhanced = newHorizonsNecromancy::reclaimResidualSkeletons(base, 100, 0, 100);
	EXPECT_EQ(enhanced.skeletonsRaised, 0);
	EXPECT_EQ(enhanced.zombiesRaised, 4);
	EXPECT_EQ(enhanced.raisedCreature, CreatureID(CreatureID::decode("core:zombie")));
	EXPECT_EQ(enhanced.manaRecovered, 0);
	EXPECT_FALSE(enhanced.darkConversionChosen);
	EXPECT_EQ(base.manaRecovered, 1); // Ordinary fallback still owns its original derivation.
}
TEST(NewHorizonsReclaimerRules, SkeletonUpgradeRetainsOnlySurvivingSkeletonOutput)
{
	const auto upgrade = CreatureID(CreatureID::decode("core:skeletonWarrior"));
	auto base = ordinary(130);
	base.skeletonCreature = upgrade;
	const auto remainder = newHorizonsNecromancy::reclaimResidualSkeletons(base, 100, 0, 100);
	EXPECT_EQ(remainder.skeletonCreature, upgrade);
	EXPECT_EQ(remainder.skeletonsRaised, 1);
	base = ordinary(120);
	base.skeletonCreature = upgrade;
	const auto pureZombie = newHorizonsNecromancy::reclaimResidualSkeletons(base, 100, 0, 100);
	EXPECT_EQ(pureZombie.skeletonCreature, CreatureID::NONE);
}
TEST(NewHorizonsReclaimerRules, WideBudgetArithmeticRetainsMaximumCountAndRejectsRepeatedConversion)
{
	const auto base = newHorizonsNecromancy::resolve(3, std::numeric_limits<int32_t>::max(), 0,
		false, false, false, true, true, 0, 100, 0, false, true, CreatureID::NONE, {}, {},
		false, false, true, 100);
	const auto enhanced = newHorizonsNecromancy::reclaimResidualSkeletons(base, 100, 0, 100);
	EXPECT_EQ(equivalents(enhanced), base.skeletonsOffered);
	EXPECT_TRUE(enhanced.isReclaimerSummaryValid());
	EXPECT_THROW(newHorizonsNecromancy::reclaimResidualSkeletons(enhanced, 100, 0, 100), std::invalid_argument);
	EXPECT_THROW(newHorizonsNecromancy::reclaimResidualSkeletons(base, 101, 0, 100), std::invalid_argument);
}
TEST(NewHorizonsReclaimerRules, TypedSubsetValidityKeepsLegacyZeroAndRejectsInventedSources)
{
	auto legacy = ordinary(120);
	EXPECT_TRUE(legacy.isReclaimerSummaryValid());
	legacy.reclaimerZombiesRaised = -1;
	EXPECT_FALSE(legacy.isReclaimerSummaryValid());
	auto enhanced = newHorizonsNecromancy::reclaimResidualSkeletons(ordinary(120), 100, 0, 100);
	ASSERT_TRUE(enhanced.isReclaimerSummaryValid());
	enhanced.reclaimerZombiesRaised = enhanced.zombiesRaised + 1;
	EXPECT_FALSE(enhanced.isReclaimerSummaryValid());
	enhanced.reclaimerZombiesRaised = enhanced.zombiesRaised;
	enhanced.darkConversionChosen = true;
	EXPECT_FALSE(enhanced.isReclaimerSummaryValid());
	enhanced.darkConversionChosen = false;
	enhanced.blockedByArmyCapacity = true;
	EXPECT_FALSE(enhanced.isReclaimerSummaryValid());
}

TEST(NewHorizonsReclaimerRules, MalformedOrdinaryBatchCannotOverflowOrInventConvertedValue)
{
	auto invalid = ordinary(400);
	invalid.zombiesRaised = std::numeric_limits<int32_t>::max();
	EXPECT_THROW(newHorizonsNecromancy::reclaimResidualSkeletons(invalid, 100, 0, 100), std::invalid_argument);
	invalid = ordinary(400);
	invalid.skeletonsOffered -= 1;
	EXPECT_THROW(newHorizonsNecromancy::reclaimResidualSkeletons(invalid, 100, 0, 100), std::invalid_argument);
	auto enhanced = newHorizonsNecromancy::reclaimResidualSkeletons(ordinary(400), 100, 0, 100);
	newHorizonsNecromancy::blockRaisedResult(enhanced);
	EXPECT_EQ(enhanced.skeletonsOffered, 40);
	EXPECT_EQ(enhanced.skeletonsRaised + enhanced.zombiesRaised + enhanced.reclaimerZombiesRaised, 0);
	EXPECT_EQ(enhanced.manaRecovered, 0);
	EXPECT_EQ(enhanced.raisedCreature, CreatureID::NONE);
	EXPECT_FALSE(enhanced.applied);
}
