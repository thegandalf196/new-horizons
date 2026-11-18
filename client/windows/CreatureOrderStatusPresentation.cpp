/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CCreatureWindow.h"
#include "NewHorizonsOrderIndicators.h"
#include "../widgets/Images.h"
#include "../widgets/TextControls.h"
#include "../render/Colors.h"

CStackWindow::OrderIndicatorsSection::OrderIndicatorsSection(CStackWindow * owner, int yOffset, const CStack * stack)
	: CWindowSection(owner, {}, yOffset)
{
	OBJECT_CONSTRUCTION;
	const auto indicators = newHorizonsOrderIndicators::activeOrderIndicators(stack);
	if(indicators.empty())
		return;

	// The portrait is 100px wide and ends at y171. No inactive slots are drawn.
	constexpr int columns = 3;
	constexpr int spacing = 28;
	constexpr int iconSize = 26;
	pos.w = 100;
	pos.h = 12 + static_cast<int>((indicators.size() + columns - 1) / columns) * spacing;
	labels.push_back(std::make_shared<CLabel>(55, 0, FONT_TINY, ETextAlignment::TOPCENTER,
		Colors::YELLOW, "Orders"));
	for(size_t index = 0; index < indicators.size(); ++index)
	{
		const int x = 13 + static_cast<int>(index % columns) * spacing;
		const int y = 12 + static_cast<int>(index / columns) * spacing;
		const auto & indicator = indicators[index];
		const auto & display = newHorizonsOrderPresentation::commandDisplay(indicator.command);
		auto icon = std::make_shared<CAnimImage>(AnimationPath::builtin(display.image), 0, 0, x, y);
		icon->setScale(Point(iconSize, iconSize));
		orderIcons.push_back(std::move(icon));
		clickableAreas.push_back(std::make_shared<LRClickableAreaWText>(Rect(x, y, iconSize, iconSize),
			indicator.description, indicator.description));
	}
}
