/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#ifdef NH_PERF_EXPERIMENTS
#include "../../AI/BattleAI/BattleAI.h"
#include <gtest/gtest.h>
#include <array>
#include <string>
namespace nativeBenchmark
{
/// Scalars only, after encounter timing stops. Absent summary/diagnostic build
/// emits no fields; comparison drivers must refuse missing unsupported metrics.
inline void recordSearchWorkDiagnostics(const std::string & prefix, const CBattleAI::TimingSummary & timing)
{
	const auto record = [&](const char * suffix, auto value)
	{ ::testing::Test::RecordProperty(prefix + suffix, std::to_string(value)); };
	record("DiagnosticsVersion", 1);
	record("DiagnosticsSupported", 1);
	const auto & budget = timing.searchWork;
	record("BudgetStartedDecisions", budget.startedDecisions);
	record("BudgetCompletedFiniteDecisions", budget.completedFiniteDecisions);
	record("BudgetUnsupportedDecisions", budget.unsupportedDecisions);
	record("BudgetMissingRootDecisions", budget.missingRootDecisions);
	record("BudgetFullDecisions", budget.fullDecisions);
	record("BudgetBeamOneDecisions", budget.beamOneDecisions);
	record("BudgetFastDecisions", budget.fastDecisions);
	record("BudgetLegacyDecisions", budget.legacyDecisions);
	record("BudgetProfileMask", budget.profileMask);
	record("BudgetSaturated", budget.saturated);
	static_assert(battleSearch::workKindCount == 5, "Extend diagnostic XML labels for new work kinds");
	constexpr std::array<const char *, battleSearch::workKindCount> kinds{
		"Generation", "Scout", "Continuation", "Node", "Movement"};
	for(size_t i = 0; i < kinds.size(); ++i)
	{
		::testing::Test::RecordProperty(prefix + "BudgetSpent" + kinds[i], std::to_string(budget.spent[i]));
		::testing::Test::RecordProperty(prefix + "BudgetRefusals" + kinds[i], std::to_string(budget.refusals[i]));
	}
	const auto & forecast = timing.forecast;
	record("ForecastRootCaptures", forecast.rootCaptures);
	record("ForecastRootAdmissions", forecast.rootAdmissions);
	record("ForecastSourceFallbacks", forecast.sourceFallbacks);
	record("ForecastLifecycleFallbacks", forecast.lifecycleFallbacks);
	record("ForecastReferenceOverrides", forecast.referenceOverrides);
	record("ForecastForks", forecast.forks);
	record("ForecastCommittedTransitions", forecast.committedTransitions);
	record("ForecastRefusedTransitions", forecast.refusedTransitions);
	record("ForecastDetachedHealthUpdates", forecast.detachedHealthUpdates);
	record("ForecastNeighborhoodInvalidations", forecast.neighborhoodInvalidations);
	record("ForecastCompactFactHits", forecast.compactFactHits);
	record("ForecastNativeReadMaterializations", forecast.nativeReadMaterializations);
	record("ForecastNativeOwningLoans", forecast.nativeOwningLoans);
	record("ForecastNativeQueryRebindings", forecast.nativeQueryRebindings);
	record("ForecastCompactFactMisses", forecast.compactFactMisses);
	record("ForecastDerivedRefreshes", forecast.derivedRefreshes);
	record("ForecastSaturated", forecast.saturated);
}
}
#endif
