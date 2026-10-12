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

/// The sprite and its position consume the same interval in one update.
class AnimationFrameAdvance
{
	AnimationPhaseStart phaseStart;
	float seconds = 0.f;

public:
	void restart()
	{
		phaseStart.restart();
		seconds = 0.f;
	}
	void onRenderAttempt() { phaseStart.onRenderAttempt(); }
	void advance(float elapsed) { seconds = phaseStart.elapsedForAdvance(elapsed); }
	float elapsedSeconds() const { return seconds; }
	double advanceProgress(double progress, double progressPerSecond) const
	{
		return progress + seconds * progressPerSecond;
	}
};
