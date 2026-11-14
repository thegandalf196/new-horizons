/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../lib/entities/hero/NewHorizonsPerkState.h"
#include "../lib/mapObjects/CGHeroInstance.h"
#include <optional>
#include <span>

namespace heroPerkAI
{
/// Mandatory perk-only offers are not ordinary random level-up alternatives.
/// Choose the first currently eligible server-authored entry in stable order;
/// never consume gameplay RNG or spend a second progression opportunity.
inline std::optional<size_t> firstEligible(const CGHeroInstance & hero,
	std::span<const newHorizonsHeroes::PerkOfferCandidate> offered)
{
	for(size_t index = 0; index < offered.size(); ++index)
	{
		const auto & candidate = offered[index];
		const auto definition = newHorizonsHeroes::perkDefinition(hero.getPerkState().rules,
			candidate.selection.skillId, candidate.selection.perkId);
		if(definition && definition->effect["status"].String() == "active"
			&& candidate.requiredRank == newHorizonsHeroes::perkRequiredRank(definition->requiredRank)
			&& hero.getPerkState().canAcquirePerk(candidate.selection.skillId,
				candidate.selection.perkId, hero.getPerkSkillRank(candidate.selection.skillId)))
			return index;
	}
	return std::nullopt;
}
}
