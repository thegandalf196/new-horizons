/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../lib/entities/hero/NewHorizonsPerkState.h"
#include <utility>
#include <stdexcept>

namespace perkStateFixture
{
/// Author a complete replacement locally; publication still uses runtime admission.
template<typename Edit> void editRules(newHorizonsHeroes::PerkState & state, Edit edit)
{
	auto rules = state.getRules();
	edit(rules);
	state.replaceRules(std::move(rules));
}
template<typename Edit> void editSelected(newHorizonsHeroes::PerkState & state, Edit edit)
{
	auto selected = state.getSelected();
	edit(selected);
	state.replaceSelected(std::move(selected));
}
/// Seed a current effect fixture through the same legal ordered tier rules.
inline void selectWithPrerequisites(newHorizonsHeroes::PerkState & state,
	const std::string & skill, const std::string & perk, int rank = 3)
{
	const auto * catalog = state.compiledCatalogue();
	const auto * target = catalog ? catalog->definition(catalog->find(skill, perk)) : nullptr;
	if(!target) throw std::runtime_error("Unknown effect-fixture perk");
	for(int tier = 1; tier < target->requiredRank; ++tier)
	{
		if(state.hasPerkForTier(skill, tier)) continue;
		const auto choices = state.candidatesForSkillTier(skill, tier);
		if(choices.empty()) throw std::runtime_error("Missing effect-fixture prerequisite");
		state.select(skill, choices.front().selection.perkId, rank);
	}
	state.select(skill, perk, rank);
}

}
