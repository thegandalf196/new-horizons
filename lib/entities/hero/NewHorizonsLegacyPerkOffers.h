/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsPerkState.h"

namespace newHorizonsHeroes::detail
{
struct RankedPerkCandidate
{
	uint64_t order = 0;
	PerkOfferCandidate candidate;
};

/// Historical uniform/policy-1 sampler. New four-offer captures never enter it.
std::vector<PerkOfferCandidate> prepareLegacyPerkOffer(std::vector<RankedPerkCandidate> eligible,
	uint64_t seed, const PerkState::OfferWeightContext & weightContext, const JsonNode & rules);
}
