/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "StackWithBonuses.h"
#include "OwnedFrameEffectQueries.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/GraveDustOrderedPreflight.h"
#include "../../lib/networkPacks/SetStackEffect.h"
void StackWithBonuses::applyOwnedBonusVector(const std::vector<Bonus> & records, newHorizonsGraveDust::EffectOperation operation)
{
	OwnedFrameEffectQueries preview(*owner, *this, records);
	preview.replay(id, records, operation);
	if(newHorizonsGraveDust::containsEffect(records)) newHorizonsGraveDust::validateInstalledGroup(preview.current(id));
	preview.publish();
	if(operation != newHorizonsGraveDust::EffectOperation::REMOVE)
		for(const auto & record : records) owner->noteSerpentBindingAdmission(record);
}
void HypotheticBattle::applyOwnedEffectChanges(const SetStackEffect & packet)
{
	requireOwnedFrameSource();
	if(packet.battleID != getBattleID()) throw std::invalid_argument("Foreign owned effect packet");
	packet.validateExchange();
	if(packet.exchange) throw std::logic_error("Owned exchange requires its complete canonical exchange transaction");
	OwnedFrameEffectQueries preview(*this, packet);
	newHorizonsGraveDust::validateOrderedEffectChanges(*this, packet,
		[&](uint32_t id) { return preview.find(id); },
		[&](uint32_t id, const std::vector<Bonus> & records, newHorizonsGraveDust::EffectOperation operation)
		{ preview.replay(id, records, operation); });
	// The shared Dust preflight is intentionally a no-Dust fast path. Other
	// packets still perform their canonical ordered vectors in this SAME scope.
	if(!newHorizonsGraveDust::containsEffect(packet))
		for(const auto operation : {newHorizonsGraveDust::EffectOperation::REMOVE,
			newHorizonsGraveDust::EffectOperation::UPDATE, newHorizonsGraveDust::EffectOperation::ADD})
		{
			const auto & rows = operation == newHorizonsGraveDust::EffectOperation::REMOVE ? packet.toRemove
				: operation == newHorizonsGraveDust::EffectOperation::UPDATE ? packet.toUpdate : packet.toAdd;
			for(const auto & [id, records] : rows) preview.replay(id, records, operation);
		}
	preview.publish();
}
