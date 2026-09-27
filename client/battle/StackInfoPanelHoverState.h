/*
 * StackInfoPanelHoverState.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include <cstdint>

namespace newHorizonsBattleStatus
{
/// Allow normal pointer travel from a battlefield stack to its distant side panel.
inline constexpr uint32_t STACK_INFO_PANEL_HOVER_GRACE_MS = 2000;

class StackInfoPanelHoverRetention
{
	uint32_t remainingMs = 0;

public:
	void stackInspected()
	{
		remainingMs = STACK_INFO_PANEL_HOVER_GRACE_MS;
	}

	bool retain(uint32_t elapsedMs, bool cursorOverPanel)
	{
		if(cursorOverPanel)
		{
			remainingMs = STACK_INFO_PANEL_HOVER_GRACE_MS;
			return true;
		}

		if(elapsedMs >= remainingMs)
		{
			remainingMs = 0;
			return false;
		}

		remainingMs -= elapsedMs;
		return true;
	}

	void clear()
	{
		remainingMs = 0;
	}
};
}
