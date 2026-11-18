/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleProjectileController.h"
#include "CreatureAnimation.h"

std::shared_ptr<ProjectileBase> BattleProjectileController::createFocusMagicProjectile(Point from, Point dest)
{
	const auto path = AnimationPath::builtin("NH_focus_magic_orb");
	if(!projectilesCache.contains(path))
		projectilesCache.emplace(path, createProjectileImage(path));

	auto orb = std::make_shared<ProjectileMissile>();
	orb->visibility.protectFirstRender();
	orb->animation = projectilesCache.at(path);
	orb->reverse = from.x > dest.x;
	orb->frameNum = 0;
	orb->speed = computeProjectileFlightTime(from, dest, AnimationControls::getProjectileSpeed());
	return orb;
}
