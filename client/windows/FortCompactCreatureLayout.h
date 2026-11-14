/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../../lib/Rect.h"
#include <algorithm>

namespace fortRecruitmentPresentation
{
struct CompactCreatureLayout
{
	Rect portrait;
	Rect stats;
	int rows;
	int columns;
	int rowStep;
	bool showNames;
};

// Prefer seven named rows beside a substantial portrait. A very short card
// keeps the same portrait and uses the familiar stat symbols in two columns;
// full names remain available in the individual stat's hover help.
inline CompactCreatureLayout compactCreatureLayout(int width, int height, int fontHeight,
	int namedStatWidth, int symbolStatWidth)
{
	const int top = fontHeight + 3;
	const int bodyHeight = std::max(1, height - top - 4);
	const bool showNames = bodyHeight / 7 >= fontHeight && namedStatWidth <= width - 14 - 90;
	const int rows = showNames ? 7 : 4;
	const int columns = showNames ? 1 : 2;
	const int statWidth = showNames ? namedStatWidth : symbolStatWidth * 2 + 4;
	const int portraitWidth = std::clamp(width - statWidth - 14, 1, 124);
	const int statX = 4 + portraitWidth + 6;
	return {Rect(4, top, portraitWidth, bodyHeight),
		Rect(statX, top, std::max(1, width - statX - 4), bodyHeight),
		rows, columns, std::max(1, bodyHeight / rows), showNames};
}
}
