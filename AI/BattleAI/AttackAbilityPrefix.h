/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <vector>
#include <compare>
#include <optional>
#include <set>
#include "../../lib/constants/EntityIdentifiers.h"
class CBattleInfoCallback;
class Bonus;
namespace battle { class Unit; }
namespace attackAbilityPrefix
{
struct StatusKey
{
	uint32_t target = 0;
	SpellID spell;
	std::vector<const Bonus *> records;
	auto operator<=>(const StatusKey &) const = default;
};
/// Only already successful current status attempts can suppress a repeated roll.
bool alreadyApplied(const std::set<StatusKey> &, const StatusKey &);
struct Attempt
{
	SpellID spell;
	int chancePercent = 0;
	double nominalSuccess = 0;
	bool harmful = false;
	bool specialty = false;
	int duration = 0;
	std::optional<StatusKey> condition;
};
struct Query
{
	bool adverseFactVisible = false;
	double initialNominalRerollAvailability = 0;
	double nominalRerollAvailability = 0;
	/// Current eligibility is exact; counterfactual effects of other legacy casts are unvalued.
	bool eligibilityStableForPoison = true;
	std::vector<SpellID> eligibleHarmfulAttempts;
	std::vector<Attempt> attempts;
	double stunSuccess = 0;
	int stunChancePercent = 0;
	std::optional<StatusKey> stunCondition;
};
/// Read-only native selection/range/target query; never casts, rolls, imports bias or changes a resource.
Query query(const CBattleInfoCallback & battle, const battle::Unit & source, const battle::Unit & target, bool ranged, bool specialtyStrike = true);
}
