/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "../battle/AerialSupremacyReturnPresentation.h"
#include <cstdlib>
#include <functional>

namespace
{
struct Visual
{
	struct Position { int value = 9; void moveTo(int origin) { value = origin; } } pos;
	bool dead = false;
	bool isDeadOrDying() const { return dead; }
};
void require(bool value) { if(!value) std::abort(); }
}
int main()
{
	bool facesAwayFromPointBlankTarget = true;
	std::function<void()> beforeHit;
	const bool flightRequired = aerialSupremacyPresentation::stageExcursionFacing(3, 3, [&]()
	{
		beforeHit = [&]() { facesAwayFromPointBlankTarget = false; };
	});
	require(!flightRequired && static_cast<bool>(beforeHit));
	beforeHit();
	require(!facesAwayFromPointBlankTarget);
	Visual actor;
	int started = 0, moved = 0, ended = 0;
	aerialSupremacyPresentation::animateReturn(&actor, [&]() { ++started; });
	// Lethal reflection happens between queued stages; don't trust an earlier alive snapshot.
	actor.dead = true;
	aerialSupremacyPresentation::animateReturn(&actor, [&]() { ++moved; });
	aerialSupremacyPresentation::finishReturn(&actor, 3, [&]() { ++ended; });
	require(started == 1 && moved == 0 && ended == 0 && actor.pos.value == 3);
	actor.dead = false;
	aerialSupremacyPresentation::animateReturn(&actor, [&]() { ++moved; });
	aerialSupremacyPresentation::finishReturn(&actor, 3, [&]() { ++ended; });
	require(moved == 1 && ended == 1);
	aerialSupremacyPresentation::animateReturn(static_cast<Visual *>(nullptr), [&]() { std::abort(); });
	aerialSupremacyPresentation::finishReturn(static_cast<Visual *>(nullptr), 3, [&]() { std::abort(); });
}
