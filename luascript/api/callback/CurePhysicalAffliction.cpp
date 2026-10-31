/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ServerCallback.h"
#include "../../../lib/battle/IBattleState.h"
#include "../../../lib/battle/Unit.h"
#include "../../../lib/battle/NewHorizonsFrozen.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/spells/CurePhysicalAffliction.h"
#include <stdexcept>
namespace scripting::api
{
void ServerCallbackProxy::clearFrozenAfterDirectMagicDamage(ServerCallback & object,
	const IBattleInfoCallback & battle, const battle::Unit & unit)
{
	if(battle.battleGetUnitByID(unit.unitId()) != &unit)
		throw std::runtime_error("Frozen removal requires a unit in the given battle");
	if(!newHorizonsFrozen::enabled(battle.getBattle()->getMagicRules()))
		return;
	const auto removed = newHorizonsFrozen::removalPlan(unit);
	if(removed.empty())
		return;
	SetStackEffect change;
	change.battleID = battle.getBattle()->getBattleID();
	change.toRemove.emplace_back(unit.unitId(), removed);
	object.apply(change);
}

void ServerCallbackProxy::removeCurePhysicalAffliction(ServerCallback & object,
	const IBattleInfoCallback & battle, const battle::Unit & unit, const std::string & kind)
{
	if(kind == "frozen")
	{
		clearFrozenAfterDirectMagicDamage(object, battle, unit);
		return;
	}
	if(kind != "graveDust") throw std::invalid_argument("Unsupported physical Cure selection");
	if(!battle.getBattle() || battle.battleGetUnitByID(unit.unitId()) != &unit)
		throw std::runtime_error("Grave Dust removal requires a unit in the given battle");
	const auto removed = spells::curePhysical::graveDustRemoval(unit);
	if(removed.empty()) return;
	SetStackEffect change;
	change.battleID = battle.getBattle()->getBattleID();
	change.toRemove.emplace_back(unit.unitId(), removed);
	object.apply(change);
}

}
