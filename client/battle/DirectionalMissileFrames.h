/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace directionalMissileFrames
{
/// Malformed optional animation data must not index beyond the loaded art.
inline std::size_t phases(std::size_t requested, std::size_t angles, std::size_t frames)
{
	if(requested == 0 || requested > 64 || angles == 0 || requested > frames / angles)
		return 1;
	return requested;
}

inline std::size_t frame(std::size_t angle, std::size_t phaseCount, double elapsedFrames)
{
	if(phaseCount <= 1)
		return angle;
	const auto phase = static_cast<std::size_t>(std::fmod(std::max(0.0, elapsedFrames), phaseCount));
	return angle * phaseCount + phase;
}
}
