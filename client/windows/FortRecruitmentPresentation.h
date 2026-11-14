/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/Point.h"
#include "../../lib/Rect.h"
#include <memory>
class CCreature;
class CPicture;
namespace fortRecruitmentPresentation
{
constexpr int NH_FORT_CARD_GAP = 4;
constexpr int NH_FORT_SIDE_MARGIN = 12;
constexpr int NH_FORT_CARDS_PER_ROW = 3;
constexpr int NH_FORT_FOOTER_HEIGHT = 48;
constexpr int NH_FORT_STAT_COUNT = 7;
constexpr int NH_FORT_COMPACT_STAT_COLUMNS = 2;
constexpr int NH_FORT_COMPACT_STAT_ROWS = (NH_FORT_STAT_COUNT + NH_FORT_COMPACT_STAT_COLUMNS - 1) / NH_FORT_COMPACT_STAT_COLUMNS;
constexpr int NH_FORT_COMPACT_PORTRAIT_HEIGHT = 15;
constexpr int NH_FORT_CONFLUX_COMPACT_PORTRAIT_MAX_HEIGHT = 40;
int rankedFortTinyFontHeight();
int rankedFortStatTop(bool compactStatGrid = false, int compactPortraitHeight = NH_FORT_COMPACT_PORTRAIT_HEIGHT);
int rankedFortStatBottomPadding();
int rankedFortStatMinimumRowHeight();
int rankedFortStatRowCount(bool compactStatGrid);
int rankedFortMinimumCardHeight(bool compactStatGrid, int compactPortraitHeight = NH_FORT_COMPACT_PORTRAIT_HEIGHT);
int rankedFortStatRowHeight(int cardHeight, bool compactStatGrid, int compactPortraitHeight = NH_FORT_COMPACT_PORTRAIT_HEIGHT);
int rankedFortConfluxCompactPortraitHeight(int cardHeight);
std::shared_ptr<CPicture> createResponsiveFortCardBackground(const Point & size);
void createCreaturePreview(const Rect & viewport, const CCreature * creature, bool contain);
}
