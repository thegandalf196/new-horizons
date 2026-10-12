/*
 * AnimationPhaseStartTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "../battle/AnimationPhaseStart.h"
#include <cstdint>
#include <iostream>
#include <limits>
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
		AnimationPhaseStart phase;
		require(phase.elapsedForAdvance(uint32_t{100}) == 0, "New phase must not consume preceding 100ms");
		require(phase.elapsedForAdvance(uint32_t{100}) == 100, "Second tick must advance even without a render");
		require(phase.elapsedForAdvance(uint32_t{100}) == 100, "Deferral must not repeat on later ticks");
		phase.restart();
		phase.onRenderAttempt();
		require(phase.elapsedForAdvance(uint32_t{100}) == 100, "Initial draw before tick must release deferral");
		phase.restart();
		require(phase.elapsedForAdvance(0.1f) == 0.f, "A restarted creature phase gets a fresh opportunity");
		require(phase.elapsedForAdvance(0.1f) == 0.1f, "Float creature intervals must be unchanged subsequently");
		phase.onRenderAttempt();
		phase.onRenderAttempt();
		require(phase.elapsedForAdvance(0.2f) == 0.2f, "Repeated draws cannot reset the phase");
		phase.restart();
		require(phase.elapsedForAdvance(uint32_t{0}) == 0, "Zero interval remains zero");
		require(phase.elapsedForAdvance(uint32_t{100}) == 100, "Zero first interval consumes only one opportunity");
		phase.restart();
		const auto large = std::numeric_limits<uint32_t>::max();
		require(phase.elapsedForAdvance(large) == 0, "Large preceding interval is not charged to new projectile");
		require(phase.elapsedForAdvance(large) == large, "Later large interval must not be capped or discarded");
		std::cout << "Animation phase start: all checks passed\n";
		return 0;
	}
	catch(const std::exception & error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
