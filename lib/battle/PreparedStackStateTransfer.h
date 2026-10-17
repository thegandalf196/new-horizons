/*
 * PreparedStackStateTransfer.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include <cstdint>
#include <memory>
#include <boost/noncopyable.hpp>

class CCreature;
class CStack;
class ProjectedBonusGraph;
class IBonusBearer;
namespace battle
{
class CUnitStateDetached;
class PreparedBattleUnitRoots;
class PreparedBattleWorldCommit;

/// Value/form-view phase ONLY, not a graph/world transaction or UPDATE route.
/// Caller retains destination, private state/graph/arena, registry and immutable
/// creature metadata under real exclusive nonreentrant ownership through teardown.
/// Check ALL world participants before the first write; after successful graph
/// generation reservation, perform this no-callback phase without reentrancy.
/// Graph finalization must have staged the exact form events/edges; this class
/// deliberately neither invokes a form hook nor changes graph/counter/cache data.
/// The registry must retain its creature handles until this plan is destroyed:
/// retained handles keep sources alive but are never a substitute for world/root
/// lifetime ownership. Commit does not release handles or destroy graph nodes.
class DLL_LINKAGE PreparedStackStateTransfer final : boost::noncopyable
{
public:
	PreparedStackStateTransfer(CStack & destination, CUnitStateDetached & prepared,
		ProjectedBonusGraph & graph);
	/// Same pure monotonicity/wide-arithmetic check consumed by construction.
	/// Validates metadata only; never changes a unit or authorizes graph use.
	static int32_t checkedFinalRevision(int32_t originalRevision, int32_t boundRevision, int32_t projectedRevision);
	bool readyToCommit() const noexcept;
	/// Refuses stale/consumed metadata before any value/view write. Other value
	/// mutations are excluded by the caller's real ownership interval, not a token.
	bool commit() noexcept;
private:
	friend class PreparedBattleUnitRoots;
	friend class PreparedBattleWorldCommit;
	void exchangePrepared() noexcept;
	CStack & destination;
	CUnitStateDetached & prepared;
	const IBonusBearer * provider;
	std::shared_ptr<const CCreature> nativeSource;
	std::shared_ptr<const CCreature> oldSource;
	std::shared_ptr<const CCreature> newSource;
	int32_t nativeIndex;
	int32_t oldIndex;
	int32_t newIndex;
	int32_t originalRevision;
	int32_t projectedRevision;
	int32_t finalRevision;
	int32_t finalCreature;
	int32_t finalOriginalCreature;
	bool finalActive;
	bool originalWidth;
	bool finalWidth;
	bool consumed = false;
};
}
