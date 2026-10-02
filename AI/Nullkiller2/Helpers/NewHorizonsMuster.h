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
#include <vector>

class CGDwelling;
class CGHeroInstance;
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
	CreatureID secondCreature = CreatureID::NONE;
	newHorizonsCreatures::CreatureCategory category = newHorizonsCreatures::CreatureCategory::CORE;
	int amount = 0;
	int firstAmount = 0;
	int secondAmount = 0;
	int64_t armyValue = 0;
	int64_t recruitableArmyValue = 0;
	int row = -1;
	int secondRow = -1;

	bool isSplit() const
	{
		return secondCreature != CreatureID::NONE;
	}

	bool valid() const
	{
		if(creature == CreatureID::NONE || amount <= 0 || row < 0)
			return false;
		if(!isSplit())
			return secondAmount == 0 && secondRow < 0;
		return secondAmount > 0 && firstAmount > 0 && firstAmount + secondAmount == amount
			&& secondRow >= 0 && secondRow != row && secondCreature != creature;
	}
};

/// A saved-Core row and the hero's current ability to receive one more stack
/// of that creature. A missing Leadership maximum means that this creature
/// has no per-stack cap under the saved rules.
struct CoreMusterRow
{
	CreatureID creature = CreatureID::NONE;
	int row = -1;
	int aiValue = 0;
	int currentStackCount = 0;
	std::optional<int> leadershipMaximum;
};

/// Select a strictly-better split of one town Muster amount across two
/// distinct saved-Core rows. The exact generated total is split into positive
/// counts; the comparison value includes only recruits the current hero can
/// admit under its per-stack Leadership capacity. Ties preserve the solo path.
std::optional<Candidate> chooseBroadMusterSplit(const std::vector<CoreMusterRow> & coreRows,
	int totalAmount, bool broadMusterActive, int64_t bestSoloRecruitableArmyValue);

/// Choose the most valuable legal row from a town's replicated recruitment
/// roster.  Rows with no New Horizons category are intentionally ignored so
/// legacy worlds never receive a New Horizons Muster request.  A row's value
/// is creature AI value multiplied by the exact rank/category amount.
std::optional<Candidate> chooseTownCandidate(const CGDwelling & town,
	const IGameInfoCallback & callback,
	int recruitmentRank,
	const ::newHorizonsMuster::PerkModifiers & modifiers = {},
	const CGHeroInstance * hero = nullptr,
	bool broadMusterActive = false);

/// Choose the most valuable saved-Core row at a currently eligible external
/// dwelling. Empty pools are valid targets because Muster replenishes stock.
std::optional<Candidate> chooseExternalCandidate(const CGDwelling & dwelling,
	const IGameInfoCallback & callback,
	int recruitmentRank,
	bool externalRecruiterActive);

} // namespace NK2AI::newHorizonsMuster
