/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "ForecastState.h"
#include "ForecastNativePreparation.h"
#include <memory>
#include <utility>
#include <vector>
class HypotheticBattle;
class StackWithBonuses;
namespace battleSearch { class SearchBudget; }
namespace forecast
{
/// Call-local transaction only. Native staging addresses never enter State,
/// escape the synchronous round boundary or replace registered unit pointers.
struct PreparedEffectRound
{
	State state;
	std::unique_ptr<NativePreparation> preparation;
	std::shared_ptr<HypotheticBattle> future;
	std::vector<std::pair<std::shared_ptr<StackWithBonuses>, std::shared_ptr<StackWithBonuses>>> peers;
	std::shared_ptr<battleSearch::SearchBudget> work;
	std::shared_ptr<const void> expectedLayer;
	explicit PreparedEffectRound(State initial) : state(std::move(initial)) {}
};
}
