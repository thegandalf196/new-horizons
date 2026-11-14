/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleAnimationClasses.h"
#include "BattleInterface.h"
#include "BattleProjectileController.h"
#include "BattleStacksController.h"
#include "CreatureAnimation.h"
#include "ThunarRangedPresentation.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/NewHorizonsCreatureSpecialtyCapabilities.h"

RangedAttackAnimation::RangedAttackAnimation(BattleInterface & owner_, const CStack * attacker, BattleHex dest_, const CStack * defender)
	: AttackAnimation(owner_, attacker, dest_, defender),
	  projectileEmitted(false)
{
	setSound(getCreature()->sounds.shoot);
}

bool RangedAttackAnimation::init()
{
	setAnimationGroup();
	initializeProjectile();

	return AttackAnimation::init();
}

void RangedAttackAnimation::setAnimationGroup()
{
	Point shooterPos = stackAnimation(attackingStack)->pos.topLeft();
	Point shotTarget = owner.stacksController->getStackPositionAtHex(dest, defendingStack);

	//maximal angle in radians between straight horizontal line and shooting line for which shot is considered to be straight (absolute value)
	static const double straightAngle = 0.2;

	double projectileAngle = -atan2(shotTarget.y - shooterPos.y, std::abs(shotTarget.x - shooterPos.x));

	// Calculate projectile start position. Offsets are read out of the CRANIM.TXT.
	if (projectileAngle > straightAngle)
		setGroup(getUpwardsGroup());
	else if (projectileAngle < -straightAngle)
		setGroup(getDownwardsGroup());
	else
		setGroup(getForwardGroup());
}

void RangedAttackAnimation::initializeProjectile()
{
	const CCreature *shooterInfo = getCreature();
	Point shotTarget = owner.stacksController->getStackPositionAtHex(dest, defendingStack) + Point(225, 225);
	Point shotOrigin = stackAnimation(attackingStack)->pos.topLeft() + Point(222, 265);
	int multiplier = stackFacingRight(attackingStack) ? 1 : -1;

	if (getGroup() == getUpwardsGroup())
	{
		shotOrigin.x += ( -25 + shooterInfo->animation.upperRightMissileOffsetX ) * multiplier;
		shotOrigin.y += shooterInfo->animation.upperRightMissileOffsetY;
	}
	else if (getGroup() == getDownwardsGroup())
	{
		shotOrigin.x += ( -25 + shooterInfo->animation.lowerRightMissileOffsetX ) * multiplier;
		shotOrigin.y += shooterInfo->animation.lowerRightMissileOffsetY;
	}
	else if (getGroup() == getForwardGroup())
	{
		shotOrigin.x += ( -25 + shooterInfo->animation.rightMissileOffsetX ) * multiplier;
		shotOrigin.y += shooterInfo->animation.rightMissileOffsetY;
	}
	else
	{
		assert(0);
	}

	createProjectile(shotOrigin, shotTarget);
}

void RangedAttackAnimation::emitProjectile()
{
	logAnim->debug("Ranged attack projectile emitted");
	owner.projectilesController->emitStackProjectile(attackingStack);
	projectileEmitted = true;
}

void RangedAttackAnimation::tick(uint32_t msPassed)
{
	// animation should be paused if there is an active projectile
	if (projectileEmitted)
	{
		if (!owner.projectilesController->hasActiveProjectile(attackingStack, false))
		{
			if(!projectileImpactEmitted)
			{
				projectileImpactEmitted = true;
				playProjectileImpact();
			}
			owner.executeAnimationStage(EAnimationEvents::HIT);
		}
	}

	bool stackHasProjectile = owner.projectilesController->hasActiveProjectile(stack, true);

	if (!projectileEmitted || stackHasProjectile)
		stackAnimation(attackingStack)->playUntil(getAttackClimaxFrame());
	else
		stackAnimation(attackingStack)->playUntil(static_cast<size_t>(-1));

	AttackAnimation::tick(msPassed);

	if (!projectileEmitted)
	{
		// emit projectile once animation playback reached "climax" frame
		if ( stackAnimation(attackingStack)->getCurrentFrame() >= getAttackClimaxFrame() )
		{
			emitProjectile();
			return;
		}
	}
}

RangedAttackAnimation::~RangedAttackAnimation()
{
	if(presentationAborted)
		return; // This presentation allocated no projectile and must not fake an emission.

	assert(!owner.projectilesController->hasActiveProjectile(attackingStack, false));
	assert(projectileEmitted);

	// FIXME: is this possible? Animation is over but we're yet to fire projectile?
	if (!projectileEmitted)
	{
		logAnim->warn("Shooting animation has finished but projectile was not emitted! Emitting it now...");
		emitProjectile();
	}
}

ShootingAnimation::ShootingAnimation(BattleInterface & owner, const CStack * attacker, BattleHex _dest, const CStack * _attacked)
	: RangedAttackAnimation(owner, attacker, _dest, _attacked)
{
	logAnim->debug("Created ShootingAnimation for %s", stack->getName());
}

bool ShootingAnimation::init()
{
	if(newHorizonsCreatureSpecialtyCapabilities::stonecallerRanged(*attackingStack))
	{
		setAnimationGroup();
		if(stackAnimation(attackingStack)->framesInGroup(getGroup()) == 0)
		{
			logGlobal->error("Stonecaller ranged presentation has neither a native shooting group nor a matching attack pose");
			presentationAborted = true;
			BattleInterface & completionOwner = owner;
			delete this;
			completionOwner.executeAnimationStage(EAnimationEvents::HIT);
			return false;
		}
	}
	return RangedAttackAnimation::init();
}

void ShootingAnimation::initializeProjectile()
{
	if(const auto origin = thunarRangedPresentation::originForShot(*attackingStack,
		*stackAnimation(attackingStack), getGroup(), stackFacingRight(attackingStack)))
	{
		const auto target = owner.stacksController->getStackPositionAtHex(dest, defendingStack) + Point(225, 225);
		createProjectile(stackAnimation(attackingStack)->pos.topLeft() + *origin, target);
		return;
	}
	RangedAttackAnimation::initializeProjectile();
}

void ShootingAnimation::createProjectile(const Point & from, const Point & dest) const
{
	owner.projectilesController->createProjectile(attackingStack, from, dest);
}

void ShootingAnimation::playProjectileImpact()
{
	const auto & impact = getCreature()->animation.projectileImpactName;
	if(!impact.empty() && dest.isValid())
		owner.stacksController->addNewAnim(new EffectAnimation(owner, impact, dest));
}

uint32_t ShootingAnimation::getAttackClimaxFrame() const
{
	if(const auto frame = thunarRangedPresentation::climaxForShot(*attackingStack,
		*stackAnimation(attackingStack), getGroup()))
		return static_cast<uint32_t>(*frame);
	const CCreature *shooterInfo = getCreature();

	uint32_t maxFrames = stackAnimation(attackingStack)->framesInGroup(getGroup());
	uint32_t climaxFrame = shooterInfo->animation.attackClimaxFrame;
	uint32_t selectedFrame = std::clamp<int>(shooterInfo->animation.attackClimaxFrame, 1, maxFrames);

	if (climaxFrame != selectedFrame)
		logGlobal->warn("Shooter %s has ranged attack climax frame set to %d, but only %d available!", shooterInfo->getNamePluralTranslated(), climaxFrame, maxFrames);

	return selectedFrame - 1; // H3 counts frames from 1
}

ECreatureAnimType ShootingAnimation::getUpwardsGroup() const
{
	return thunarRangedPresentation::shootingGroup(*attackingStack,
		*stackAnimation(attackingStack), ECreatureAnimType::SHOOT_UP);
}

ECreatureAnimType ShootingAnimation::getForwardGroup() const
{
	return thunarRangedPresentation::shootingGroup(*attackingStack,
		*stackAnimation(attackingStack), ECreatureAnimType::SHOOT_FRONT);
}

ECreatureAnimType ShootingAnimation::getDownwardsGroup() const
{
	return thunarRangedPresentation::shootingGroup(*attackingStack,
		*stackAnimation(attackingStack), ECreatureAnimType::SHOOT_DOWN);
}
