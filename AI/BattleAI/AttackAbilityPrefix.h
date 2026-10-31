/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <vector>
#include "../../lib/constants/EntityIdentifiers.h"
class CBattleInfoCallback;
namespace battle { class Unit; }
namespace attackAbilityPrefix
{
struct Query
{
	bool adverseFactVisible = false;
	double initialNominalRerollAvailability = 0;
	double nominalRerollAvailability = 0;
	/// Current eligibility is exact; counterfactual effects of other legacy casts are unvalued.
	bool eligibilityStableForPoison = true;
	std::vector<SpellID> eligibleHarmfulAttempts;
};
/// Read-only native selection/range/target query; never casts, rolls, imports bias or changes a resource.
Query query(const CBattleInfoCallback & battle, const battle::Unit & source, const battle::Unit & target, bool ranged);
}
