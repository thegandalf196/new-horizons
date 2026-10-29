/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsParalyzingGaze.h"
#include "DefiantDenial.h"
#include "../CGameHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
namespace newHorizonsParalyzingGaze
{
bool consumeRetaliation(CGameHandler & handler, const CBattleInfoCallback & battle,
	const CStack & retaliator, const CStack & opponent, bool ranged,
	const newHorizonsAerialSupremacy::Excursion * aerial)
{
	if(!canConsume(retaliator))
		return false;
	const auto token = pending(retaliator);
	SetStackEffect remove;
	remove.battleID = battle.getBattle()->getBattleID();
	remove.toRemove.emplace_back(retaliator.unitId(), std::vector<Bonus>{*token});
	// This typed validator runs before either packet. Counter-only state cannot
	// alter the exact token, life, position, control or Time Stop precondition.
	validateMarker(remove.toRemove.front().second.front());
	if(battleDefiant::consumeGaze(handler, battle, opponent, retaliator, ranged, aerial))
	{
		handler.sendAndApply(remove);
		return false; // No counter resource was used; the actual hit will use it.
	}
	auto state = retaliator.acquireState();
	state->counterAttacks.use();
	if(aerial && retaliator.creatureId() == CreatureID(CreatureID::decode("core:griffin")))
		state->aerialRetaliationRound = battle.battleGetRound();
	BattleUnitsChanged resources;
	resources.battleID = remove.battleID;
	UnitChanges update(retaliator.unitId(), UnitChanges::EOperation::UPDATE);
	update.data = state->save();
	resources.changedStacks.push_back(std::move(update));
	handler.sendAndApply(resources);
	handler.sendAndApply(remove);
	return true;
}
void applyAfterShot(CGameHandler & handler, const CBattleInfoCallback & battle,
	uint32_t sourceId, uint32_t targetId, bool admitted)
{
	if(!admitted)
		return;
	const auto * source = battle.battleGetUnitByID(sourceId);
	const auto * target = battle.battleGetUnitByID(targetId);
	if(!qualifiesAttack(battle, source, target, true, true, false) || pending(*target))
		return;
	SetStackEffect effect;
	effect.battleID = battle.getBattle()->getBattleID();
	effect.toAdd.emplace_back(targetId, std::vector<Bonus>{marker()});
	handler.sendAndApply(effect);
}
}
