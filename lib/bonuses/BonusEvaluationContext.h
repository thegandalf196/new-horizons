/*
 * BonusEvaluationContext.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "BonusEnum.h"
#include "../constants/EntityIdentifiers.h"
#include <optional>
#include <vector>

class CBonusSystemNode;
class CStack;
class CCreature;
class CStackInstance;
class ProjectedBonusRoster;
struct Bonus;
namespace battle { class Unit; }

/// Explicit updater/limiter input, NOT a projected bonus graph collector.
/// Original typed hero/army/base identities stay live and immutable. Optional
/// battle-unit values are borrowed from the caller's matching sequential view.
/// Both identities/views must outlive evaluation under the world's access lease.
class DLL_LINKAGE BonusEvaluationContext final
{
	const CBonusSystemNode & original;
	const battle::Unit * projected = nullptr;
	const ProjectedBonusRoster * roster = nullptr;

public:
	/// Implicit adapter preserves existing live calls using a bonus node.
	BonusEvaluationContext(const CBonusSystemNode & node) noexcept;
	/// Requires matching unit ID; the enclosing owner binds battle/lifetimes.
	BonusEvaluationContext(const CStack & node, const battle::Unit & unit);
	BonusEvaluationContext(const CStack & node, const battle::Unit & unit, const ProjectedBonusRoster & roster);
	const CBonusSystemNode & originalNode() const noexcept;
	BonusNodeType getNodeType() const;
	PlayerColor getOwner() const;
	std::optional<int> stackCount() const;
	/// Preserve legacy stable base-instance levels; projected effective creature
	/// level applies only when the battle stack has no adventure base instance.
	std::optional<int> stackLevel() const;
	const battle::Unit * battleUnit() const;
	const CStackInstance * stackInstance() const;
	const CCreature * creature() const;
	std::optional<FactionID> faction() const;
	/// Projected adjacency requires complete roster membership; never live-falls-back.
	std::vector<const battle::Unit *> adjacentUnits() const;
	/// Projected acceptance requires a CUnitState view (not a generic Unit wrapper).
	bool acceptsBonus(const Bonus & bonus) const;
};
