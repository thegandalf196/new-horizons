/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsSoulHarvest.h"
#include "CUnitState.h"
#include "CBattleInfoCallback.h"
#include "NewHorizonsCreatureAbilitySuppression.h"
namespace newHorizonsSoulHarvest
{
bool qualifies(const CBattleInfoCallback & battle, const battle::Unit & actor,
	const battle::Unit & victim, bool physical)
{
	if(!physical || !actor.alive() || !victim.alive() || actor.isGhost() || victim.isGhost()
		|| actor.isClone() || victim.isClone() || actor.isSummoned() || victim.isSummoned()
		|| newHorizonsCreatureAbilitySuppression::suppressionLevel(actor) > 0
		|| actor.creatureId() != CreatureID(CreatureID::decode("new-horizons:ghost"))
		|| victim.isTurret() || victim.hasBonusOfType(BonusType::SIEGE_WEAPON)
		|| victim.unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER
		|| battle.battleGetOwner(&actor) == battle.battleGetOwner(&victim)
		|| !battle.battleMatchActionController(&actor, &victim))
		return false;
	const auto * actorState = dynamic_cast<const battle::CUnitState *>(&actor);
	if(!actorState || actorState->hasBattleForm()
		|| actorState->health.getShadowGiftMaximumHealthLost() > 0)
		return false;
	return true;
}
int32_t casualtiesAfterHit(const battle::Unit & victim, int32_t countBeforeHit, bool willRebirth)
{
	if(countBeforeHit <= 0 || (willRebirth && !victim.alive()))
		return 0;
	return std::max<int32_t>(0, countBeforeHit - victim.getCount());
}
bool grant(battle::CUnitState & actor, int64_t killedCreatures)
{
	if(killedCreatures <= 0 || !actor.alive()
		|| actor.isClone() || actor.isSummoned() || actor.hasBattleForm()
		|| killedCreatures > std::numeric_limits<int32_t>::max() - actor.getCount()
		|| actor.health.getShadowGiftMaximumHealthLost() > 0)
		return false;
	// The count bound above limits this product to int32_max squared in int64.
	int64_t growth = static_cast<int64_t>(actor.getMaxHealth())
		* killedCreatures * GHOSTS_PER_CREATURE_KILLED;
	if(growth <= 0 || growth > std::numeric_limits<int32_t>::max())
		return false;
	const int32_t before = actor.getCount();
	actor.heal(growth, EHealLevel::OVERHEAL, EHealPower::ONE_BATTLE);
	if(actor.getCount() != before + killedCreatures)
		throw std::logic_error("Soul Harvest did not add the qualified temporary creature count");
	return true;
}
}
