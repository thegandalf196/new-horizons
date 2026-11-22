/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "SearchBudget.h"
#include "../../lib/CStack.h"
#include <optional>
#include <string>
#include <stdexcept>

namespace battleAI
{
/// Runs one hypothetical evaluation of a live activation. A forecast invariant
/// failure inside it must not end the live battle: the candidates it was
/// scoring are discarded with an error record and `recover` supplies the
/// caller's legal fallback. Budget exhaustion keeps its own handling, and a
/// failure after a submission began (recover returns nothing) is rethrown.
template<typename Evaluate, typename Recover>
auto recoverEvaluation(const char * stage, const CStack * stack, Evaluate && evaluate, Recover && recover)
	-> decltype(evaluate())
{
	try
	{
		return evaluate();
	}
	catch(const battleSearch::BudgetExhausted &)
	{
		throw;
	}
	catch(const std::exception & error)
	{
		auto fallback = recover();
		if(!fallback)
			throw;
		logAi->error("BattleAI %s failed for %s and was discarded: %s", stage,
			stack ? stack->nodeName() : std::string("no stack"), error.what());
		return *std::move(fallback);
	}
}
}
