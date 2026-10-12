/*
 * AnimationPhaseStart.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

/// A new phase must not consume the interval preceding its initialization.
/// This defers at most one update, independently of whether rendering occurs.
class AnimationPhaseStart
{
	bool pending = true;

public:
	void restart() { pending = true; }
	void onRenderAttempt() { pending = false; }

	template<typename Duration>
	Duration elapsedForAdvance(Duration elapsed)
	{
		if(pending)
		{
			pending = false;
			return Duration{};
		}
		return elapsed;
	}
};
