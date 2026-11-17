/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "AnimationPhaseStart.h"
#include "../../lib/battle/BattleHex.h"
#include "../../lib/Point.h"
#include <cstdint>
#include <map>
#include <memory>

class BattleInterface;
class BattleRenderer;
class CAnimation;
class CStack;

/// Scene-owned readback only. No effect here changes combat state or blocks an action.
class ForceLancePresentation
{
	struct Pin
	{
		Point origin;
		BattleHex tile;
	};
	struct Release
	{
		Pin pin;
		uint32_t elapsed = 0;
		AnimationPhaseStart phase;
	};
	std::map<uint32_t, Pin> held;
	std::map<uint32_t, Release> releasing;
	std::shared_ptr<CAnimation> holdImage;
	std::shared_ptr<CAnimation> releaseImage;

	void refresh(BattleInterface & owner);

public:
	static void strike(BattleInterface & owner, const CStack & target);
	void tick(uint32_t elapsed);
	void collect(BattleInterface & owner, BattleRenderer & renderer);
};
