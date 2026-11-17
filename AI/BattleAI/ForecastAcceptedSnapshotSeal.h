/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "../../Global.h"
#include <memory>
#include <vector>
class AttackPossibility;
class VisibleCandidateWorld;
class HypotheticBattle;
namespace battle { class CUnitState; }
namespace forecast
{
class State;
/// Producer provenance only. Retains exact produced object/ownership identities and
/// source owners; shared pointer identity does NOT freeze mutable contents.
class DLL_LINKAGE AcceptedSnapshotSeal final
{
	friend class ::VisibleCandidateWorld;
	const std::shared_ptr<const State> source;
	const std::shared_ptr<const VisibleCandidateWorld> query;
	const std::shared_ptr<HypotheticBattle> effects;
	const std::shared_ptr<battle::CUnitState> actor;
	const std::vector<std::shared_ptr<battle::CUnitState>> recipients;
	explicit AcceptedSnapshotSeal(const AttackPossibility &);
public:
	bool matches(const AttackPossibility &) const noexcept;
	bool contains(const battle::CUnitState &) const noexcept;
};
}
