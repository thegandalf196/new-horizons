/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "ReachCursor.h"

#include <algorithm>

namespace reachCursor
{
namespace
{
int textWidth(const std::string & text, const NumberMetrics & metrics)
{
	int width = 0;
	for(const char digit : text)
		width += metrics.digitWidths[digit - '0'];
	return width;
}

void placeLongNumber(Layout & result, const std::string & text, const NumberMetrics & metrics)
{
	int width = 40;
	for(size_t index = 0; index < text.size(); index += 10)
	{
		const auto row = text.substr(index, 10);
		width = std::max(width, textWidth(row, metrics) + 4);
		result.number.push_back({row, {0, 33 + static_cast<int>(result.number.size()) * metrics.lineHeight}});
	}
	for(auto & row : result.number)
		row.position.x = (width - textWidth(row.text, metrics)) / 2;
	result.bodyOffset.x = (width - Body::SIZE) / 2;
	result.hotspot.x += result.bodyOffset.x;
	result.size = {width, result.number.back().position.y + metrics.lineHeight + 1};
}
}

std::optional<Layout> makeLayout(const Key & key, const NumberMetrics & metrics)
{
	if(!valid(key) || metrics.lineHeight <= 0
		|| std::any_of(metrics.digitWidths.begin(), metrics.digitWidths.end(), [](int width) { return width <= 0; }))
		return std::nullopt;
	Layout result;
	result.body = makeBody(key.direction);
	result.hotspot = result.body.hotspot;
	const auto text = std::to_string(key.maximumReach);
	const PixelPoint origin{result.body.inkBottomRight.x + 3,
		std::max(1, result.body.inkBottomRight.y - metrics.lineHeight + 1)};
	const int width = origin.x + textWidth(text, metrics) + 1;
	if(width <= 64)
	{
		result.size = {std::max(Body::SIZE, width),
			std::max(Body::SIZE, origin.y + metrics.lineHeight + 1)};
		result.number.push_back({text, origin});
	}
	else
		placeLongNumber(result, text, metrics);
	return result;
}
}
