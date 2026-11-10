/*
 * ProjectedHeroBonusQueries.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "InitialExportBattleSubject.h"
#include "../../lib/bonuses/BonusList.h"
#include <stdexcept>

TConstBonusListPtr HypotheticBattle::visibleHeroBonuses(BattleSide side,
	const CSelector & selector, bool unstacked) const
{
	if(usesOwnedFrameQueries()) throw std::logic_error("Owned Hero policy queries require captured canonical dependencies");
	if(const auto owned = initialExportSubject())
	{
		owned->synchronizeValues(*this);
		return owned->battleGetHeroBonuses(side, selector, unstacked);
	}
	return BattleProxy::visibleHeroBonuses(side, selector, unstacked);
}

int HypotheticBattle::visibleHeroMorale(BattleSide side) const
{
	if(usesOwnedFrameQueries()) throw std::logic_error("Owned Hero policy queries require captured canonical dependencies");
	if(const auto owned = initialExportSubject())
	{
		owned->synchronizeValues(*this);
		return owned->battleGetHeroMorale(side);
	}
	return BattleProxy::visibleHeroMorale(side);
}

TConstBonusListPtr InitialExportBattleSubject::visibleHeroBonuses(BattleSide side,
	const CSelector & selector, bool unstacked) const
{
	const auto all = owned->projectedHeroBonuses(side, unstacked);
	auto selected = std::make_shared<BonusList>();
	all->getBonuses(*selected, selector);
	return selected;
}

int InitialExportBattleSubject::visibleHeroMorale(BattleSide side) const
{
	return owned->projectedHeroMorale(side);
}
