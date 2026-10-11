/*
 * ProjectileFlightLifecycle.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

/// Keep short missile flights visible without changing their elapsed-time progress.
class ProjectileFlightLifecycle
{
	bool protectedFlight = false;
	bool renderAttempted = false;

public:
	void protectFirstRender() { protectedFlight = true; }
	void onRenderAttempt() { renderAttempted = true; }

	float visibleProgress(float progress) const
	{
		// A completed flight has no in-flight position left. Present one midpoint
		// frame instead of drawing beyond its destination or silently deleting it.
		if(protectedFlight && progress >= 1.f)
			return 0.5f;
		return progress;
	}

	bool canRemove(float progress) const
	{
		return progress > 1.f && (!protectedFlight || renderAttempted);
	}
};
