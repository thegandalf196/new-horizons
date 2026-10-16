/*
 * HeroGrowthPerkPresentation.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "NewHorizonsPerkAvailability.h"

#include <functional>
#include <map>
#include <vector>

namespace heroGrowthPerkPresentation
{
using newHorizonsPerkAvailability::Status;
using newHorizonsPerkAvailability::Reason;

struct Card
{
	newHorizonsPerkAvailability::Evaluation evaluation;
	std::string status;
	std::string reason;
};

inline Card card(const newHorizonsHeroes::PerkState & state,
	const newHorizonsHeroes::SkillDefinition & skill, int rank,
	const newHorizonsHeroes::PerkDefinition & perk)
{
	const auto evaluation = newHorizonsPerkAvailability::evaluatePerk(state, skill.id, rank, perk);
	return {evaluation, newHorizonsPerkAvailability::statusName(evaluation.status),
		newHorizonsPerkAvailability::explanation(evaluation, skill.name)};
}

inline std::string joinNames(const std::vector<std::string> & names)
{
	std::string result;
	for(const auto & name : names)
	{
		if(!result.empty())
			result += ", ";
		result += name;
	}
	return result;
}

struct SkillSummary
{
	size_t eligible = 0;
	size_t locked = 0;
	size_t unavailable = 0;
	std::string text;
};

/// Aggregate display classifications; never infer admission from rank or counts.
inline SkillSummary skillSummary(const newHorizonsHeroes::PerkState & state,
	const newHorizonsHeroes::SkillDefinition & skill, int rank,
	const std::vector<newHorizonsHeroes::PerkModifier> & projected)
{
	SkillSummary result;
	std::vector<std::string> learned;
	std::vector<std::string> eligible;
	std::map<std::string, size_t> blocked;
	for(const auto & perk : skill.perks)
	{
		const auto presentation = card(state, skill, rank, perk);
		const auto & evaluation = presentation.evaluation;
		if(evaluation.status == Status::ACQUIRED)
		{
			const auto modifier = std::find_if(projected.begin(), projected.end(), [&](const auto & candidate)
			{
				return candidate.skillId == skill.id && candidate.perkId == perk.id;
			});
			const bool active = modifier != projected.end() && modifier->enabled;
			std::string status = evaluation.reason == Reason::PLANNED_INACTIVE
				? "planned; inactive" : active ? "active" : "inactive";
			if(!active && rank < evaluation.requiredRank)
				status += "; requires " + newHorizonsPerkAvailability::tierName(evaluation.requiredRank);
			learned.push_back(perk.name + " (" + status + ")");
		}
		else if(evaluation.status == Status::AVAILABLE)
			eligible.push_back(perk.name);
		else
		{
			if(evaluation.status == Status::LOCKED)
				++result.locked;
			else
				++result.unavailable;
			++blocked[presentation.reason];
		}
	}
	const size_t selected = std::count_if(state.selected.begin(), state.selected.end(), [&](const auto & selection)
	{
		return selection.skillId == skill.id;
	});
	const auto cap = static_cast<size_t>(state.rules["maxPerksPerSkill"].Integer());
	result.eligible = eligible.size();
	result.text = "\n" + skill.name + (rank > 0
		? " (" + newHorizonsPerkAvailability::tierName(rank) + ") - "
			+ std::to_string(selected) + "/" + std::to_string(cap) + " selected, "
			+ std::to_string(cap - std::min(selected, cap)) + " remaining"
		: " - locked: skill not learned");
	if(!learned.empty())
		result.text += "\n  Selected: " + joinNames(learned);
	if(!eligible.empty())
		result.text += "\n  Eligible for future level-up offers: " + joinNames(eligible);
	for(const auto & [reason, count] : blocked)
		result.text += "\n  " + std::to_string(count) + " - " + reason;
	return result;
}

inline std::string summary(const newHorizonsHeroes::PerkState & state,
	const std::function<int(const std::string &)> & rankFor)
{
	const int cap = state.rules["maxPerksPerSkill"].Integer();
	const auto projected = state.project(rankFor);
	size_t capacity = 0;
	size_t eligible = 0;
	size_t locked = 0;
	size_t unavailable = 0;
	std::vector<std::string> skillIds;
	for(const auto & entry : state.rules["skills"].Struct())
		skillIds.push_back(entry.first);
	std::stable_sort(skillIds.begin(), skillIds.end(), [&](const auto & left, const auto & right)
	{
		const auto learned = [&](const auto & id)
		{
			return rankFor(id) > 0 || std::any_of(state.selected.begin(), state.selected.end(),
				[&](const auto & selection) { return selection.skillId == id; });
		};
		return learned(left) > learned(right);
	});
	std::string details = "Perks\nSelected: " + std::to_string(state.selected.size());
	for(const auto & skillId : skillIds)
	{
		const auto skill = newHorizonsHeroes::perkSkill(state.rules, skillId);
		if(!skill)
			continue;
		const int rank = rankFor(skillId);
		if(rank > 0)
			capacity += static_cast<size_t>(cap);
		const auto entry = skillSummary(state, *skill, rank, projected);
		eligible += entry.eligible;
		locked += entry.locked;
		unavailable += entry.unavailable;
		details += entry.text;
	}
	return "Selected: " + std::to_string(state.selected.size()) + " / " + std::to_string(capacity)
		+ " capacity for learned skills; " + std::to_string(eligible) + " eligible for future level-up offers; "
		+ std::to_string(locked) + " locked; " + std::to_string(unavailable) + " unavailable.\nEach learned skill can hold up to "
		+ std::to_string(cap) + " perks; a level-up offers at most "
		+ std::to_string(state.rules["maxPerkChoices"].Integer()) + ".\n" + details;
}
}
