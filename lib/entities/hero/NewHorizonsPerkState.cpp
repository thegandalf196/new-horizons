/*
 * NewHorizonsPerkState.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "NewHorizonsPerkState.h"

#include <map>
#include <limits>
#include <array>
#include <optional>
#include <set>
#include <stdexcept>
#include <tuple>
#include <cmath>
#include <boost/multiprecision/cpp_dec_float.hpp>
#include <boost/multiprecision/cpp_int.hpp>

namespace newHorizonsHeroes
{
struct R2WeightCache
{
	double ratio = 1;
	std::map<std::string, int> reference;
	std::map<std::string, boost::multiprecision::cpp_int> weights;
};

namespace
{
constexpr std::string_view METAMAGIC_SKILL = "new-horizons:metamagic";
constexpr std::string_view RETIRED_SPELL_BUFFER = "new-horizons:metamagic.spellBuffer";
constexpr std::string_view SPELL_ECHO = "new-horizons:metamagic.spellEcho";
// This source hash identifies the historical rules authority under which
// Spell Buffer meant Counterspell protection. New catalogs must use their
// reconciled source hash before reintroducing Spell Buffer.
constexpr std::string_view RETIRED_SPELL_BUFFER_SOURCE_SHA256 =
	"d0aa9c0017967e85120b4e63e04df3d58441d606ce9c496d29117515330654ce";
constexpr int RETIRED_SPELL_BUFFER_SCHEMA_VERSION = 1;
constexpr int RETIRED_SPELL_BUFFER_RULESET_VERSION = 1;
constexpr std::string_view SPELL_ECHO_DESCRIPTION =
	"If the additional Spell repeats the first Spell in the Metamagic sequence, it gains +25% to its Spell Power-derived component.";

void savedFields(const JsonNode & node, std::initializer_list<std::string_view> keys)
{
	if(!node.isStruct())
		throw std::runtime_error("New Horizons perk state object required");
	for(const auto & [key, value] : node.Struct())
		if(std::find(keys.begin(), keys.end(), key) == keys.end())
			throw std::runtime_error("Unknown New Horizons perk state field " + key);
}

const std::map<std::string, boost::multiprecision::cpp_int> & normalizedR2Weights(
	const PerkState::OfferWeightContext & context, const JsonNode & rules)
{
	using Integer = boost::multiprecision::cpp_int;
	using Decimal = boost::multiprecision::cpp_dec_float_100;
	if(context.mode != PerkState::OfferWeightMode::CapturedTable || !context.lookup
		|| context.referenceWeights.size() != rules["skills"].Struct().size())
		throw std::runtime_error("R2 offers require the complete captured class reference");
	int minimum = 101;
	int maximum = 0;
	for(const auto & [id, definition] : rules["skills"].Struct())
	{
		const auto found = context.referenceWeights.find(id);
		if(found == context.referenceWeights.end() || found->second < 0 || found->second > 100
			|| context.lookup(id) != std::optional<int>(found->second))
			throw std::runtime_error("R2 reference differs from captured Skill weights");
		if(found->second > 0)
		{
			minimum = std::min(minimum, found->second);
			maximum = std::max(maximum, found->second);
		}
	}
	// Software decimal math, then one common2^32 quantum. Sampling uses only
	// integers. Endpoints are explicit; this is a quantized real distribution.
	const double configuredRatio = rules["perkOfferNormalization"]["maximumPreferenceRatio"].Float();
	if(!context.normalizedWeights || context.normalizedWeights->ratio != configuredRatio
		|| context.normalizedWeights->reference != context.referenceWeights)
	{
		auto cache = std::make_shared<R2WeightCache>();
		cache->ratio = configuredRatio;
		cache->reference = context.referenceWeights;
		const Decimal ratio(configuredRatio);
		const uint64_t scaleInteger = uint64_t{1} << PERK_OFFER_WEIGHT_FRACTION_BITS;
		const Integer quantum = scaleInteger;
		const Decimal scale(scaleInteger);
		const auto quantize = [](const Decimal & value) -> Integer
		{
			return floor(value + Decimal("0.5")).template convert_to<Integer>();
		};
		// Construct the endpoint from the exact binary double, not a rounded
		// decimal approximation (large finite R may require over1000 bits).
		static_assert(std::numeric_limits<double>::radix == 2 && std::numeric_limits<double>::digits <= 64);
		int binaryExponent = 0;
		const double mantissa = std::frexp(configuredRatio, &binaryExponent);
		const auto significand = static_cast<uint64_t>(std::ldexp(mantissa, std::numeric_limits<double>::digits));
		const int shift = binaryExponent - std::numeric_limits<double>::digits + static_cast<int>(PERK_OFFER_WEIGHT_FRACTION_BITS);
		Integer upper = significand;
		if(shift >= 0)
			upper <<= shift;
		else
			upper = (upper + (Integer(1) << (-shift - 1))) >> -shift;
		const bool uniform = maximum <= minimum || ratio == 1;
		Decimal exponent = 0;
		if(!uniform)
			exponent = log(ratio) / log(Decimal(maximum) / minimum);
		auto & weights = cache->weights;
		for(const auto & [id, weight] : context.referenceWeights)
		{
			Integer normalized = quantum;
			if(!uniform && weight > minimum)
			{
				normalized = weight == maximum ? upper
					: quantize(exp(log(Decimal(weight) / minimum) * exponent) * scale);
				normalized = std::max(quantum, std::min(upper, normalized));
			}
			weights.emplace(id, std::move(normalized));
		}
		context.normalizedWeights = std::move(cache);
	}
	return context.normalizedWeights->weights;
}

template<typename Candidates>
std::vector<PerkOfferCandidate> prepareR2Offer(const Candidates & eligible, uint64_t seed,
	const PerkState::OfferWeightContext & context, const JsonNode & rules)
{
	using Integer = boost::multiprecision::cpp_int;
	const auto & weights = normalizedR2Weights(context, rules);
	uint64_t randomState = seed;
	const auto nextRandom = [&randomState]()
	{
		uint64_t value = (randomState += 0x9e3779b97f4a7c15ULL);
		value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
		value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
		return value ^ (value >> 31);
	};
	const auto drawBelow = [&nextRandom](const Integer & total) -> Integer
	{
		if(total <= 0)
			throw std::runtime_error("Empty R2 draw");
		if(total == 1)
			return 0;
		const unsigned bits = boost::multiprecision::msb(Integer(total - 1)) + 1;
		const Integer mask = (Integer(1) << bits) - 1;
		Integer value;
		do
		{
			value = 0;
			for(unsigned filled = 0; filled < bits; filled += 64)
			{
				value <<= 64;
				value += nextRandom();
			}
			value &= mask;
		} while(value >= total);
		return value;
	};
	std::map<std::string, std::vector<PerkOfferCandidate>> parents;
	for(const auto & entry : eligible)
		parents[entry.candidate.selection.skillId].push_back(entry.candidate);
	for(auto & [id, candidates] : parents)
		std::sort(candidates.begin(), candidates.end(), [](const auto & a, const auto & b)
		{
			return a.selection.perkId < b.selection.perkId;
		});
	std::vector<PerkOfferCandidate> result;
	const bool soleParent = parents.size() == 1;
	while(result.size() < static_cast<size_t>(rules["maxPerkChoices"].Integer()) && !parents.empty())
	{
		auto selectedParent = parents.begin();
		if(!soleParent)
		{
			Integer total = 0;
			for(const auto & [id, candidates] : parents)
				total += weights.at(id);
			Integer draw = drawBelow(total);
			for(auto it = parents.begin(); it != parents.end(); ++it)
			{
				if(draw < weights.at(it->first))
				{
					selectedParent = it;
					break;
				}
				draw -= weights.at(it->first);
			}
		}
		auto & pool = selectedParent->second;
		const size_t index = drawBelow(Integer(pool.size())).template convert_to<size_t>();
		result.push_back(pool.at(index));
		if(!soleParent || pool.size() == 1)
			parents.erase(selectedParent);
		else
			pool.erase(pool.begin() + index);
	}
	return result;
}

void migrateLegacyHeroProcText(JsonNode & rules)
{
	if(!usesPerkRules(rules))
		return;
	struct LegacyText
	{
		const char * skill;
		const char * perk;
		const char * name;
		const char * description;
	};
	const std::array<LegacyText, 4> legacy = {{
		{"new-horizons:luck", "new-horizons:luck.luckyRecovery", "Lucky Recovery",
			"When a melee attack triggers positive Luck, surviving creatures in the attacker recover HP equal to 10% of actual damage dealt."},
		{"new-horizons:luck", "new-horizons:luck.gambler", "Gambler",
			"The first friendly attack each round gains +3 Luck for that attack. If positive Luck does not trigger, the attacking stack suffers -2 Luck until its next activation."},
		{"new-horizons:luck", "new-horizons:luck.twistOfFate", "Twist of Fate",
			"The first random combat roll each combat that produces a negative result for the hero's army is automatically rerolled once. Deterministic effects cannot be rerolled."},
		{"new-horizons:discipline", "new-horizons:discipline.heroicSpirit", "Heroic Spirit",
			"When positive Morale triggers, the affected stack gains one additional retaliation until its next activation."}
	}};
	// Resource services are initialized before world/crossover loading. Load the
	// shipped text only when a recognized legacy row is present, never cache a
	// second catalogue or replace the saved gameplay registry/provenance.
	std::optional<JsonNode> canonical;
	for(const auto & entry : legacy)
	{
		auto & skills = rules["skills"].Struct();
		const auto skill = skills.find(entry.skill);
		if(skill == skills.end() || !skill->second["perks"].isVector())
			continue;
		for(auto & perk : skill->second["perks"].Vector())
		{
			if(perk["id"].String() != entry.perk || perk["name"].String() != entry.name
				|| perk["description"].String() != entry.description
				|| perk["effect"]["description"].String() != entry.description)
				continue;
			if(!canonical)
				canonical.emplace(JsonPath::builtin("config/newHorizonsPerks"));
			const auto current = perkDefinition(*canonical, entry.skill, entry.perk);
			if(!current)
				throw std::runtime_error("Missing canonical Hero proc perk text");
			perk["name"].String() = current->name;
			perk["description"].String() = current->description;
			perk["effect"]["description"] = current->effect["description"];
		}
	}
}
}

void PerkState::migrateRetiredPerks()
{
	migrateLegacyHeroProcText(rules);
	if(!usesPerkRules(rules)
		|| !rules["schemaVersion"].isNumber()
		|| rules["schemaVersion"].Integer() != RETIRED_SPELL_BUFFER_SCHEMA_VERSION
		|| !rules["rulesetVersion"].isNumber()
		|| rules["rulesetVersion"].Integer() != RETIRED_SPELL_BUFFER_RULESET_VERSION
		|| !rules["sourceSha256"].isString()
		|| rules["sourceSha256"].String() != RETIRED_SPELL_BUFFER_SOURCE_SHA256)
		return;

	auto & skills = rules["skills"].Struct();
	const auto skillIt = skills.find(std::string(METAMAGIC_SKILL));
	if(skillIt == skills.end() || !skillIt->second["perks"].isVector())
		return;

	bool migratedDefinition = false;
	for(auto & perk : skillIt->second["perks"].Vector())
	{
		if(perk["id"].String() != RETIRED_SPELL_BUFFER)
			continue;
		perk["id"].String() = SPELL_ECHO;
		perk["name"].String() = "Spell Echo";
		perk["description"].String() = SPELL_ECHO_DESCRIPTION;
		perk["effect"]["status"].String() = "active";
		perk["effect"]["description"].String() = SPELL_ECHO_DESCRIPTION;
		migratedDefinition = true;
	}

	if(migratedDefinition)
	{
		for(auto & selection : selected)
			if(selection.skillId == METAMAGIC_SKILL && selection.perkId == RETIRED_SPELL_BUFFER)
				selection.perkId = SPELL_ECHO;
	}
}

bool PerkState::hasSelection(const std::string & skillId, const std::string & perkId) const
{
	return std::any_of(selected.begin(), selected.end(), [&](const auto & entry)
	{
		return entry.skillId == skillId && entry.perkId == perkId;
	});
}

bool PerkState::canAdvanceSkillNormally(std::string_view skillId, int currentRank) const
{
	if(currentRank < 0 || currentRank > 2)
		return false;
	if(!usesPerkRules(rules) || currentRank == 0)
		return true;
	validate();
	for(int requiredTier = 1; requiredTier <= currentRank; ++requiredTier)
	{
		const bool tierSelected = std::any_of(selected.begin(), selected.end(), [&](const auto & entry)
		{
			const auto definition = perkDefinition(rules, entry.skillId, entry.perkId);
			return entry.skillId == skillId && definition
				&& perkRequiredRank(definition->requiredRank) == requiredTier;
		});
		if(!tierSelected)
			return false;
	}
	return true;
}

void PerkState::normalizeLegacyTierConflicts()
{
	if(!usesPerkRules(rules) || usesR2PerkOffers(rules))
		return;

	std::set<std::pair<std::string, int>> occupiedTiers;
	std::erase_if(selected, [&](const auto & entry)
	{
		const auto definition = perkDefinition(rules, entry.skillId, entry.perkId);
		if(!definition)
			return false;
		return !occupiedTiers.emplace(entry.skillId, perkRequiredRank(definition->requiredRank)).second;
	});
}

void PerkState::validate() const
{
	validatePerkRules(rules);
	if(!usesPerkRules(rules))
	{
		if(!selected.empty())
			throw std::runtime_error("Perk selections without a saved rules identity");
		return;
	}
	const auto cap = static_cast<size_t>(rules["maxPerksPerSkill"].Integer());
	std::set<std::pair<std::string, std::string>> identities;
	std::set<std::pair<std::string, int>> occupiedTiers;
	std::map<std::string, size_t> perSkill;
	for(const auto & entry : selected)
	{
		const auto definition = perkDefinition(rules, entry.skillId, entry.perkId);
		if(!definition || !identities.emplace(entry.skillId, entry.perkId).second
			|| (!usesR2PerkOffers(rules) && !occupiedTiers.emplace(entry.skillId, perkRequiredRank(definition->requiredRank)).second)
			|| ++perSkill[entry.skillId] > cap)
			throw std::runtime_error("Invalid saved New Horizons perk selection");
	}
}

void PerkState::select(const std::string & skillId, const std::string & perkId, int currentRank)
{
	validate();
	if(currentRank < 0 || currentRank > 3)
		throw std::runtime_error("Invalid New Horizons skill rank");
	const auto definition = perkDefinition(rules, skillId, perkId);
	if(!definition || definition->effect["status"].String() != "active"
		|| currentRank < perkRequiredRank(definition->requiredRank) || hasSelection(skillId, perkId))
		throw std::runtime_error("Unavailable New Horizons perk selection");
	const int selectedTier = perkRequiredRank(definition->requiredRank);
	for(int prerequisiteTier = 1; prerequisiteTier < selectedTier; ++prerequisiteTier)
	{
		const bool prerequisiteSelected = std::any_of(selected.begin(), selected.end(), [&](const auto & entry)
		{
			const auto existing = perkDefinition(rules, entry.skillId, entry.perkId);
			return entry.skillId == skillId && existing
				&& perkRequiredRank(existing->requiredRank) == prerequisiteTier;
		});
		if(!prerequisiteSelected)
			throw std::runtime_error("Earlier New Horizons perk tier is still required");
	}
	if(!usesR2PerkOffers(rules) && std::any_of(selected.begin(), selected.end(), [&](const auto & entry)
	{
		const auto existing = perkDefinition(rules, entry.skillId, entry.perkId);
		return entry.skillId == skillId && existing
			&& perkRequiredRank(existing->requiredRank) == selectedTier;
	}))
		throw std::runtime_error("New Horizons perk tier already occupied for skill");
	const auto count = std::count_if(selected.begin(), selected.end(), [&](const auto & entry)
	{
		return entry.skillId == skillId;
	});
	if(count >= rules["maxPerksPerSkill"].Integer())
		throw std::runtime_error("New Horizons perk cap reached for skill");
	selected.push_back({skillId, perkId});
	validate();
}

std::vector<PerkOfferCandidate> PerkState::prepareOffer(
	const std::function<int(const std::string &)> & rankLookup, uint64_t seed,
	const OfferWeightContext & weightContext) const
{
	validate();
	if(!usesPerkRules(rules))
		return {};

	struct RankedCandidate
	{
		uint64_t order = 0;
		PerkOfferCandidate candidate;
	};
	std::vector<RankedCandidate> eligible;
	const auto mix = [seed](std::string_view skillId, std::string_view perkId)
	{
		uint64_t value = 1469598103934665603ULL ^ seed;
		for(const unsigned char c : std::string(skillId) + "\n" + std::string(perkId))
		{
			value ^= c;
			value *= 1099511628211ULL;
		}
		return value;
	};

	for(const auto & [skillId, skillNode] : rules["skills"].Struct())
	{
		const int rank = rankLookup(skillId);
		if(rank <= 0 || rank > 3)
			continue;
		const auto selectedForSkill = std::count_if(selected.begin(), selected.end(), [&](const auto & entry)
		{
			return entry.skillId == skillId;
		});
		if(selectedForSkill >= rules["maxPerksPerSkill"].Integer())
			continue;
		int nextTier = 1;
		while(nextTier <= 3)
		{
			const bool tierSelected = std::any_of(selected.begin(), selected.end(), [&](const auto & entry)
			{
				const auto existing = perkDefinition(rules, entry.skillId, entry.perkId);
				return entry.skillId == skillId && existing
					&& perkRequiredRank(existing->requiredRank) == nextTier;
			});
			if(!tierSelected)
				break;
			++nextTier;
		}
		for(const auto & perkNode : skillNode["perks"].Vector())
		{
			// Planned registry entries are deliberately retained in the saved rules
			// snapshot for forward compatibility, but they are not mechanics. Do not
			// present them as level-up choices before their server-side effect exists.
			if(perkNode["effect"]["status"].String() != "active")
				continue;
			const auto & perkId = perkNode["id"].String();
			if(hasSelection(skillId, perkId))
				continue;
			const int requiredRank = perkRequiredRank(perkNode["requires"].String());
			if(rank < requiredRank || (usesR2PerkOffers(rules) ? requiredRank > nextTier : requiredRank != nextTier))
				continue;
			const bool tierOccupied = std::any_of(selected.begin(), selected.end(), [&](const auto & entry)
			{
				const auto existing = perkDefinition(rules, entry.skillId, entry.perkId);
				return entry.skillId == skillId && existing
					&& perkRequiredRank(existing->requiredRank) == requiredRank;
			});
			if(tierOccupied && !usesR2PerkOffers(rules))
				continue;
			PerkOfferCandidate candidate{{skillId, perkId}, perkNode["name"].String(),
				perkNode["description"].String(), requiredRank};
			eligible.push_back({mix(skillId, perkId), std::move(candidate)});
		}
	}
	std::sort(eligible.begin(), eligible.end(), [](const auto & left, const auto & right)
	{
		return std::tie(left.order, left.candidate.selection.skillId, left.candidate.selection.perkId)
			< std::tie(right.order, right.candidate.selection.skillId, right.candidate.selection.perkId);
	});
	if(usesR2PerkOffers(rules))
		return prepareR2Offer(eligible, seed, weightContext, rules);
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

std::map<std::string, std::string> PerkState::quantizedParentWeights(const OfferWeightContext & context) const
{
	validate();
	if(!usesR2PerkOffers(rules))
		throw std::runtime_error("Quantized parent weights require captured R2 policy");
	std::map<std::string, std::string> result;
	for(const auto & [id, weight] : normalizedR2Weights(context, rules))
		result.emplace(id, weight.convert_to<std::string>());
	return result;
}

void PerkState::acceptOffer(const std::vector<PerkOfferCandidate> & offer, size_t choice,
	const std::function<int(const std::string &)> & rankLookup, uint64_t seed,
	const OfferWeightContext & weightContext)
{
	validate();
	if(offer.empty() || offer.size() > static_cast<size_t>(PERK_MAX_PERK_CHOICES) || choice >= offer.size())
		throw std::runtime_error("Invalid New Horizons perk offer selection");
	if(offer != prepareOffer(rankLookup, seed, weightContext))
		throw std::runtime_error("Stale or forged New Horizons perk offer");
	const auto & selectedCandidate = offer[choice];
	select(selectedCandidate.selection.skillId, selectedCandidate.selection.perkId,
		rankLookup(selectedCandidate.selection.skillId));
}

std::vector<PerkModifier> PerkState::project(
	const std::function<int(const std::string &)> & rankLookup) const
{
	validate();
	std::vector<PerkModifier> result;
	result.reserve(selected.size());
	for(const auto & entry : selected)
	{
		const auto definition = perkDefinition(rules, entry.skillId, entry.perkId);
		const int requiredRank = perkRequiredRank(definition->requiredRank);
		result.push_back({entry.skillId, entry.perkId, requiredRank,
			rankLookup(entry.skillId) >= requiredRank && definition->effect["status"].String() == "active",
			definition->effect});
	}
	return result;
}

JsonNode PerkState::toJson() const
{
	validate();
	if(!usesPerkRules(rules))
		return JsonNode();
	JsonNode result;
	result["stateVersion"].Integer() = 1;
	result["rules"] = rules;
	result["selected"].Vector();
	for(const auto & entry : selected)
	{
		JsonNode saved;
		saved["skillId"].String() = entry.skillId;
		saved["perkId"].String() = entry.perkId;
		result["selected"].Vector().push_back(std::move(saved));
	}
	return result;
}

PerkState PerkState::fromJson(const JsonNode & node)
{
	PerkState result;
	if(node.isNull() || (node.isStruct() && node.Struct().empty()))
		return result;
	savedFields(node, {"stateVersion", "rules", "selected"});
	if(node["stateVersion"].getType() != JsonNode::JsonType::DATA_INTEGER
		|| node["stateVersion"].Integer() != 1 || !node["selected"].isVector())
		throw std::runtime_error("Unsupported New Horizons perk state version or shape");
	result.rules = node["rules"];
	for(const auto & saved : node["selected"].Vector())
	{
		savedFields(saved, {"skillId", "perkId"});
		if(!saved["skillId"].isString() || !saved["perkId"].isString())
			throw std::runtime_error("Invalid New Horizons perk selection identity");
		result.selected.push_back({saved["skillId"].String(), saved["perkId"].String()});
	}
	result.migrateRetiredPerks();
	result.normalizeLegacyTierConflicts();
	result.validate();
	return result;
}
}
