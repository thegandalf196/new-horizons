/*
 * ProjectedAdjacencyBonuses.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "../../lib/bonuses/IBonusBearer.h"
#include <memory>
#include <string>
#include <vector>

class CBattleInfoCallback;
class StackWithBonuses;
class CStack;
class HypotheticBattle;
struct ProjectedAdjacencySources;
struct ProjectedAdjacencyQuery;
struct ProjectedAdjacencyBaseline;
struct ProjectedAdjacencyRoster;
namespace battle { class Unit; }

/// Current native recipient operations: authorized visible exports plus opaque
/// numeric current-effect operations for exact native source occurrences.
/// Does not import a world, expose unknown sources or own the native lifetime.
class ProjectedAdjacencyBonuses
{
public:
	static std::shared_ptr<const ProjectedAdjacencySources> captureSources(const CBattleInfoCallback & source);
	/// Null retains the exact ordinary/frozen/foreign-provider query route.
	static TConstBonusListPtr query(const StackWithBonuses & unit, const CSelector & selector,
		const std::string & cachingStr, bool unstacked);
	/// Only the raw-native-record reevaluation lane is type-preserving. Opaque
	/// numeric operations, rebound sources and owned exports retain full queries.
	/// Partial adjacency proof only; the caller must prove recipient/raw ancestry.
	static DLL_LINKAGE bool canProveTypeAbsence(const StackWithBonuses & unit);
	/// The branch's unit-state epoch: advanced by every roster/bonus mutator.
	static uint64_t unitStateEpoch(const HypotheticBattle & branch) noexcept;
	/// Whether query() may re-evaluate limiters against this branch's roster (may say yes when it will not).
	static bool projectsRoster(const HypotheticBattle & branch);
private:
	static const CStack * nativeSource(const StackWithBonuses & unit);
	static std::shared_ptr<const ProjectedAdjacencySources> sourcesFor(const HypotheticBattle & owner);
	/// The branch's whole unit roster (ProjectedAdjacencyRoster.cpp).
	static std::shared_ptr<const ProjectedAdjacencyRoster> cachedRoster(const HypotheticBattle & branch);
	/// The selector-independent baseline; ProjectedAdjacencyMemoSlot reuses it.
	static ProjectedAdjacencyBaseline projectedBaseline(const StackWithBonuses & unit, const CStack & native,
		const std::shared_ptr<const ProjectedAdjacencySources> & sources, const std::vector<const battle::Unit *> & units);
};
