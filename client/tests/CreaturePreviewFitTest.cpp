/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include <gtest/gtest.h>
#include "../widgets/CreaturePreviewFit.h"
#include "../windows/FortCompactCreatureLayout.h"

namespace
{
void expectRect(const Rect & actual, int x, int y, int width, int height)
{
	EXPECT_EQ(actual.x, x);
	EXPECT_EQ(actual.y, y);
	EXPECT_EQ(actual.w, width);
	EXPECT_EQ(actual.h, height);
}
}

TEST(CreaturePreviewFitTest, WideWingsAndTallCreaturesRetainAspectInsideViewport)
{
	expectRect(creaturePreviewFit::destination(Rect(80, 90, 300, 100), Point(150, 140)), 0, 45, 150, 50);
	expectRect(creaturePreviewFit::destination(Rect(150, 30, 80, 280), Point(150, 140)), 55, 0, 40, 140);
}

TEST(CreaturePreviewFitTest, TransparentCanvasPaddingDoesNotDetermineCreatureScale)
{
	const auto content = creaturePreviewFit::include(Rect(0, 0, 0, 0), Rect(200, 180, 100, 120));
	expectRect(creaturePreviewFit::destination(content, Point(150, 140)), 25, 10, 100, 120);
	// Moving the same silhouette within a much larger source canvas changes no scale.
	expectRect(creaturePreviewFit::destination(Rect(800, 720, 100, 120), Point(150, 140)), 25, 10, 100, 120);
}

TEST(CreaturePreviewFitTest, AllAllowedPosesShareOneStableUnionAndAnchor)
{
	const auto holding = Rect(180, 150, 80, 130);
	const auto wings = Rect(90, 80, 270, 180);
	const auto attack = Rect(150, 120, 240, 160);
	auto bounds = creaturePreviewFit::include(holding, wings);
	bounds = creaturePreviewFit::include(bounds, attack);
	expectRect(bounds, 90, 80, 300, 200);
	expectRect(creaturePreviewFit::include(attack, creaturePreviewFit::include(wings, holding)), 90, 80, 300, 200);
	const auto destination = creaturePreviewFit::destination(bounds, Point(150, 140));
	expectRect(destination, 0, 20, 150, 100);
	// Repeated casts/frames cannot replace the union with current-frame bounds.
	expectRect(creaturePreviewFit::include(bounds, holding), bounds.x, bounds.y, bounds.w, bounds.h);
}

TEST(CreaturePreviewFitTest, LogicalGeometryHasConsistentOneAndTwoTimesScaling)
{
	const Rect logical(90, 80, 300, 200);
	const auto native = creaturePreviewFit::destination(logical, Point(150, 140));
	const auto twice = creaturePreviewFit::destination(logical * 2, Point(300, 280));
	expectRect(twice, native.x * 2, native.y * 2, native.w * 2, native.h * 2);
}

TEST(CreaturePreviewFitTest, CompactViewportFitsFullSilhouetteWithoutEnlargingSmallCreatures)
{
	expectRect(creaturePreviewFit::destination(Rect(80, 90, 300, 200), Point(380, 40)), 160, 0, 60, 40);
	expectRect(creaturePreviewFit::destination(Rect(80, 90, 20, 20), Point(150, 140)), 65, 60, 20, 20);
}

TEST(CreaturePreviewFitTest, MissingFramesAndInvalidViewportsAreSafe)
{
	expectRect(creaturePreviewFit::include(Rect(10, 20, 30, 40), Rect()), 10, 20, 30, 40);
	expectRect(creaturePreviewFit::destination(Rect(), Point(150, 140)), 0, 0, 0, 0);
	expectRect(creaturePreviewFit::destination(Rect(10, 20, 30, 40), Point()), 0, 0, 0, 0);
}

TEST(CreaturePreviewFitTest, CompactConfluxCardKeepsSubstantialPortraitBesideAllSevenNamedStats)
{
	const auto layout = fortRecruitmentPresentation::compactCreatureLayout(253, 107, 11, 103, 54);
	EXPECT_TRUE(layout.showNames);
	EXPECT_EQ(layout.rows, 7);
	EXPECT_EQ(layout.columns, 1);
	EXPECT_GE(layout.portrait.w, 90);
	EXPECT_GE(layout.portrait.h, 84);
	EXPECT_GE(layout.stats.w, 103);
	EXPECT_GE(layout.stats.x - layout.portrait.right(), 6);
	EXPECT_GE(layout.rowStep, 11);
	EXPECT_LE(layout.stats.y + 7 * layout.rowStep + 2, 107 - 2);
}

TEST(CreaturePreviewFitTest, CompactConfluxCardMeasuresTranslatedLabelsAndActualValueWidth)
{
	const auto layout = fortRecruitmentPresentation::compactCreatureLayout(253, 107, 12, 145, 54);
	EXPECT_TRUE(layout.showNames);
	EXPECT_GE(layout.stats.w, 145);
	EXPECT_GE(layout.portrait.w, 90);
	EXPECT_GE(layout.rowStep, 12);
	EXPECT_LE(layout.stats.right(), 249);
	EXPECT_LE(layout.stats.y + 7 * layout.rowStep + 2, 105);
	const auto boundary = fortRecruitmentPresentation::compactCreatureLayout(253, 107, 11, 149, 54);
	EXPECT_TRUE(boundary.showNames);
	EXPECT_EQ(boundary.portrait.w, 90);
	const auto tooWide = fortRecruitmentPresentation::compactCreatureLayout(253, 107, 11, 150, 54);
	EXPECT_FALSE(tooWide.showNames);
	EXPECT_GE(tooWide.portrait.w, 90);
	const auto overwide = fortRecruitmentPresentation::compactCreatureLayout(253, 107, 11, 239, 54);
	EXPECT_FALSE(overwide.showNames);
	EXPECT_GE(overwide.portrait.w, 90);
	EXPECT_GE((overwide.stats.w - 4) / 2, 54);
}

TEST(CreaturePreviewFitTest, ShortCompactCardsKeepPortraitAndUseLegibleSymbolRows)
{
	const auto layout = fortRecruitmentPresentation::compactCreatureLayout(253, 85, 12, 103, 54);
	EXPECT_FALSE(layout.showNames);
	EXPECT_EQ(layout.rows, 4);
	EXPECT_EQ(layout.columns, 2);
	EXPECT_GE(layout.portrait.w, 90);
	EXPECT_GT(layout.portrait.h, 40);
	EXPECT_GE((layout.stats.w - 4) / 2, 54);
	EXPECT_GE(layout.rowStep, 12);
	EXPECT_LE(layout.stats.y + 4 * layout.rowStep + 2, 83);
}
