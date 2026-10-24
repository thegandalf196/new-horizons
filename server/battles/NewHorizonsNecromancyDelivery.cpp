/*
 * Part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsRaisedArmy.h"
#include "../CGameHandler.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/CStack.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/networkPacks/StackLocation.h"
namespace newHorizonsNecromancy
{
namespace
{
/// Select the nearest currently owned Necropolis by the same squared-distance
/// and first-on-tie convention used by the adventure spell town selector.
const CGTownInstance * nearestOwnedNecropolisTown(const PlayerState * ownerState, PlayerColor owner,
	const int3 & origin)
{
	if(!ownerState)
		return nullptr;

	const CGTownInstance * nearest = nullptr;
	ui32 nearestDistance = std::numeric_limits<ui32>::max();
	for(const auto * town : ownerState->getTowns())
	{
		if(!town || town->getOwner() != owner || town->getFactionID() != FactionID::NECROPOLIS)
			continue;

		const ui32 distance = town->visitablePos().dist2dSQ(origin);
		if(!nearest || distance < nearestDistance)
		{
			nearest = town;
			nearestDistance = distance;
		}
	}
	return nearest;
}


std::optional<RaisedArmyDelivery> delivery(const CArmedInstance & army,
	const NecromancyResult & summary, const CGTownInstance * town = nullptr)
{
	if(!summary.isReclaimerSummaryValid())
		throw std::runtime_error("Invalid Necromancy reclamation summary before delivery");
	if(!validRaisedArmyState(army))
		throw std::runtime_error("Invalid receiving army state during Necromancy resolution");
	const auto outputs = raisedOutputs(summary);
	if(!validRaisedArmyOutputs(outputs))
		throw std::runtime_error("Invalid Necromancy army output");
	auto plan = planRaisedArmy(army, outputs);
	if(!plan)
		return std::nullopt;
	auto accepted = summary;
	if(town && std::ranges::any_of(outputs, [](const auto & output) { return output.second > 0; }))
		accepted.ossuaryTown = town->id;
	return RaisedArmyDelivery{accepted, &army, std::move(*plan)};
}
}
std::optional<RaisedArmyDelivery> selectRaisedArmyDelivery(const CGHeroInstance & hero,
	const PlayerState * ownerState, const PreparedBattleNecromancy & prepared)
{
	const bool enhanced = prepared.enhanced.reclaimerZombiesRaised > 0;
	if(enhanced)
		if(auto selected = delivery(hero, prepared.enhanced))
			return selected;
	if(auto selected = delivery(hero, prepared.ordinary))
		return selected;
	if(!prepared.ossuary)
		return std::nullopt;
	const auto * town = nearestOwnedNecropolisTown(ownerState, hero.getOwner(), hero.visitablePos());
	const auto * army = town ? town->getUpperArmy() : nullptr;
	if(!army || army->getOwner() != hero.getOwner())
		return std::nullopt;
	if(enhanced)
		if(auto selected = delivery(*army, prepared.enhanced, town))
			return selected;
	return delivery(*army, prepared.ordinary, town);
}
void applyRaisedArmy(CGameHandler & handler, const CArmedInstance & army, const RaisedArmyPlan & plan)
{
	for(const auto & addition : plan)
	{
		const StackLocation location(army.id, addition.slot);
		const bool applied = army.hasStackAtSlot(addition.slot)
			? handler.changeStackCount(location, addition.count, ChangeValueMode::RELATIVE)
			: handler.insertNewStack(location, addition.creature.toCreature(), addition.count);
		// The simulation thread applies this preflighted plan without yielding.
		// A rejection is an internal error, not an ordinary capacity failure.
		if(!applied)
			throw std::runtime_error("Preflighted Necromancy army addition was rejected");
	}
}
}
