/*
 * ProjectedAdjacencyRoster.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include <cstdint>
#include <memory>
#include <vector>

class CBattleInfoCallback;
class HypotheticBattle;
namespace battle { class Unit; class CUnitState; }

/// A branch's whole unit roster, exactly as battleGetUnitsIf with no filter
/// returns it, with each unit's typed state (null for a foreign unit kind).
struct ProjectedAdjacencyRoster
{
	std::vector<const battle::Unit *> units;
	std::vector<const battle::CUnitState *> states;
};

/// Never-saved per-branch cache of the adjacency query roster. It is reused
/// while a digest of every branch level's unit map (identities and IDs), the
/// chain of subjects and the native root's bonus-tree version are unchanged;
/// the roster is a function of exactly those. Branches with owned, compact or
/// initial-export routing are never cached. Copies start empty.
/// NH_AI_ADJACENCY_ROSTER=0 turns reuse off; NH_AI_ADJACENCY_ROSTER_CHECK=1
/// compares every reuse with a fresh roster and aborts on a difference.
class ProjectedAdjacencyRosterSlot
{
public:
	struct Level
	{
		const CBattleInfoCallback * subject = nullptr;
		const HypotheticBattle * parent = nullptr;
	};
	ProjectedAdjacencyRosterSlot() = default;
	ProjectedAdjacencyRosterSlot(const ProjectedAdjacencyRosterSlot &) noexcept {}
	ProjectedAdjacencyRosterSlot & operator=(const ProjectedAdjacencyRosterSlot &) noexcept
	{
		reset();
		return *this;
	}
	void reset() noexcept
	{
		roster.reset();
		levels.clear();
	}
	/// Subject classification remembered from the last build, by chain depth.
	mutable std::vector<Level> levels;
	mutable uint64_t digest = 0;
	mutable std::shared_ptr<const ProjectedAdjacencyRoster> roster;
};
