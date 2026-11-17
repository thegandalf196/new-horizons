/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsMetamagicAmmunition.h"
#include "../battle/CUnitState.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/NewHorizonsFocusMagic.h"
#include "../battle/FocusMagicValueState.h"
#include "../battle/IBattleState.h"
#include "../combatScripts/nativeEvents/NativeEventCommon.h"
#include "../networkPacks/SetStackEffect.h"
namespace newHorizonsMetamagic
{
namespace
{
void destroyFocusCharges(ServerCallback & server, const CBattleInfoCallback & callback,
	const battle::Unit & unit, int maximum)
{
	auto captured = newHorizonsFocusMagic::captureValueState(unit);
	if(!captured || captured->charges <= 0 || maximum <= 0) return;
	const auto original = captured->records;
	for(int charge = 0; charge < maximum && captured->charges > 0; ++charge)
		*captured = newHorizonsFocusMagic::prepareStrike(*captured, true, true).after;
	SetStackEffect packet;
	packet.battleID = callback.getBattle()->getBattleID();
	packet.toRemove.emplace_back(unit.unitId(), original);
	if(!captured->records.empty()) packet.toAdd.emplace_back(unit.unitId(), captured->records);
	server.apply(packet);
}
}
void destroyShots(ServerCallback & server, const CBattleInfoCallback & callback, const battle::Unit & unit)
{
	if(!unit.alive() || unit.isGhost()) return;
	if(newHorizonsFocusMagic::converted(unit))
	{
		destroyFocusCharges(server, callback, unit, 3);
		return;
	}
	// Suppress this stack's Cart exemption for the remainder of this battle.
	// It is nonmagical OTHER metadata: never dispel fuel or an enchantment seed.
	Bonus disruption(BonusDuration::ONE_BATTLE, BonusType::NONE, BonusSource::OTHER, 1, BonusSourceID());
	disruption.stacking = battle::CShots::DISRUPTED_SUPPLY;
	SetStackEffect supply;
	supply.battleID = callback.getBattle()->getBattleID();
	supply.toUpdate.emplace_back(unit.unitId(), std::vector<Bonus>{disruption});
	server.apply(supply);
	auto state = unit.acquireState();
	if(!state || !state->shots.isLimited()) return;
	const int lost = std::min(3, std::max(0, state->shots.available()));
	if(lost == 0) return;
	// Enhancement charges belong to the same actual destroyed shots. Native
	// reserves of zero/one cannot destroy three unrelated enhancement charges.
	destroyFocusCharges(server, callback, unit, lost);
	state = unit.acquireState();
	state->shots.use(lost);
	static const JsonNode parameters;
	static const CombatEventPayload payload;
	nativeEvents::changeUnit({server, callback, nullptr, nullptr, parameters, payload}, *state);
}
}
