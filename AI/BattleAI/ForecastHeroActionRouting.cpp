/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastHeroActionRouting.h"
#include "ForecastNativeAdapter.h"
#include <stdexcept>
namespace forecast::heroAction
{
std::shared_ptr<HypotheticBattle> makeBranch(const Environment * env, HypotheticBattle::Subject source,
	std::shared_ptr<battleSearch::SearchBudget> work, Route route)
{
	if(!source) throw std::invalid_argument("Hero forecast requires its current source");
	const auto * parent = dynamic_cast<const HypotheticBattle *>(source.get());
	if(route == Route::CurrentNativeReference && parent && parent->forecastState())
		throw std::logic_error("Unsupported nested closure was not preflighted before its original prefix");
	auto result = std::make_shared<HypotheticBattle>(env, source,
		std::shared_ptr<const battle::OwnedBattleFrameBatch>{}, std::move(work));
	if(route == Route::CurrentNativeReference || (parent && parent->referenceForecastOnly()))
		result->useReferenceForecast();
	else if(parent) NativeAdapter::forkExchange(*parent, *result);
	return result;
}
}
