/*
 * FrozenStatusPresentation.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#pragma once

#include "../gui/CIntObject.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

class CStack;
class CBattleInfoCallback;
struct SetStackEffect;

namespace newHorizonsFrozenPresentation
{
class ApplicationFeedback;

class FrozenStatusIcon final : public CIntObject
{
public:
	explicit FrozenStatusIcon(Point position);
	void showAll(Canvas & canvas) override;
	void show(Canvas & canvas) override;
};

std::string tooltip(int32_t shatterBonusPercent);
std::optional<std::string> currentTooltip(const CStack & stack);
std::size_t newApplicationCount(const SetStackEffect & packet,
	const CBattleInfoCallback & battle, ApplicationFeedback & feedback);
}
