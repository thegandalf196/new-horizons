/*
 * NewHorizonsMusterRules.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 */
#pragma once

#include "NewHorizonsCreatureCategoryRules.h"

#include <algorithm>
#include <optional>
#include <string_view>

namespace newHorizonsMuster
{
inline constexpr std::string_view RECRUITMENT_SKILL = "new-horizons:recruitment";
inline constexpr std::string_view VOLUNTEER_NETWORK_PERK = "new-horizons:recruitment.volunteerNetwork";
inline constexpr std::string_view ELITE_DRAFT_PERK = "new-horizons:recruitment.eliteDraft";
inline constexpr std::string_view CHAMPIONS_CALL_PERK = "new-horizons:recruitment.championSCall";
inline constexpr std::string_view MASTER_RECRUITER_PERK = "new-horizons:recruitment.masterRecruiter";

struct PerkModifiers
{
	bool volunteerNetwork = false;
	bool eliteDraft = false;
	bool championsCall = false;
	bool masterRecruiter = false;
};

/// Return the exact number of recruits generated for one legal town target.
/// The rank table is the base effect; the four booleans are the only active
/// Recruitment perk modifiers in this playable slice.
inline std::optional<int> amountForCategory(const int recruitmentRank,
	const newHorizonsCreatures::CreatureCategory category, const PerkModifiers & modifiers = {})
{
	if(recruitmentRank < 1 || recruitmentRank > 3)
		return std::nullopt;

	int amount = 0;
	switch(recruitmentRank)
	{
	case 1:
		if(category == newHorizonsCreatures::CreatureCategory::CORE)
			amount = 2;
		break;
	case 2:
		if(category == newHorizonsCreatures::CreatureCategory::CORE)
			amount = 4;
		else if(category == newHorizonsCreatures::CreatureCategory::ELITE)
			amount = 1;
		break;
	case 3:
		if(category == newHorizonsCreatures::CreatureCategory::CORE)
			amount = 6;
		else if(category == newHorizonsCreatures::CreatureCategory::ELITE)
			amount = 2;
		else if(category == newHorizonsCreatures::CreatureCategory::CHAMPION)
			amount = modifiers.championsCall ? 2 : 1;
		break;
	default:
		break;
	}

	if(amount == 0)
		return std::nullopt;

	if(category == newHorizonsCreatures::CreatureCategory::CORE && modifiers.volunteerNetwork)
		amount += 2;
	if(category == newHorizonsCreatures::CreatureCategory::ELITE && modifiers.eliteDraft)
		++amount;

	return amount;
}

inline int maximumUsesPerWeek(const PerkModifiers & modifiers)
{
	return modifiers.masterRecruiter ? 2 : 1;
}

/// Convert the engine's one-based day counter into the zero-based absolute
/// week used by all Muster state.  Initialization day zero intentionally maps
/// to week zero as well, avoiding a transient negative week in UI/AI guards.
inline int absoluteWeek(const int currentDay, const int daysInWeek)
{
	if(daysInWeek <= 0)
		return 0;
	return std::max(0, (currentDay - 1) / daysInWeek);
}
}
