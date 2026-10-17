/*
 * PreparedBattleUnitRoots.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include <memory>
#include <boost/noncopyable.hpp>

class BattleInfo;
class CStack;
namespace battle
{
class ProjectedBattleWorldArena;
class PreparedBattleWorldCommit;
struct BattleUnitRootIdentity;
struct BattleUnitRootOrigin;

/// Physical root ownership participant ONLY; no authoritative UPDATE route.
/// Construct after ordered private edits, BEFORE graph-storage preparation.
/// The caller retains battle, arena, library and exact mutable graph closure
/// under real exclusive/nonreentrant ownership through all participant retirement.
/// Root facts are captured at ARENA binding, not recaptured by this constructor.
/// Other unit/graph inputs require the common owner's unchanged-input contract.
/// ADD identities remain cold until the prepared graph and values can be installed
/// together. Query-only Rebirth identities never enter these battlefield roots.
class DLL_LINKAGE PreparedBattleUnitRoots final : boost::noncopyable
{
public:
	PreparedBattleUnitRoots(BattleInfo & destination, ProjectedBattleWorldArena & arena);
	~PreparedBattleUnitRoots();
	/// Pre-CAS only. Never call after the common graph generation reservation.
	bool readyToCommit() const noexcept;
	/// Checked NONROUTING storage entry: caller excludes mutation/reentrancy for
	/// this entire call. This alone does not install graph, existing values,
	/// sidecars or replay; do not expose native queries/publication afterward.
	bool commit() noexcept;
private:
	friend class ProjectedBattleWorldArena;
	friend class PreparedBattleWorldCommit;
	struct Data;
	std::unique_ptr<Data> impl;
	static BattleUnitRootIdentity captureIdentity(const CStack & original) noexcept;
	static std::unique_ptr<const BattleUnitRootOrigin> captureOrigin(BattleInfo & source);
	/// Future common coordinator ONLY, after ALL readiness checks and graph CAS.
	/// No query, allocation, callbacks, object destruction or generation reads.
	void exchangePreparedStorage() noexcept;
};
}
