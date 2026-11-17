/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "RedemptionEvent.h"
#include "CBattleInfoCallback.h"
#include "CUnitState.h"
#include "ConsecratedArms.h"
#include "IBattleState.h"
#include "../networkPacks/PacksForClientBattle.h"
#include <vcmi/ServerCallback.h>
#include <algorithm>
#include <stdexcept>
namespace battle
{
bool RedemptionEvent::hasRecipients(const CBattleInfoCallback & battle)
{
	if(!inspected)
	{
		inspected = true;
		for(const auto * unit : battle.battleGetAllUnits(false))
			if(unit->mayHaveBonusType(BonusType::NONE)
				&& consecratedArms::activePrayer(*unit, consecratedArms::Prayer::VICARIOUS_REDEMPTION))
			{ enabled = true; break; }
	}
	return enabled;
}
void RedemptionEvent::recordActualLoss(const CBattleInfoCallback & battle, uint32_t recipient,
	PlayerColor owner, int64_t actualLostMaximumHP)
{
	if(completed || actualLostMaximumHP < 0)
		throw std::logic_error("Invalid completed Redemption casualty receipt");
	if(actualLostMaximumHP > 0 && hasRecipients(battle))
		losses.push_back({recipient, owner, actualLostMaximumHP});
}
void RedemptionEvent::record(const CBattleInfoCallback & battle, std::span<const BattleStackAttacked> hits)
{
	if(completed) throw std::logic_error("Redemption event already completed");
	if(std::ranges::none_of(hits, [](const auto & hit) { return hit.killedAmount > 0; }) || !hasRecipients(battle)) return;
	for(const auto & hit : hits)
	{
		if(hit.killedAmount <= 0) continue;
		const auto * unit = battle.battleGetUnitByID(hit.stackAttacked);
		if(!unit || !unit->alive()) continue;
		const auto killed = std::min<int64_t>(hit.killedAmount, unit->getCount());
		const auto maximumHP = killed * static_cast<int64_t>(unit->getMaxHealth());
		if(maximumHP > 0)
			losses.push_back({unit->unitId(), battle.battleGetOwner(unit), maximumHP});
	}
}
void RedemptionEvent::complete(const CBattleInfoCallback & battle, ServerCallback & server)
{
	if(completed) throw std::logic_error("Redemption event completed twice");
	completed = true;
	if(losses.empty()) return;
	BattleUnitsChanged changes;
	changes.battleID = battle.getBattle()->getBattleID();
	for(const auto * unit : battle.battleGetAllUnits(false))
	{
		if(!unit->alive() || unit->isClone() || unit->getPhantomInitialIntegrity() > 0
			|| !unit->mayHaveBonusType(BonusType::NONE)) continue;
		const auto prayer = consecratedArms::activePrayer(*unit, consecratedArms::Prayer::VICARIOUS_REDEMPTION);
		if(!prayer) continue;
		const auto conversion = consecratedArms::prayerValues(*unit,
			consecratedArms::Prayer::VICARIOUS_REDEMPTION).redemptionConversionBP;
		const bool communion = prayer->policy.has(consecratedArms::Perk::COMMUNION_OF_SAINTS);
		const auto owner = battle.battleGetOwner(unit);
		std::shared_ptr<CUnitState> state;
		for(const auto & loss : losses)
		{
			const bool self = loss.recipient == unit->unitId();
			if(loss.owner != owner || (!self && !communion)) continue;
			if(!state) state = unit->acquireState();
			state->gainRedemptionShield(loss.maximumHP, conversion, !self);
		}
		if(state)
		{
			UnitChanges update(unit->unitId(), UnitChanges::EOperation::UPDATE);
			update.data = state->save();
			changes.changedStacks.push_back(std::move(update));
		}
	}
	if(!changes.changedStacks.empty()) server.apply(changes);
}
}
