/*
 * FrozenStatusPresentation.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "FrozenStatusPresentation.h"
#include "FrozenApplicationFeedback.h"

#include "render/Canvas.h"
#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/texts/CGeneralTextHandler.h"

#include <array>
#include <stdexcept>
#include <utility>

namespace newHorizonsFrozenPresentation
{
FrozenStatusIcon::FrozenStatusIcon(Point position) : CIntObject(0, position)
{
	pos.w = 48;
	pos.h = 36;
}

void FrozenStatusIcon::showAll(Canvas & canvas)
{
	show(canvas);
}

void FrozenStatusIcon::show(Canvas & canvas)
{
	const auto center = pos.topLeft() + Point(24, 17);
	const ColorRGBA shadow(24, 54, 86, 255);
	const ColorRGBA ice(122, 193, 222, 255);
	const ColorRGBA light(224, 247, 255, 255);
	const std::array<Point, 6> tips{{Point(0, -13), Point(11, -7), Point(11, 7), Point(0, 13), Point(-11, 7), Point(-11, -7)}};
	for(std::size_t index = 0; index < tips.size(); ++index)
	{
		const auto tip = center + tips[index];
		canvas.drawLine(center + Point(1, 1), tip + Point(1, 1), shadow, shadow);
		canvas.drawLine(center, tip, ice, light);
		canvas.drawLine(tip, center + tips[(index + 1) % tips.size()], light, ice);
	}
}

std::string tooltip(int32_t shatterBonusPercent)
{
	auto result = LIBRARY->generaltexth->translate("new-horizons.combat.frozen.tooltip");
	const std::array<std::pair<std::string, std::string>, 2> replacements{{
		{"%label%", LIBRARY->generaltexth->translate("new-horizons.combat.frozen.label")},
		{"%bonus%", std::to_string(shatterBonusPercent)}}};
	for(const auto & [token, replacement] : replacements)
	{
		std::size_t position = 0;
		while((position = result.find(token, position)) != std::string::npos)
		{
			result.replace(position, token.size(), replacement);
			position += replacement.size();
		}
	}
	return result;
}

std::optional<std::string> currentTooltip(const CStack & stack)
{
	if(!stack.alive() || !newHorizonsFrozen::isFrozen(stack))
		return {};
	const auto * battle = stack.getBattle();
	if(!battle)
		throw std::logic_error("Physical Frozen status requires its battle context");
	return tooltip(newHorizonsFrozen::shatterBonusPercent(battle->getMagicRules()));
}

std::size_t newApplicationCount(const SetStackEffect & packet,
	const CBattleInfoCallback & battle, ApplicationFeedback & feedback)
{
	std::size_t applications = 0;
	for(const auto & [unitId, bonuses] : packet.toAdd)
	{
		const auto * unit = battle.battleGetStackByID(unitId, false);
		if(!unit || !unit->alive() || !newHorizonsFrozen::isFrozen(*unit))
			continue;
		for(const auto & bonus : bonuses)
		{
			const auto round = newHorizonsFrozen::markerApplicationRound(bonus);
			if(round && unit->frozenLastAppliedRound() == *round && feedback.record(unitId, *round))
				++applications;
		}
	}
	return applications;
}

void drawShatterShards(Canvas & canvas, const Rect & bounds, uint32_t elapsedMilliseconds)
{
	const auto center = bounds.center();
	const int distance = 5 + static_cast<int>(elapsedMilliseconds) / 14;
	const ColorRGBA ice(200, 237, 255, static_cast<uint8_t>(255 - elapsedMilliseconds * 255 / 550));
	const std::array<Point, 6> directions{{Point(0, -2), Point(2, -1), Point(2, 1), Point(0, 2), Point(-2, 1), Point(-2, -1)}};
	for(const auto & direction : directions)
	{
		const auto shard = center + Point(direction.x * distance, direction.y * distance);
		canvas.drawLine(shard + Point(-2, -4), shard + Point(3, 0), ice, ice);
		canvas.drawLine(shard + Point(3, 0), shard + Point(-1, 4), ice, ice);
		canvas.drawLine(shard + Point(-1, 4), shard + Point(-2, -4), ice, ice);
	}
}
}
