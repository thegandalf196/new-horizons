/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "NewHorizonsPerkState.h"
#include "../../constants/EntityIdentifiers.h"
#include <array>
#include <functional>
#include <optional>
#include <vector>

class CGHeroInstance;
class IGameInfoCallback;

namespace newHorizonsHeroes
{
/// Four fixed positions. A missing Skill is NONE; an unavailable group is empty.
/// Old flat HeroLevelUp offers are a different, captured-policy representation.
struct DLL_LINKAGE FourOfferProgressionOffer
{
	std::array<SecondarySkill, 2> skills{SecondarySkill::NONE, SecondarySkill::NONE};
	std::array<int, 2> targetRanks{};
	std::array<std::string, 2> perkSkills;

	bool operator==(const FourOfferProgressionOffer &) const = default;
	bool empty() const;

	template<typename Handler> void serialize(Handler & h)
	{
		h & skills;
		h & targetRanks;
		h & perkSkills;
	}
};

/// An immutable eligibility snapshot. Weights are the unmodified class weights.
struct DLL_LINKAGE FourOfferSkillCandidate
{
	SecondarySkill skill = SecondarySkill::NONE;
	int rank = 0;
	int weight = 0;
	bool skillEligible = false;
	std::vector<PerkOfferCandidate> perks;
};

struct DLL_LINKAGE FourOfferProgressionSelection
{
	SecondarySkill skill = SecondarySkill::NONE;
	std::optional<PerkOfferCandidate> perk;
	bool exhausted = false;
};

using FourOfferWeightedChooser = std::function<size_t(const std::vector<int> &)>;

/// nullopt means the historical ordinary sampler owns this capture.
DLL_LINKAGE std::optional<std::vector<PerkOfferCandidate>> progressionSpecificPerkOffer(
	const PerkState & state, const std::function<int(const std::string &)> & rankLookup);
/// Shared live-state collection; contains no RNG and performs no mutations.
DLL_LINKAGE std::vector<FourOfferSkillCandidate> fourOfferCandidates(
	const IGameInfoCallback & info, const CGHeroInstance & hero);
/// Filtering, rank priority and weighted selection are distinct operations.
DLL_LINKAGE FourOfferProgressionOffer prepareFourOfferProgression(
	const std::vector<FourOfferSkillCandidate> & candidates, const FourOfferWeightedChooser & choose);
/// Strict issuance check against the complete pool used for these four draws.
DLL_LINKAGE void validateFourOfferProgression(const FourOfferProgressionOffer & offer,
	const std::vector<FourOfferSkillCandidate> & candidates);
/// Held identity/presentation remains canonical even when later external grants
/// change eligibility. Priority is an issuance rule, not a reason to redraw.
DLL_LINKAGE void validateFourOfferSnapshot(const FourOfferProgressionOffer & offer, const PerkState & state);
using FourOfferPerkGroups = std::array<std::vector<PerkOfferCandidate>, 2>;
DLL_LINKAGE FourOfferPerkGroups fourOfferPerkGroups(const FourOfferProgressionOffer & offer,
	const std::vector<FourOfferSkillCandidate> & liveCandidates);
DLL_LINKAGE int32_t fourOfferPerkReply(const FourOfferProgressionOffer & offer, const PerkState & state,
	size_t group, const std::string & perkId);
DLL_LINKAGE std::vector<int32_t> legalFourOfferReplies(const FourOfferProgressionOffer & offer, const PerkState & state,
	const std::vector<FourOfferSkillCandidate> & liveCandidates);
/// Skill replies are 0/1. Perks use 2 + group * 10 + captured catalogue index.
DLL_LINKAGE std::optional<FourOfferProgressionSelection> fourOfferSelection(
	const FourOfferProgressionOffer & offer, const PerkState & state, int32_t reply);
/// Existing Connoisseur may replace only a legal candidate in the same slot.
DLL_LINKAGE std::vector<FourOfferSkillCandidate> fourOfferRerollCandidates(
	const FourOfferProgressionOffer & offer, const std::vector<FourOfferSkillCandidate> & candidates, size_t slot);
/// A warning only: selecting another accessible perk remains a legal choice.
DLL_LINKAGE bool perkChoiceBlocksTierCompletion(const PerkState & state,
	const PerkOfferCandidate & candidate, int currentRank);
}
