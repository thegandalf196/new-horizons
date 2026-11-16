/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsLegacyPerkOffers.h"
#include <algorithm>
#include <limits>
#include <map>
#include <tuple>

namespace newHorizonsHeroes::detail
{
std::vector<PerkOfferCandidate> prepareLegacyPerkOffer(std::vector<RankedPerkCandidate> eligible,
	uint64_t seed, const PerkState::OfferWeightContext & weightContext, const JsonNode & rules)
{
	using OfferWeightMode = PerkState::OfferWeightMode;
	// Absence is historical policy: never reinterpret an already captured save
	// or its pending query merely because the current server knows new weights.
	const auto & policy = rules["offerWeightPolicyVersion"];
	if(!policy.isNull())
	{
		if(!policy.isNumber() || policy.Float() != 1)
			throw std::runtime_error("Unsupported New Horizons perk offer weight policy");
		if(weightContext.mode == OfferWeightMode::IntentionalLegacyNoCapturedTable)
		{
			if(weightContext.lookup)
				throw std::runtime_error("Legacy perk weight context must not supply a table lookup");
		}
		else if(weightContext.mode == OfferWeightMode::CapturedTable)
		{
			if(!weightContext.lookup)
				throw std::runtime_error("Captured perk weight table lookup is required");
			std::map<std::string, std::optional<int>> weights;
			for(const auto & entry : eligible)
			{
				const auto & skill = entry.candidate.selection.skillId;
				if(!weights.contains(skill))
					weights.emplace(skill, weightContext.lookup(skill));
			}
			for(const auto & [skill, weight] : weights)
				if(!weight || *weight < 0 || *weight > 100)
					throw std::runtime_error("Missing or invalid captured parent skill offer weight");
			std::erase_if(eligible, [&](const auto & entry)
			{
				return *weights.at(entry.candidate.selection.skillId) == 0;
			});
			std::sort(eligible.begin(), eligible.end(), [](const auto & left, const auto & right)
			{
				return std::tie(left.candidate.selection.skillId, left.candidate.selection.perkId)
					< std::tie(right.candidate.selection.skillId, right.candidate.selection.perkId);
			});
			// SplitMix64 with specified unsigned arithmetic, followed by rejection
			// sampling: no platform-dependent distributions or floating scores.
			uint64_t randomState = seed;
			const auto nextRandom = [&randomState]()
			{
				uint64_t value = (randomState += 0x9e3779b97f4a7c15ULL);
				value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
				value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
				return value ^ (value >> 31);
			};
			std::vector<PerkOfferCandidate> result;
			const size_t limit = std::min(eligible.size(), static_cast<size_t>(rules["maxPerkChoices"].Integer()));
			while(result.size() < limit)
			{
				uint64_t total = 0;
				for(const auto & entry : eligible)
				{
					const auto weight = static_cast<uint64_t>(*weights.at(entry.candidate.selection.skillId));
					if(total > std::numeric_limits<uint64_t>::max() - weight)
						throw std::runtime_error("Perk offer weight sum overflow");
					total += weight; // Each perk receives the FULL parent skill weight.
				}
				const uint64_t threshold = (uint64_t{0} - total) % total;
				uint64_t draw;
				do
				{
					draw = nextRandom();
				} while(draw < threshold);
				draw %= total;
				for(size_t i = 0; i < eligible.size(); ++i)
				{
					const auto weight = static_cast<uint64_t>(*weights.at(eligible[i].candidate.selection.skillId));
					if(draw < weight)
					{
						result.push_back(std::move(eligible[i].candidate));
						eligible.erase(eligible.begin() + i);
						break;
					}
					draw -= weight;
				}
			}
			return result;
		}
		else
			throw std::runtime_error("Explicit captured or legacy perk weight context is required");
		// Only an explicit verified legacy context retains old uniform ordering.
	}
	std::vector<PerkOfferCandidate> result;
	const size_t limit = std::min(eligible.size(), static_cast<size_t>(rules["maxPerkChoices"].Integer()));
	for(size_t i = 0; i < limit; ++i)
		result.push_back(std::move(eligible[i].candidate));
	return result;
}
}
