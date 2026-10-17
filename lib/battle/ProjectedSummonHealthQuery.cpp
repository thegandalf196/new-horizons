/*
 * ProjectedSummonHealthQuery.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "ProjectedSummonHealthQuery.h"
#include "SummonHealthQuery.h"
#include "UnitStateValueFields.h"
#include "../CStack.h"
#include "../bonuses/ProjectedBonusGraph.h"
#include "../bonuses/ProjectedBonusRoster.h"
#include "../mapObjects/army/CArmedInstance.h"
#include "../mapObjects/army/CStackBasicDescriptor.h"

namespace newHorizonsElementalRebirth
{
ProjectedSummonHealthQuery::ProjectedSummonHealthQuery(const CStackBasicDescriptor & descriptor,
	PlayerColor owner, BattleSide side, UnitQueryTerrain terrain)
	: original(std::make_unique<CStack>(&descriptor, owner, -1, side, SlotID::SUMMONED_SLOT_PLACEHOLDER, true, terrain))
{
	original->summoned = true;
	original->natureSummoned = false;
}

ProjectedSummonHealthQuery::~ProjectedSummonHealthQuery() = default;

std::unique_ptr<ProjectedSummonHealthQuery> ProjectedSummonHealthQuery::create(ProjectedBonusGraph & graph,
	const CArmedInstance * army, CreatureID id, PlayerColor owner, BattleSide side)
{
	return createImpl(graph, army, id, owner, side, std::nullopt);
}

std::unique_ptr<ProjectedSummonHealthQuery> ProjectedSummonHealthQuery::create(ProjectedBonusGraph & graph,
	const CArmedInstance * army, CreatureID id, PlayerColor owner, BattleSide side, UnitQueryTerrain terrain)
{
	return createImpl(graph, army, id, owner, side, terrain);
}

std::unique_ptr<ProjectedSummonHealthQuery> ProjectedSummonHealthQuery::createImpl(ProjectedBonusGraph & graph,
	const CArmedInstance * army, CreatureID id, PlayerColor owner, BattleSide side,
	std::optional<UnitQueryTerrain> terrain)
{
	std::unique_ptr<ProjectedSummonHealthQuery> result;
	try
	{
		if(!id.hasValue())
			return nullptr;
		const auto * creature = id.toCreature();
		if(!creature || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER))
			return nullptr;
		const auto capturedTerrain = terrain ? *terrain : captureUnitQueryTerrain(army);
		CStackBasicDescriptor descriptor(creature, 1);
		result.reset(new ProjectedSummonHealthQuery(descriptor, owner, side, capturedTerrain));
		result->prepare(graph, army, *creature);
		return result;
	}
	catch(...)
	{
		// result is STILL alive here, so poison before its captured key expires.
		graph.abandonPreparation();
		throw;
	}
}

void ProjectedSummonHealthQuery::prepare(ProjectedBonusGraph & graph, const CArmedInstance * army,
	const CCreature & creature)
{
	state = std::make_unique<battle::CUnitStateDetached>(original.get(), &graph.view(*original));
	battle::UnitStateValueFields::copy(*state, *original); // exact constructor count/provenance, no localInit/reset
	queryRoster = std::make_unique<ProjectedBonusRoster>(std::vector<const battle::Unit *>{state.get()},
		ProjectedBonusRoster::Identity::OrderedOccurrences);
	graph.bindColdOccurrence(*original, *state, *queryRoster, nullptr); // explicitly NO battle/environment
	if(army)
		graph.attachToSource(*original, *army);
	graph.attachToSource(*original, creature);
	value = querySummonMaxHP(*state); // SAME numeric formula and typed nested projected queries
	graph.detachFromAll(*original); // exact front source order: army, then creature
}

int32_t ProjectedSummonHealthQuery::maximum() const noexcept { return value; }
CStack & ProjectedSummonHealthQuery::identity() const noexcept { return *original; }
}
