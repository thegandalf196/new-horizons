/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "FrozenScoringBonuses.h"
#include "BonusList.h"
#include "../battle/NewHorizonsDiscipline.h"
namespace frozenScoring
{
TConstBonusListPtr query(const View & source, const CSelector & selector, bool unstacked)
{
	auto ret = std::make_shared<BonusList>();
	if(unstacked)
	{
		if(source.unstacked)
			for(const auto & bonus : *source.unstacked)
				if(bonus && selector(bonus.get())) ret->push_back(bonus);
	}
	else
		for(const auto & bonus : source.stacked)
			if(selector(&bonus)) ret->push_back(std::make_shared<Bonus>(bonus));
	if(!unstacked) ret->stackBonuses();
	return newHorizonsDiscipline::veteranCohesionBonuses(ret, selector, source.veteranCohesionEarned);
}
}
