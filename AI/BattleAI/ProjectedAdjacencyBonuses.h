/*
 * ProjectedAdjacencyBonuses.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "../../lib/bonuses/IBonusBearer.h"
#include <memory>
#include <string>

class CBattleInfoCallback;
class StackWithBonuses;
class CStack;
class HypotheticBattle;
struct ProjectedAdjacencySources;
struct ProjectedAdjacencyQuery;

/// Ordinary recipient projection of actual, visible Pasis exports only.
/// Does not import a world, expose unknown sources or own the native lifetime.
class ProjectedAdjacencyBonuses
{
public:
	static std::shared_ptr<const ProjectedAdjacencySources> captureSources(const CBattleInfoCallback & source);
	/// Null retains the exact ordinary/frozen/foreign-provider query route.
	static TConstBonusListPtr query(const StackWithBonuses & unit, const CSelector & selector,
		const std::string & cachingStr, bool unstacked);
private:
	static const CStack * nativeSource(const StackWithBonuses & unit);
	static std::shared_ptr<const ProjectedAdjacencySources> sourcesFor(const HypotheticBattle & owner);
};
