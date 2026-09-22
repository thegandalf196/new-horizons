/*
 * NewHorizonsPerkState.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "NewHorizonsPerkState.h"

#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace newHorizonsHeroes
{
namespace
{
constexpr std::string_view METAMAGIC_SKILL = "new-horizons:metamagic";
constexpr std::string_view RETIRED_SPELL_BUFFER = "new-horizons:metamagic.spellBuffer";
constexpr std::string_view SPELL_ECHO = "new-horizons:metamagic.spellEcho";
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
}

void PerkState::migrateRetiredPerks()
{
	if(!usesPerkRules(rules))
		return;

	auto & skills = rules["skills"].Struct();
	const auto skillIt = skills.find(std::string(METAMAGIC_SKILL));
	if(skillIt != skills.end())
	{
		for(auto & perk : skillIt->second["perks"].Vector())
		{
			if(perk["id"].String() != RETIRED_SPELL_BUFFER)
				continue;
			perk["id"].String() = SPELL_ECHO;
			perk["name"].String() = "Spell Echo";
			perk["description"].String() = SPELL_ECHO_DESCRIPTION;
			perk["effect"]["status"].String() = "active";
			perk["effect"]["description"].String() = SPELL_ECHO_DESCRIPTION;
		}
	}

	for(auto & selection : selected)
		if(selection.skillId == METAMAGIC_SKILL && selection.perkId == RETIRED_SPELL_BUFFER)
			selection.perkId = SPELL_ECHO;
}

bool PerkState::hasSelection(const std::string & skillId, const std::string & perkId) const
{
	return std::any_of(selected.begin(), selected.end(), [&](const auto & entry)
	{
		return entry.skillId == skillId && entry.perkId == perkId;
	});
}

void PerkState::normalizeLegacyTierConflicts()
{
	if(!usesPerkRules(rules))
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
			|| !occupiedTiers.emplace(entry.skillId, perkRequiredRank(definition->requiredRank)).second
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
	if(std::any_of(selected.begin(), selected.end(), [&](const auto & entry)
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
	const std::function<int(const std::string &)> & rankLookup, uint64_t seed) const
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
			if(rank < requiredRank)
				continue;
			const bool tierOccupied = std::any_of(selected.begin(), selected.end(), [&](const auto & entry)
			{
				const auto existing = perkDefinition(rules, entry.skillId, entry.perkId);
				return entry.skillId == skillId && existing
					&& perkRequiredRank(existing->requiredRank) == requiredRank;
			});
			if(tierOccupied)
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
	std::vector<PerkOfferCandidate> result;
	const size_t limit = std::min(eligible.size(), static_cast<size_t>(rules["maxPerkChoices"].Integer()));
	for(size_t i = 0; i < limit; ++i)
		result.push_back(std::move(eligible[i].candidate));
	return result;
}

void PerkState::acceptOffer(const std::vector<PerkOfferCandidate> & offer, size_t choice,
	const std::function<int(const std::string &)> & rankLookup, uint64_t seed)
{
	validate();
	if(offer.empty() || offer.size() > static_cast<size_t>(PERK_MAX_PERK_CHOICES) || choice >= offer.size())
		throw std::runtime_error("Invalid New Horizons perk offer selection");
	if(offer != prepareOffer(rankLookup, seed))
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
