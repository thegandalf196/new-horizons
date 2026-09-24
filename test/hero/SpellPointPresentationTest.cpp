/*
 * SpellPointPresentationTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../client/windows/SpellPointPresentation.h"

TEST(SpellPointPresentationTest, BufferIsIncludedNotAddedTwice)
{
	EXPECT_EQ(spellPointPresentation::readout(310, 460, 50), "310 / 460  {+50}");
	const auto text = spellPointPresentation::tooltip(310, 460, 50);
	EXPECT_NE(text.find("260 Normal + {50 Buffer}"), std::string::npos);
	EXPECT_EQ(text.find('#'), std::string::npos);
	EXPECT_EQ(text.find("360"), std::string::npos);
	EXPECT_NE(text.find("included in your total"), std::string::npos);
}

TEST(SpellPointPresentationTest, EmptyBufferDoesNotShowBonus)
{
	EXPECT_EQ(spellPointPresentation::readout(80, 100, 0), "80 / 100");
	EXPECT_EQ(spellPointPresentation::tooltip(80, 100, 0).find("+0"), std::string::npos);
}

TEST(SpellPointPresentationTest, HiddenMaximumIsNotInvented)
{
	EXPECT_EQ(spellPointPresentation::readout(80, -1, 0), "80");
	EXPECT_EQ(spellPointPresentation::tooltip(80, -1, 0).find("Maximum"), std::string::npos);
}

TEST(SpellPointPresentationTest, LargeTotalsRemainExact)
{
	EXPECT_EQ(spellPointPresentation::readout(4294967294LL, 2147483647, 2147483647),
		"4294967294 / 2147483647  {+2147483647}");
}
