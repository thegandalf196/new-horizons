/*
 * NewHorizonsMuster.h, part of VCMI engine
 *
 * New Horizons Muster selection used by Nullkiller2. This helper only
 * reads replicated dwelling/creature/category state; the authoritative
 * request is sent through CCallback::musterCreatures.
 */
#pragma once

#include "../../../lib/entities/creature/NewHorizonsCreatureCategoryRules.h"
#include "../../../lib/entities/creature/NewHorizonsMusterRules.h"

#include <cstdint>
#include <optional>

class CGDwelling;
class IGameInfoCallback;

namespace NK2AI::newHorizonsMuster
{

/// The amount granted by Muster for a creature category, including any active
/// Recruitment perk modifiers supplied by the caller.
///
/// The returned value is deliberately not a percentage.  It is the exact
/// number of normal recruits created by one town Muster request.  A missing
/// value means that the category is not legal at that Recruitment rank.
std::optional<int> amountMultiplier(int recruitmentRank,
	newHorizonsCreatures::CreatureCategory category, const ::newHorizonsMuster::PerkModifiers & modifiers = {});

/// External Recruiter always adds exactly two saved-Core recruits. Keep the
/// ordinary Recruitment rank domain bounded to the canonical three ranks.
inline std::optional<int> externalAmountMultiplier(int recruitmentRank,
	newHorizonsCreatures::CreatureCategory category, bool externalRecruiterActive)
{
	if(recruitmentRank < 1 || recruitmentRank > 3)
		return std::nullopt;
	return ::newHorizonsMuster::amountForExternalCategory(recruitmentRank, category, externalRecruiterActive);
}

struct Candidate
{
	CreatureID creature = CreatureID::NONE;
	newHorizonsCreatures::CreatureCategory category = newHorizonsCreatures::CreatureCategory::CORE;
	int amount = 0;
	int64_t armyValue = 0;
	int row = -1;

	bool valid() const
	{
		return creature != CreatureID::NONE && amount > 0 && row >= 0;
	}
};

/// Choose the most valuable legal row from a town's replicated recruitment
/// roster.  Rows with no New Horizons category are intentionally ignored so
/// legacy worlds never receive a New Horizons Muster request.  A row's value
/// is creature AI value multiplied by the exact rank/category amount.
std::optional<Candidate> chooseTownCandidate(const CGDwelling & town,
	const IGameInfoCallback & callback,
	int recruitmentRank,
	const ::newHorizonsMuster::PerkModifiers & modifiers = {});

/// Choose the most valuable saved-Core row at a currently eligible external
/// dwelling. Empty pools are valid targets because Muster replenishes stock.
std::optional<Candidate> chooseExternalCandidate(const CGDwelling & dwelling,
	const IGameInfoCallback & callback,
	int recruitmentRank,
	bool externalRecruiterActive);

} // namespace NK2AI::newHorizonsMuster
