/*
 * NewHorizonsPerkAvailability.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../../lib/entities/hero/NewHorizonsPerkState.h"

#include <algorithm>
#include <exception>
#include <optional>
#include <string>
#include <string_view>

namespace newHorizonsPerkAvailability
{
enum class Status
{
	ACQUIRED,
	AVAILABLE,
	LOCKED,
	UNAVAILABLE
};

enum class Reason
{
	NONE,
	NO_CATALOGUE,
	SKILL_NOT_LEARNED,
	PLANNED_INACTIVE,
	INSUFFICIENT_RANK,
	EARLIER_TIER_MISSING,
	TIER_OCCUPIED,
	PER_SKILL_CAP
};

struct Evaluation
{
	Status status = Status::UNAVAILABLE;
	Reason reason = Reason::NO_CATALOGUE;
	int requiredRank = 0;
};

struct TierSlot
{
	int requiredRank = 0;
	Status status = Status::UNAVAILABLE;
	Reason reason = Reason::NO_CATALOGUE;
	std::optional<newHorizonsHeroes::PerkDefinition> selectedPerk;
};

namespace detail
{
inline bool tierSelected(const newHorizonsHeroes::PerkState & state, std::string_view skillId, int tier)
{
	const std::string skillIdString(skillId);
	for(const auto & selection : state.selected)
	{
		if(selection.skillId != skillIdString)
			continue;
		try
		{
			const auto selected = newHorizonsHeroes::perkDefinition(state.rules, skillIdString, selection.perkId);
			if(selected && newHorizonsHeroes::perkRequiredRank(selected->requiredRank) == tier)
				return true;
		}
		catch(const std::exception &)
		{
			return false;
		}
	}
	return false;
}
}

inline std::string tierName(int tier)
{
	switch(tier)
	{
	case 1:
		return "Basic";
	case 2:
		return "Advanced";
	case 3:
		return "Expert";
	default:
		return "Unknown";
	}
}

inline std::string statusName(Status status)
{
	switch(status)
	{
	case Status::ACQUIRED:
		return "Acquired";
	case Status::AVAILABLE:
		return "Available";
	case Status::LOCKED:
		return "Locked";
	case Status::UNAVAILABLE:
		return "Unavailable";
	}
	return "Unavailable";
}

inline std::string explanation(const Evaluation & evaluation, std::string_view skillName)
{
	const std::string skill(skillName);
	switch(evaluation.status)
	{
	case Status::ACQUIRED:
		return evaluation.reason == Reason::PLANNED_INACTIVE
			? "This saved perk is acquired, but its planned effect is inactive."
			: "This perk has already been acquired.";
	case Status::AVAILABLE:
		return "Eligible for a future level-up offer; browsing does not acquire it.";
	case Status::LOCKED:
		switch(evaluation.reason)
		{
		case Reason::SKILL_NOT_LEARNED:
			return "Locked: learn " + skill + " first.";
		case Reason::INSUFFICIENT_RANK:
			return "Locked: requires " + tierName(evaluation.requiredRank) + " " + skill + ".";
		case Reason::EARLIER_TIER_MISSING:
			return "Locked: select the earlier " + skill + " perk tier first.";
		case Reason::TIER_OCCUPIED:
			return "Locked: this tier already has its one selected perk.";
		case Reason::PER_SKILL_CAP:
			return "Locked: the per-Skill perk limit has been reached.";
		default:
			return "Locked by the saved perk rules.";
		}
	case Status::UNAVAILABLE:
		if(evaluation.reason == Reason::PLANNED_INACTIVE)
			return "Unavailable: this perk is planned and its effect is inactive.";
		return "Unavailable: no matching saved perk catalogue is available.";
	}
	return "Unavailable: no matching saved perk catalogue is available.";
}

/// Read-only presentation classification matching the saved offer/select rules.
/// Planned entries remain visible, but can never be presented as available.
inline Evaluation evaluatePerk(const newHorizonsHeroes::PerkState & state,
	std::string_view skillId, int skillRank, const newHorizonsHeroes::PerkDefinition & requested)
{
	Evaluation result;
	if(!newHorizonsHeroes::usesPerkRules(state.rules))
		return result;
	const std::string skillIdString(skillId);

	std::optional<newHorizonsHeroes::PerkDefinition> perk;
	try
	{
		perk = newHorizonsHeroes::perkDefinition(state.rules, skillIdString, requested.id);
	}
	catch(const std::exception &)
	{
		return result;
	}
	if(!perk)
		return result;

	result.requiredRank = newHorizonsHeroes::perkRequiredRank(perk->requiredRank);
	if(state.hasSelection(skillIdString, perk->id))
	{
		result.status = Status::ACQUIRED;
		result.reason = perk->effect["status"].String() == "active" ? Reason::NONE : Reason::PLANNED_INACTIVE;
		return result;
	}
	if(perk->effect["status"].String() != "active")
	{
		result.status = Status::UNAVAILABLE;
		result.reason = Reason::PLANNED_INACTIVE;
		return result;
	}
	if(skillRank <= 0)
	{
		result.status = Status::LOCKED;
		result.reason = Reason::SKILL_NOT_LEARNED;
		return result;
	}
	if(skillRank < result.requiredRank)
	{
		result.status = Status::LOCKED;
		result.reason = Reason::INSUFFICIENT_RANK;
		return result;
	}

	const auto selectedForSkill = std::count_if(state.selected.begin(), state.selected.end(), [&skillIdString](const auto & selection)
	{
		return selection.skillId == skillIdString;
	});
	if(selectedForSkill >= state.rules["maxPerksPerSkill"].Integer())
	{
		result.status = Status::LOCKED;
		result.reason = Reason::PER_SKILL_CAP;
		return result;
	}
	for(int tier = 1; tier < result.requiredRank; ++tier)
		if(!detail::tierSelected(state, skillId, tier))
		{
			result.status = Status::LOCKED;
			result.reason = Reason::EARLIER_TIER_MISSING;
			return result;
		}
	if(detail::tierSelected(state, skillId, result.requiredRank))
	{
		result.status = Status::LOCKED;
		result.reason = Reason::TIER_OCCUPIED;
		return result;
	}

	result.status = Status::AVAILABLE;
	result.reason = Reason::NONE;
	return result;
}

/// Summarize one of the three authored tiers. Acquired perks are found by their
/// required rank, never by the order in which the saved selection was written.
inline TierSlot evaluateTier(const newHorizonsHeroes::PerkState & state,
	std::string_view skillId, int skillRank, int tier)
{
	TierSlot result;
	result.requiredRank = tier;
	if(tier < 1 || tier > 3 || !newHorizonsHeroes::usesPerkRules(state.rules))
		return result;
	const std::string skillIdString(skillId);

	std::optional<newHorizonsHeroes::SkillDefinition> skill;
	try
	{
		skill = newHorizonsHeroes::perkSkill(state.rules, skillIdString);
	}
	catch(const std::exception &)
	{
		return result;
	}
	if(!skill)
		return result;

	bool hasTierPerk = false;
	bool hasActiveTierPerk = false;
	std::optional<Evaluation> locked;
	for(const auto & perk : skill->perks)
	{
		if(newHorizonsHeroes::perkRequiredRank(perk.requiredRank) != tier)
			continue;
		hasTierPerk = true;
		if(state.hasSelection(skillIdString, perk.id))
		{
			result.status = Status::ACQUIRED;
			result.reason = perk.effect["status"].String() == "active" ? Reason::NONE : Reason::PLANNED_INACTIVE;
			result.selectedPerk = perk;
			return result;
		}
		if(perk.effect["status"].String() != "active")
			continue;
		hasActiveTierPerk = true;
		const auto evaluation = evaluatePerk(state, skillIdString, skillRank, perk);
		if(evaluation.status == Status::AVAILABLE)
		{
			result.status = Status::AVAILABLE;
			result.reason = Reason::NONE;
			return result;
		}
		if(evaluation.status == Status::LOCKED && !locked)
			locked = evaluation;
	}

	if(locked)
	{
		result.status = Status::LOCKED;
		result.reason = locked->reason;
	}
	else if(hasTierPerk && !hasActiveTierPerk)
	{
		result.status = Status::UNAVAILABLE;
		result.reason = Reason::PLANNED_INACTIVE;
	}
	else if(!hasTierPerk)
	{
		result.status = Status::UNAVAILABLE;
		result.reason = Reason::NO_CATALOGUE;
	}
	return result;
}
}
