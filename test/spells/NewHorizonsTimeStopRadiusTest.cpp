/*
 * NewHorizonsTimeStopRadiusTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/spells/NewHorizonsSorcery.h"

#include <array>
#include <limits>

TEST(NewHorizonsTimeStopRadiusTest, ExactBasisPointsPreserveBothRadiusThresholds)
{
	struct Case { int32_t power; int32_t coefficient; int32_t normal; int32_t chronomancer; };
	const std::array cases{
		Case{0, 10000, 1, 1}, Case{99, 10000, 1, 1}, Case{100, 10000, 2, 2},
		Case{199, 10000, 2, 2}, Case{200, 10000, 2, 3},
		Case{68, 14500, 1, 1}, Case{69, 14500, 2, 2},
		Case{137, 14500, 2, 2}, Case{138, 14500, 2, 3},
		Case{136, 14599, 2, 2}, Case{137, 14599, 2, 3}};
	for(const auto & row : cases)
	{
		SCOPED_TRACE(::testing::Message() << row.power << '/' << row.coefficient);
		EXPECT_EQ(newHorizonsSorcery::timeStopRadius(row.power, row.coefficient, 0, 0, false), row.normal);
		EXPECT_EQ(newHorizonsSorcery::timeStopRadius(row.power, row.coefficient, 0, 0, true), row.chronomancer);
	}
}

TEST(NewHorizonsTimeStopRadiusTest, WarcastingAndEmpowerShareTheSingleFinalFloor)
{
	// 122% x 125% x 133% yields a radius step at power50 and the next at99.
	// Flooring intermediate components would lose the first boundary.
	EXPECT_EQ(newHorizonsSorcery::timeStopRadius(49, 12200, 25, 33, true), 1);
	EXPECT_EQ(newHorizonsSorcery::timeStopRadius(50, 12200, 25, 33, true), 2);
	EXPECT_EQ(newHorizonsSorcery::timeStopRadius(98, 12200, 25, 33, true), 2);
	EXPECT_EQ(newHorizonsSorcery::timeStopRadius(99, 12200, 25, 33, true), 3);
	EXPECT_EQ(newHorizonsSorcery::timeStopRadius(99, 12200, 25, 33, false), 2);
}

TEST(NewHorizonsTimeStopRadiusTest, ZeroInputsAndLargeRatingsRespectUnscaledBaseAndCaps)
{
	const auto maximumPower = std::numeric_limits<int32_t>::max();
	EXPECT_EQ(newHorizonsSorcery::timeStopRadius(0, 100000, 1000, 1000, true), 1);
	EXPECT_EQ(newHorizonsSorcery::timeStopRadius(maximumPower, 0, 1000, 1000, true), 1);
	EXPECT_EQ(newHorizonsSorcery::timeStopRadius(maximumPower, 100000, 1000, 1000, false), 2);
	EXPECT_EQ(newHorizonsSorcery::timeStopRadius(maximumPower, 100000, 1000, 1000, true), 3);
}

TEST(NewHorizonsTimeStopRadiusTest, InvalidScalerInputsRemainRejected)
{
	EXPECT_THROW(newHorizonsSorcery::timeStopRadius(-1, 10000, 0, 0, false), std::invalid_argument);
	EXPECT_THROW(newHorizonsSorcery::timeStopRadius(100, -1, 0, 0, false), std::invalid_argument);
	EXPECT_THROW(newHorizonsSorcery::timeStopRadius(100, 100001, 0, 0, false), std::invalid_argument);
	EXPECT_THROW(newHorizonsSorcery::timeStopRadius(100, 10000, -1, 0, false), std::invalid_argument);
	EXPECT_THROW(newHorizonsSorcery::timeStopRadius(100, 10000, 1001, 0, false), std::invalid_argument);
	EXPECT_THROW(newHorizonsSorcery::timeStopRadius(100, 10000, 0, -1, false), std::invalid_argument);
	EXPECT_THROW(newHorizonsSorcery::timeStopRadius(100, 10000, 0, 1001, false), std::invalid_argument);
}

TEST(NewHorizonsTimeStopRadiusTest, WholePercentInputsMatchTheUnchangedLegacyOverloads)
{
	for(const int32_t power : {0, 68, 69, 99, 100, 137, 138, 199, 200, 500})
		for(const int32_t coefficient : {0, 100, 115, 130, 145, 1000})
			for(const int32_t empower : {0, 25, 1000})
				for(const bool chronomancer : {false, true})
					EXPECT_EQ(newHorizonsSorcery::timeStopRadius(power, coefficient * 100, 0, empower, chronomancer),
						newHorizonsSorcery::timeStopRadius(power, chronomancer, coefficient, empower));
}
