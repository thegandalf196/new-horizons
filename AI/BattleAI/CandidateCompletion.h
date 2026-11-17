/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "SearchBudget.h"
#include <limits>
#include <utility>
namespace battleSearch
{
/// Scope finalizers may finish accepted terminal candidates. A work limit must
/// discard that incomplete score rather than escape an implicit-noexcept dtor.
/// Other exceptions retain their ordinary error contract.
template<typename Completion>
bool completeCandidate(float & score, Completion && completion)
{
	try
	{
		std::forward<Completion>(completion)();
		return true;
	}
	catch(const BudgetExhausted &)
	{
		score = std::numeric_limits<float>::lowest();
		return false;
	}
}
}
