/*
 * ProjectileFlightLifecycleTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "../battle/ProjectileFlightLifecycle.h"

#include <iostream>
#include <stdexcept>

static void require(bool condition, const char * message)
{
	if(!condition)
		throw std::runtime_error(message);
}

int main()
{
	try
	{
		ProjectileFlightLifecycle missile;
		missile.protectFirstRender();
		const float progress = 0.1f * 3750.f / 300.f;
		require(progress > 1.f, "Fast 100ms flight must reproduce completion before redraw");
		require(!missile.canRemove(progress), "Unseen completed missile must survive");
		require(missile.visibleProgress(progress) == 0.5f, "First completed flight must draw in-flight, not overshoot");
		require(!missile.canRemove(progress + 1.f), "Repeated ticks without redraw must retain first presentation");
		missile.onRenderAttempt();
		require(missile.visibleProgress(progress) == 0.5f, "Repeated redraw before retirement must preserve the visible in-flight frame");
		missile.onRenderAttempt();
		require(missile.visibleProgress(progress) == 0.5f, "Further redraws must not overwrite the in-flight frame with an endpoint");
		require(missile.canRemove(progress), "Presented completed missile must retire without slowing its flight");

		ProjectileFlightLifecycle ordinary;
		ordinary.protectFirstRender();
		require(ordinary.visibleProgress(0.f) == 0.f && !ordinary.canRemove(0.f), "Paused un-emitted missile must remain at origin");
		require(ordinary.visibleProgress(0.2f) == 0.2f, "Short first flight tick must be unchanged");
		ordinary.onRenderAttempt();
		require(ordinary.visibleProgress(0.8f) == 0.8f && !ordinary.canRemove(0.8f), "Ordinary subsequent flight must be unchanged");
		require(!ordinary.canRemove(1.f) && ordinary.canRemove(1.01f), "Existing strict completion boundary must remain");

		ProjectileFlightLifecycle catapult;
		catapult.protectFirstRender();
		require(catapult.visibleProgress(4.f) == 0.5f && !catapult.canRemove(4.f), "Catapult must retain a bounded first flight position");
		catapult.onRenderAttempt();
		require(catapult.canRemove(4.f), "Catapult must complete after presentation");

		ProjectileFlightLifecycle ray;
		require(ray.visibleProgress(1.f) == 1.f && !ray.canRemove(1.f), "Ray endpoint hold must remain unchanged");
		require(ray.canRemove(2.f), "Ray expiry must remain unchanged without requiring a render");
		std::cout << "Projectile flight lifecycle: all checks passed\n";
		return 0;
	}
	catch(const std::exception & error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
