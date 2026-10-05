/*
 * NewHorizonsMandateOfHeavenPacketsTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "../../../lib/battle/HeroActionAllowanceState.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"

namespace
{
void useCurrentVersion(CMemorySerializer & serializer)
{
	serializer.oser.version = ESerializationVersion::CURRENT;
	serializer.iser.version = ESerializationVersion::CURRENT;
}

HeroActionAllowanceState allowances(uint8_t completedPairs)
{
	HeroActionAllowanceState result;
	result.currentRound = 2;
	result.divineMandateCompletedPairs = completedPairs;
	return result;
}

constexpr auto previousVersion = ESerializationVersion::NEW_HORIZONS_KNIGHTLY_SEQUENCE;
}

TEST(NewHorizonsMandateOfHeavenPacketsTest, DirectLedgerRoundTripsFourPairsAndRetainsThreePairOlderSnapshots)
{
	const auto currentState = allowances(4);
	CMemorySerializer current;
	useCurrentVersion(current);
	ASSERT_NO_THROW(current.oser & currentState);
	HeroActionAllowanceState restoredCurrent;
	ASSERT_NO_THROW(current.iser & restoredCurrent);
	EXPECT_EQ(restoredCurrent, currentState);

	const auto legacyState = allowances(3);
	CMemorySerializer legacy;
	legacy.oser.version = previousVersion;
	legacy.iser.version = previousVersion;
	ASSERT_NO_THROW(legacy.oser & legacyState);
	HeroActionAllowanceState restoredLegacy;
	ASSERT_NO_THROW(legacy.iser & restoredLegacy);
	EXPECT_EQ(restoredLegacy.divineMandateCompletedPairs, 3);
	EXPECT_EQ(restoredLegacy.currentRound, 2);

	CMemorySerializer unsupportedWriter;
	unsupportedWriter.oser.version = previousVersion;
	EXPECT_THROW(unsupportedWriter.oser & currentState, std::runtime_error);
	EXPECT_TRUE(unsupportedWriter.extractBuffer().empty())
		<< "Older formats reject the fourth completed pair before emitting bytes";

	CMemorySerializer unsupportedReader;
	unsupportedReader.oser.version = ESerializationVersion::CURRENT;
	unsupportedReader.oser & currentState;
	unsupportedReader.iser.version = previousVersion;
	HeroActionAllowanceState rejectedLegacy;
	EXPECT_THROW(unsupportedReader.iser & rejectedLegacy, std::runtime_error)
		<< "A legacy format cannot decode a fourth pair without its feature marker";
}

TEST(NewHorizonsMandateOfHeavenPacketsTest, CountsAboveFourAreRejectedBeforeWriting)
{
	auto invalid = allowances(5);
	CMemorySerializer writer;
	useCurrentVersion(writer);
	EXPECT_THROW(writer.oser & invalid, std::runtime_error);
	EXPECT_TRUE(writer.extractBuffer().empty());
}
