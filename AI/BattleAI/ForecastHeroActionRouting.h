/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "ForecastHeroActionAdmission.h"
#include "HypotheticBattle.h"
namespace forecast::heroAction
{
/// Whole-closure route is selected BEFORE a candidate branch/action prefix.
/// Reference descendants keep their CURRENT source and cannot re-admit compact.
DLL_LINKAGE std::shared_ptr<HypotheticBattle> makeBranch(const Environment *, HypotheticBattle::Subject,
	std::shared_ptr<battleSearch::SearchBudget>, Route);
}
