/* Part of VCMI engine; GNU General Public License v2.0 or later. */
#include "StdInc.h"
#include "GameStatePackVisitor.h"
#include "CGameState.h"
#include "../CPlayerState.h"

void GameStatePackVisitor::visitEstatesKingdomChanged(EstatesKingdomChanged & pack)
{
	pack.before.validate();
	pack.after.validate();
	if(!pack.owner.isValidPlayer() || !gs.players.contains(pack.owner))
		throw std::runtime_error("Estates update for an invalid kingdom");
	auto & state = gs.players.at(pack.owner);
	if(state.estates != pack.before)
		throw std::runtime_error("Stale Estates kingdom update");
	for(const auto amount : pack.payment)
		if(amount < 0)
			throw std::runtime_error("Negative Estates purchase payment");
	if(!state.resources.canAfford(pack.payment))
		throw std::runtime_error("Unaffordable Estates purchase payment");
	if(!state.validStewardAssignments(pack.after))
		throw std::runtime_error("Invalid Estates steward appointment");
	for(size_t resource = 0; resource < pack.after.recruitmentBaseCost.size(); ++resource)
		if(pack.after.recruitmentBaseCost[resource] < pack.before.recruitmentBaseCost[resource])
			throw std::runtime_error("Conscription cumulative ledger cannot decrease");
	state.resources -= pack.payment;
	state.estates = pack.after;
}
