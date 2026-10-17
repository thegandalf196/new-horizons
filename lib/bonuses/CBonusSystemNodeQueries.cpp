/*
 * CBonusSystemNodeQueries.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "CBonusSystemNode.h"
#include "NativeBonusGraphAccess.h"
#include "Updaters.h"
#include "BonusLimitation.h"

constexpr bool cachingEnabled = true;

TConstBonusListPtr CBonusSystemNode::getAllBonuses(const CSelector &selector, const std::string &cachingStr) const
{
	NativeBonusGraphAccess graphAccess;
	if (cachingEnabled)
	{
		// If a bonus system request comes with a caching string then look up in the map if there are any
		// pre-calculated bonus results. Limiters can't be cached so they have to be calculated.
		if (cachedLast == nodeChanged && !cachingStr.empty())
		{
			RequestsMap::const_accessor accessor;

			//Cached list contains bonuses for our query with applied limiters
			if (cachedRequests.find(accessor, cachingStr) && accessor->second.first == cachedLast)
				return accessor->second.second;
		}

		//We still don't have the bonuses (didn't returned them from cache)
		//Perform bonus selection
		auto ret = std::make_shared<BonusList>();

		if (cachedLast == nodeChanged)
		{
			// Cached bonuses are up-to-date - use shared/read access and compute results
			std::shared_lock lock(sync);
			cachedBonuses.getBonuses(*ret, selector);
		}
		else
		{
			// If the bonus system tree changes(state of a single node or the relations to each other) then
			// cache all bonus objects. Selector objects doesn't matter.
			std::lock_guard lock(sync);
			if (cachedLast == nodeChanged)
			{
				// While our thread was waiting, another one have updated bonus tree. Use cached bonuses.
				cachedBonuses.getBonuses(*ret, selector);
			}
			else
			{
				// Cached bonuses may be outdated - regenerate them
				rebuildBonusCache();
				cachedBonuses.getBonuses(*ret, selector);
			}
		}

		// Save the results in the cache
		if (!cachingStr.empty())
		{
			RequestsMap::accessor accessor;
			if (cachedRequests.find(accessor, cachingStr))
			{
				accessor->second.second = ret;
				accessor->second.first = cachedLast;
			}
			else
				cachedRequests.emplace(cachingStr, std::pair<int32_t, TBonusListPtr>{ cachedLast, ret });
		}

		return ret;
	}
	else
	{
		return getAllBonusesWithoutCaching(selector);
	}
}

TConstBonusListPtr CBonusSystemNode::getUnstackedBonuses(const CSelector & selector) const
{
	NativeBonusGraphAccess graphAccess;
	auto ret = std::make_shared<BonusList>();
	if(cachingEnabled)
	{
		if(cachedLast == nodeChanged)
		{
			std::shared_lock lock(sync);
			cachedUnstackedBonuses.getBonuses(*ret, selector);
		}
		else
		{
			std::lock_guard lock(sync);
			if(cachedLast == nodeChanged)
				cachedUnstackedBonuses.getBonuses(*ret, selector);
			else
			{
				rebuildBonusCache();
				cachedUnstackedBonuses.getBonuses(*ret, selector);
			}
		}
		return ret;
	}

	return getUnstackedBonusesWithoutCaching(selector);
}

TConstBonusListPtr CBonusSystemNode::getAllBonusesWithoutCaching(const CSelector &selector) const
{
	auto ret = std::make_shared<BonusList>();

	// Get bonus results without caching enabled.
	BonusList beforeLimiting;
	BonusList afterLimiting;
	getAllBonusesRec(beforeLimiting);
	limitBonuses(beforeLimiting, afterLimiting);
	afterLimiting.getBonuses(*ret, selector);
	ret->stackBonuses();
	return ret;
}

TConstBonusListPtr CBonusSystemNode::getUnstackedBonusesWithoutCaching(const CSelector & selector) const
{
	BonusList allBonuses;
	BonusList limitedBonuses;
	getAllBonusesRec(allBonuses);
	limitBonuses(allBonuses, limitedBonuses);
	auto ret = std::make_shared<BonusList>();
	limitedBonuses.getBonuses(*ret, selector);
	return ret;
}

void CBonusSystemNode::rebuildBonusCache() const
{
	BonusList allBonuses;
	cachedBonuses.clear();
	cachedUnstackedBonuses.clear();
	getAllBonusesRec(allBonuses);
	limitBonuses(allBonuses, cachedUnstackedBonuses);
	cachedUnstackedBonuses.getAllBonuses(cachedBonuses);
	cachedBonuses.stackBonuses();
	cachedLast = nodeChanged;
}

std::shared_ptr<Bonus> CBonusSystemNode::getUpdatedBonus(const std::shared_ptr<Bonus> & b, const TUpdaterPtr & updater) const
{
	assert(updater);
	return updater->createUpdatedBonus(b, * this);
}
