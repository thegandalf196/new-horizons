/*
 * PreparedBattleUnitSidecars.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once
#include <memory>
#include <boost/noncopyable.hpp>

class BattleInfo;
namespace battle
{
class ProjectedBattleWorldArena;
class PreparedBattleWorldCommit;

/// Nonrouting storage participant, NOT a complete packet transaction.
/// Capture originates at arena binding, not construction of this participant.
/// Caller retains BOTH owners and real exclusive/nonreentrant world ownership
/// through readiness, common commit and controlled retirement. Original values,
/// source metadata and borrowed pointees must not change via external aliases.
/// Final projected unit/reference inputs must stay stable too: the common owner
/// freezes and verifies those separate participants, not this storage inventory.
/// Validate ALL participants before graph CAS; no later refusal is permissible
/// under that actual ownership interval. This class does not acquire a lease.
class DLL_LINKAGE PreparedBattleUnitSidecars final : boost::noncopyable
{
public:
	explicit PreparedBattleUnitSidecars(BattleInfo & destination, const ProjectedBattleWorldArena & arena);
	~PreparedBattleUnitSidecars();
	bool readyToCommit() const noexcept;
	/// Refuses stale/single-use plans before writes. Changed storage is swapped;
	/// displaced allocations remain owned here until controlled retirement.
	/// No setter, query, callback, graph event, allocation or destruction follows
	/// the first write. No live unit/root/value/graph/replay transfer is performed.
	bool commit() noexcept;
private:
	friend class PreparedBattleWorldCommit;
	void exchangePreparedStorage() noexcept;
	struct Data;
	std::unique_ptr<Data> impl;
};
}
