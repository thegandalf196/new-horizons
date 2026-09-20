/*
 * SpellObstacleDescriptor.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "SpellObstacleDescriptor.h"

#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/filesystem/ResourcePath.h"
#include "../../../lib/spells/NewHorizonsMagic.h"

namespace scripting::api
{

SpellCreatedObstacle SpellObstacleDescriptor::toObstacle() const
{
	if(casterPowerDivisor <= 0)
		throw std::runtime_error("Invalid spell obstacle power divisor");

	SpellCreatedObstacle obstacle;
	obstacle.pos              = pos;
	obstacle.obstacleType     = obstacleType;
	obstacle.ID               = spell ? spell->getId() : SpellID(SpellID::NONE);
	obstacle.turnsRemaining   = turnsRemaining;
	obstacle.casterSpellPower = casterSpellPower;
	obstacle.casterPowerDivisor = casterPowerDivisor;
	obstacle.spellLevel       = spellLevel;
	obstacle.casterSide       = casterSide;
	obstacle.minimalDamage    = minimalDamage;

	obstacle.hidden          = hidden;
	obstacle.passable        = passable;
	obstacle.trap            = trap;
	obstacle.removeOnTrigger = removeOnTrigger;
	obstacle.nativeVisible   = nativeVisible;
	// The explicit script field is authoritative.  Also recognize the complete
	// canonical NH Land Mine descriptor at this C++ boundary so older cached Lua
	// registries cannot silently discard the marker while the engine and script
	// are being upgraded together.  Legacy mines use divisor 1 and/or no exact
	// damage value, so they retain floor semantics.
	obstacle.damageSnapshot  = damageSnapshot
		|| (obstacle.ID == SpellID(SpellID::LAND_MINE)
			&& casterPowerDivisor == newHorizonsMagic::DIRECT_DAMAGE_POWER_DIVISOR
			&& minimalDamage > 0 && hidden && !nativeVisible && removeOnTrigger);

	obstacle.trigger = trigger.empty() ? SpellID(SpellID::NONE) : SpellID(SpellID::decode(trigger));

	obstacle.appearSound      = AudioPath::builtin(appearSound);
	obstacle.appearAnimation  = AnimationPath::builtin(appearAnimation);
	obstacle.animation        = AnimationPath::builtin(animation);
	obstacle.removalAnimation = AnimationPath::builtin(removalAnimation);

	for(const BattleHex & hex : customSize)
		obstacle.customSize.insert(hex);

	return obstacle;
}

}
