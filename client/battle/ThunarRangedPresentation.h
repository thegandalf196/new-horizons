/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "BattleConstants.h"
#include "../../lib/Point.h"
#include <array>
#include <algorithm>
#include <cstddef>
#include <optional>

class CStack;
class CreatureAnimation;

namespace thunarRangedPresentation
{
inline constexpr auto PROJECTILE = "PCYCLBX";
enum class Family : std::size_t { EARTH, MAGMA };
enum class Direction : std::size_t { UP, FRONT, DOWN };

struct Admission
{
	/// Supplied by canonical saved-rule/actual-unit capability provenance.
	bool qualifyingSpecialty = false;
	std::optional<Family> family;
};

struct Pose
{
	ECreatureAnimType group;
	std::size_t climaxFrame = 3; // Native fourth frame is the outward strike.
};

inline std::optional<Pose> fallbackPose(const Admission & admission, Direction direction,
	std::size_t nativeShootingFrames, std::size_t fallbackAttackFrames)
{
	if(!admission.qualifyingSpecialty || !admission.family
		|| static_cast<std::size_t>(*admission.family) >= 2 || nativeShootingFrames != 0
		|| fallbackAttackFrames == 0)
		return std::nullopt;
	constexpr std::array groups{ECreatureAnimType::ATTACK_UP,
		ECreatureAnimType::ATTACK_FRONT, ECreatureAnimType::ATTACK_DOWN};
	const auto index = static_cast<std::size_t>(direction);
	if(index >= groups.size())
		return std::nullopt;
	return Pose{groups[index], std::min<std::size_t>(3, fallbackAttackFrames - 1)};
}

/// Initial emission centers selected from decoded native frame4. They require
/// actual directional/facing rendering before being considered visually accepted.
inline std::optional<Point> provisionalOrigin(Family family, Direction direction,
	bool facingRight, int canvasWidth)
{
	constexpr std::array<std::array<Point, 3>, 2> rightOrigins{{
		{{Point(232, 182), Point(249, 206), Point(223, 244)}},
		{{Point(233, 183), Point(248, 208), Point(227, 244)}}
	}};
	const auto familyIndex = static_cast<std::size_t>(family);
	const auto directionIndex = static_cast<std::size_t>(direction);
	if(canvasWidth != 450 || familyIndex >= rightOrigins.size() || directionIndex >= 3)
		return std::nullopt;
	Point origin = rightOrigins[familyIndex][directionIndex];
	if(!facingRight)
		origin.x = canvasWidth - origin.x;
	return origin;
}

ECreatureAnimType shootingGroup(const CStack & stack, const CreatureAnimation & animation, ECreatureAnimType requested);
std::optional<std::size_t> climaxForShot(const CStack & stack, const CreatureAnimation & animation, ECreatureAnimType group);
std::optional<Point> originForShot(const CStack & stack, const CreatureAnimation & animation, ECreatureAnimType group, bool facingRight);
}
