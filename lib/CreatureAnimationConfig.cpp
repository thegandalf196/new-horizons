/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CCreatureHandler.h"
#include "json/JsonNode.h"

void CCreatureHandler::loadJsonAnimation(CCreature * cre, const JsonNode & graphics) const
{
	cre->animation.timeBetweenFidgets = graphics["timeBetweenFidgets"].Float();

	const JsonNode & animationTime = graphics["animationTime"];
	cre->animation.walkAnimationTime = animationTime["walk"].Float();
	cre->animation.idleAnimationTime = animationTime["idle"].Float();
	cre->animation.attackAnimationTime = animationTime["attack"].Float();

	const JsonNode & missile = graphics["missile"];
	const JsonNode & offsets = missile["offset"];
	cre->animation.upperRightMissileOffsetX = static_cast<int>(offsets["upperX"].Float());
	cre->animation.upperRightMissileOffsetY = static_cast<int>(offsets["upperY"].Float());
	cre->animation.rightMissileOffsetX =      static_cast<int>(offsets["middleX"].Float());
	cre->animation.rightMissileOffsetY =      static_cast<int>(offsets["middleY"].Float());
	cre->animation.lowerRightMissileOffsetX = static_cast<int>(offsets["lowerX"].Float());
	cre->animation.lowerRightMissileOffsetY = static_cast<int>(offsets["lowerY"].Float());

	cre->animation.attackClimaxFrame = static_cast<int>(missile["attackClimaxFrame"].Float());
	cre->animation.missileFrameAngles = missile["frameAngles"].convertTo<std::vector<double> >();

	cre->animation.projectileImageName = AnimationPath::fromJson(graphics["missile"]["projectile"]);
	cre->animation.projectileImpactName = AnimationPath::fromJson(graphics["missile"]["impact"]);
	cre->animation.projectileFramesPerAngle = static_cast<int>(std::clamp<int64_t>(
		graphics["missile"]["framesPerAngle"].Integer(), 1, 64));

	for(const JsonNode & value : graphics["missile"]["ray"].Vector())
		cre->animation.projectileRay.push_back(RayColor::fromJson(value));

	cre->smallIconName = graphics["iconSmall"].String();
	cre->largeIconName = graphics["iconLarge"].String();
}
