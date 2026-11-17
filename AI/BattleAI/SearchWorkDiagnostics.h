/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#ifdef NH_PERF_EXPERIMENTS
#include "SearchBudget.h"
#include "BattleSearchPolicy.h"
#include <cassert>
#include <limits>
namespace battleSearch
{
/// Diagnostic ROOT totals, never a reserved child's quota. No remaining-work
/// field: retained AP handles can defer reservation release past search finish.
struct SearchWorkSummary
{
	std::array<uint64_t, workKindCount> spent{}, refusals{};
	uint64_t startedDecisions = 0, completedFiniteDecisions = 0;
	uint64_t unsupportedDecisions = 0, missingRootDecisions = 0;
	uint64_t fullDecisions = 0, beamOneDecisions = 0, fastDecisions = 0, legacyDecisions = 0;
	uint32_t profileMask = 0; // Full=1, BeamOne=2, Fast=4, LegacyUnlimited=8.
	bool saturated = false;
	void add(uint64_t & destination, uint64_t value) noexcept
	{
		const auto room = std::numeric_limits<uint64_t>::max() - destination;
		if(value > room) { destination += room; saturated = true; }
		else destination += value;
	}
	void accumulate(const SearchWorkSummary & other) noexcept
	{
		for(size_t i = 0; i < workKindCount; ++i)
		{
			add(spent[i], other.spent[i]);
			add(refusals[i], other.refusals[i]);
		}
		for(auto field : {&SearchWorkSummary::startedDecisions, &SearchWorkSummary::completedFiniteDecisions,
			&SearchWorkSummary::unsupportedDecisions, &SearchWorkSummary::missingRootDecisions,
			&SearchWorkSummary::fullDecisions, &SearchWorkSummary::beamOneDecisions,
			&SearchWorkSummary::fastDecisions, &SearchWorkSummary::legacyDecisions}) add((*this).*field, other.*field);
		profileMask |= other.profileMask;
		saturated |= other.saturated;
	}
};
/// Callback-local owner, retained before evaluator construction can fail. Finish
/// explicitly after nested search calls return, never from a destructor callback.
class RootSearchWorkCapture
{
	std::shared_ptr<SearchBudget> owner;
	SearchWorkSummary totals;
	bool active = false, finiteExpected = false;
public:
	const std::shared_ptr<SearchBudget> & begin(Profile profile, const std::optional<SearchLimits> & limits)
	{
		assert(!active);
		finiteExpected = profile != Profile::LegacyUnlimited;
		owner = limits ? std::make_shared<SearchBudget>(limits->decision) : nullptr;
		active = true;
		totals.add(totals.startedDecisions, 1);
		switch(profile)
		{
		case Profile::Full: totals.profileMask |= 1; totals.add(totals.fullDecisions, 1); break;
		case Profile::BeamOne: totals.profileMask |= 2; totals.add(totals.beamOneDecisions, 1); break;
		case Profile::BeamOneTwoActors: totals.profileMask |= 4; totals.add(totals.fastDecisions, 1); break;
		case Profile::LegacyUnlimited: totals.profileMask |= 8; totals.add(totals.legacyDecisions, 1); break;
		}
		return owner;
	}
	void complete() noexcept
	{
		if(!active) return;
		active = false;
		if(!owner)
		{
			totals.add(finiteExpected ? totals.missingRootDecisions : totals.unsupportedDecisions, 1);
			return;
		}
		const auto spent = owner->spent();
		const auto refused = owner->refusals();
		for(size_t i = 0; i < workKindCount; ++i)
		{
			totals.add(totals.spent[i], spent[i]);
			totals.add(totals.refusals[i], refused[i]);
			// The source counter saturates; its maximum is ambiguous, never
			// report an apparently exact successful comparison at that boundary.
			if(refused[i] == std::numeric_limits<uint64_t>::max()) totals.saturated = true;
		}
		totals.add(totals.completedFiniteDecisions, 1);
	}
	const SearchWorkSummary & summary() const noexcept { return totals; }
};
}
#endif
