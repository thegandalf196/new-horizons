/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedFrameEffectQueries.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/OwnedUnitBonusFrameData.h"
#include "../../lib/battle/UnitStateValueExchange.h"
#include <stdexcept>
#include <type_traits>
void OwnedFrameEffectQueries::publish()
{
	static_assert(std::is_nothrow_swappable_v<recipientBonusOverlay::Layer>);
	owner.requireOwnedFrameSource(); synchronize();
	std::vector<battle::OwnedUnitBonusFrame::Data::CurrentPeers> peers;
	std::vector<std::pair<StackWithBonuses *, battle::OwnedUnitBonusFrame::Scratch>> scalars;
	for(size_t i = 0; i < actual.size(); ++i)
	{
		peers.push_back(actual[i]->data->prepareCurrentPeers(order));
		if(isolated && actual[i]->unit().unitId() != target->unitId()) continue;
		const auto found = owner.stackStates.find(actual[i]->unit().unitId());
		if(target && target->unitId() == actual[i]->unit().unitId()) scalars.emplace_back(target, order[i]->scratch());
		else if(found != owner.stackStates.end()) scalars.emplace_back(found->second.get(), order[i]->scratch());
	}
	// All allocations, scalar copies and complete-roster validation precede any
	// actual branch write. Source records/pools are read-only throughout replay.
	for(size_t i = 0; i < actual.size(); ++i)
	{
		if(isolated && actual[i]->unit().unitId() != target->unitId()) continue;
		auto & to = *actual[i]->data; auto & from = *order[i]->data;
		battle::exchangePreparedUnitStateValues(*to.subject, *from.subject);
		to.ledger.swap(from.ledger); to.sourceReplacements.swap(from.sourceReplacements);
		to.pendingPayloads.swap(from.pendingPayloads); to.env.swap(peers[i].environment);
		to.peerQueries->swap(peers[i].queries); to.subject->bindStateEnvironment(to.env.get());
		to.hasCurrentRoster = true;
	}
	for(auto & [unit, prepared] : scalars)
	{
		battle::exchangePreparedUnitStateValues(*unit, prepared.unit());
		++unit->treeVersionLocal;
	}
}
