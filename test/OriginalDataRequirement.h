/*
 * OriginalDataRequirement.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <gtest/gtest.h>

/// Whether the test bootstrap found the original Heroes III data. Without it the
/// game library is not initialized, and tests that need it are skipped.
namespace vcmiTestData
{
inline bool & originalDataMissing()
{
	static bool missing = false;
	return missing;
}

inline constexpr const char * MISSING_ORIGINAL_DATA =
	"needs the original Heroes III data, which this test profile does not have";
}

/// Skip the current test, or every test of a suite when used in SetUpTestSuite().
#define VCMI_TEST_REQUIRE_ORIGINAL_DATA() \
	do \
	{ \
		if(vcmiTestData::originalDataMissing()) \
			GTEST_SKIP() << vcmiTestData::MISSING_ORIGINAL_DATA; \
	} while(false)

namespace vcmiTestData
{
/// Fixture for plain test suites that need the original data.
class OriginalDataTest : public ::testing::Test
{
public:
	static void SetUpTestSuite() { VCMI_TEST_REQUIRE_ORIGINAL_DATA(); }
};
}
