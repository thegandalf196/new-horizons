/*
 * ProjectedSummonHealthQuery.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once
#include "../constants/EntityIdentifiers.h"
#include "BattleSide.h"
#include "UnitQueryTerrain.h"
#include <memory>

class CStack;
class CCreature;
class CStackBasicDescriptor;
class CArmedInstance;
class ProjectedBonusGraph;
class ProjectedBonusRoster;
namespace battle { class CUnitStateDetached; }

namespace newHorizonsElementalRebirth
{
/// Retained count-one/-1/summoned hypothetical, NEVER a battlefield roster entry.
/// Caller owns this stable object until graph AND any finalized plan are retired.
/// Keep it in the owning world arena; do not destroy it after reading maximum().
/// All borrowed source nodes and immutable Bonus payloads obey that same lease.
/// Invalid creature/side returns null without events, exactly like native query.
/// Any preparation exception poisons the graph before private identities die;
/// abandon the entire world. No live edges/counters, no rollback or publication.
class DLL_LINKAGE ProjectedSummonHealthQuery final : boost::noncopyable
{
	std::unique_ptr<CStack> original;
	std::unique_ptr<battle::CUnitStateDetached> state;
	std::unique_ptr<ProjectedBonusRoster> queryRoster;
	int32_t value = 0;
	ProjectedSummonHealthQuery(const CStackBasicDescriptor & descriptor, PlayerColor owner, BattleSide side,
		UnitQueryTerrain terrain);
	void prepare(ProjectedBonusGraph & graph, const CArmedInstance * army, const CCreature & creature);
	static std::unique_ptr<ProjectedSummonHealthQuery> createImpl(ProjectedBonusGraph & graph,
		const CArmedInstance * army, CreatureID type, PlayerColor owner, BattleSide side,
		std::optional<UnitQueryTerrain> terrain);
public:
	~ProjectedSummonHealthQuery();
	static std::unique_ptr<ProjectedSummonHealthQuery> create(ProjectedBonusGraph & graph,
		const CArmedInstance * sourceArmy, CreatureID creature, PlayerColor owner, BattleSide side);
	static std::unique_ptr<ProjectedSummonHealthQuery> create(ProjectedBonusGraph & graph,
		const CArmedInstance * sourceArmy, CreatureID creature, PlayerColor owner, BattleSide side,
		UnitQueryTerrain terrain);
	int32_t maximum() const noexcept;
	/// Explicit mutable finalizer authority for this privately owned identity.
	CStack & identity() const noexcept;
};
}
