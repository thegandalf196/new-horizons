/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../client/windows/SpellSchoolBadgeLayout.h"

using namespace spellSchoolBadge;

TEST(SpellSchoolBadgeLayout, EachNewHorizonsSchoolHasItsOwnMedallion)
{
	for(const auto key : SCHOOLS)
	{
		const std::string identifier = "new-horizons:" + std::string(key);
		ASSERT_TRUE(schoolKey(identifier).has_value()) << identifier;
		EXPECT_EQ(*schoolKey(identifier), key);
		EXPECT_EQ(*schoolKey(key), key);
	}
}

TEST(SpellSchoolBadgeLayout, OriginalElementsAndUnknownSchoolsHaveNoMedallion)
{
	for(const std::string identifier : {"air", "fire", "water", "earth", "core:air", "any", "new-horizons:arcane", "", ":"})
		EXPECT_FALSE(schoolKey(identifier).has_value()) << identifier;
}

TEST(SpellSchoolBadgeLayout, OneMedallionIsCentredOnTheBottomEdge)
{
	const auto origins = medallionOrigins(1);
	ASSERT_EQ(origins.size(), 1u);
	EXPECT_EQ(origins[0], Point(32, 52));
	// Its centre sits on the scroll's bottom edge, at most one pixel off the scroll's centre.
	EXPECT_EQ(origins[0].y + MEDALLION / 2, SCROLL_HEIGHT);
	EXPECT_LE(std::abs(2 * origins[0].x + MEDALLION - SCROLL_WIDTH), 1);
}

TEST(SpellSchoolBadgeLayout, TwoSchoolsSitSideBySideWithinTheScroll)
{
	const auto origins = medallionOrigins(2);
	ASSERT_EQ(origins.size(), 2u);
	EXPECT_EQ(origins[0], Point(22, 52));
	EXPECT_EQ(origins[1], Point(42, 52));
	EXPECT_EQ(origins[1].x - origins[0].x, MEDALLION + GAP);
	EXPECT_GE(origins[0].x, 0);
	EXPECT_LE(origins[1].x + MEDALLION, SCROLL_WIDTH);
}

TEST(SpellSchoolBadgeLayout, NoOrTooManySchoolsGetNoBadge)
{
	EXPECT_TRUE(medallionOrigins(0).empty());
	EXPECT_TRUE(medallionOrigins(MAX_SCHOOLS + 1).empty());
}

TEST(SpellSchoolBadgeLayout, OverhangFitsUnderEveryMageGuildScroll)
{
	// Every faction's Mage Guild leaves at least 14 px under each scroll.
	EXPECT_EQ(OVERHANG, 9);
	EXPECT_LE(OVERHANG, 14);
}
