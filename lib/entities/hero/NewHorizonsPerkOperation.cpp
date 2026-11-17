/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsPerkOperation.h"
#include "../../nativeRules/CapturedPerkRules.h"
#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>

namespace newHorizonsHeroes
{
namespace
{
// Callers validate the registry first. Borrow nodes here rather than repeatedly
// validating the full catalogue for each candidate in one offer transaction.
const JsonNode * acquisitionDefinition(const JsonNode & rules, const std::string & skillId, const std::string & perkId)
{
	const auto & skills = rules["skills"].Struct();
	const auto parent = skills.find(skillId);
	if(parent == skills.end())
		return nullptr;
	const auto & perks = parent->second["perks"].Vector();
	const auto found = std::find_if(perks.begin(), perks.end(), [&](const auto & row)
	{
		return row["id"].String() == perkId;
	});
	return found == perks.end() ? nullptr : &*found;
}

}

bool PerkState::canAcquirePerkInState(const JsonNode & rules, const std::vector<PerkSelection> & selected,
	const std::string & skillId, const std::string & perkId, int currentRank)
{
	if(!usesPerkRules(rules) || currentRank < 1 || currentRank > 3)
		return false;
	const auto * definition = acquisitionDefinition(rules, skillId, perkId);
	if(!definition || (*definition)["effect"]["status"].String() != "active")
		return false;
	const int tier = perkRequiredRank((*definition)["requires"].String());
	if(tier > currentRank)
		return false;
	std::array<bool, 4> occupied{};
	size_t count = 0;
	for(const auto & entry : selected)
	{
		if(entry.skillId != skillId)
			continue;
		if(entry.perkId == perkId)
			return false;
		++count;
		const auto * existing = acquisitionDefinition(rules, skillId, entry.perkId);
		if(!existing)
			return false;
		occupied.at(perkRequiredRank((*existing)["requires"].String())) = true;
	}
	if(count >= static_cast<size_t>(rules["maxPerksPerSkill"].Integer()))
		return false;
	for(int prerequisite = 1; prerequisite < tier; ++prerequisite)
		if(!occupied[prerequisite])
			return false;
	const bool onePerTier = !usesR2PerkOffers(rules)
		|| (usesStrictPerkProgression(rules) && !usesFourOfferProgression(rules))
		|| usesExclusivePerkTiers(rules);
	return !onePerTier || !occupied[tier];
}

void PerkState::validateSelections() const { validateSelections(selected); }
void PerkState::validateSelections(const std::vector<PerkSelection> & selections) const
{
	const auto & rules = getRules();
	if(!usesPerkRules(rules))
	{
		if(!selections.empty())
			throw std::runtime_error("Perk selections without a saved rules identity");
		return;
	}
	const auto cap = static_cast<size_t>(rules["maxPerksPerSkill"].Integer());
	std::set<std::pair<std::string, std::string>> identities;
	std::set<std::pair<std::string, int>> occupiedTiers;
	std::map<std::string, size_t> perSkill;
	std::vector<PerkSelection> strictPrefix;
	for(const auto & entry : selections)
	{
		// Saved strict selections must be a legal ordered prefix. Rank is not
		// stored here: validate tiers against Expert, then consumers use live rank.
		if(usesStrictPerkProgression(rules))
		{
			if(!canAcquirePerkInState(rules, strictPrefix, entry.skillId, entry.perkId, 3))
				throw std::runtime_error("Invalid saved strict New Horizons perk progression");
			strictPrefix.push_back(entry);
		}
		const auto * definition = acquisitionDefinition(rules, entry.skillId, entry.perkId);
		if(!definition || !identities.emplace(entry.skillId, entry.perkId).second
			|| (!usesR2PerkOffers(rules) && !occupiedTiers.emplace(entry.skillId, perkRequiredRank((*definition)["requires"].String())).second)
			|| ++perSkill[entry.skillId] > cap)
			throw std::runtime_error("Invalid saved New Horizons perk selection");
	}
}

PerkSelectionOperation::PerkSelectionOperation(const PerkState & value)
	: source(value), capturedSnapshot(value.capturedOwner()),
	  catalog(capturedSnapshot ? capturedSnapshot->admitted() : uncapturedCatalog.emplace(value.getRules())),
	  publishedSelections(value.selected), working(publishedSelections)
{
	source.validateSelections(working);
}

void PerkSelectionOperation::requireCurrentCapture() const
{
	if(source.capturedOwner() != capturedSnapshot || source.getSelected() != publishedSelections)
		throw std::logic_error("Perk operation source was replaced");
}

bool PerkSelectionOperation::canAcquire(const std::string & skill, const std::string & perk, int rank) const
{
	requireCurrentCapture();
	return source.canAcquirePerkInState(source.getRules(), working, skill, perk, rank);
}

bool PerkSelectionOperation::hasTier(const std::string & skill, int tier) const
{
	requireCurrentCapture();
	return std::any_of(working.begin(), working.end(), [&](const auto & choice)
	{
		if(choice.skillId != skill) return false;
		const auto definition = catalog.definition(skill, choice.perkId);
		return definition && perkRequiredRank(definition->requiredRank) == tier;
	});
}

std::vector<PerkOfferCandidate> PerkSelectionOperation::candidates(const std::string & skillId, int tier) const
{
	requireCurrentCapture();
	const auto & rules = source.getRules();
	if(!usesPerkRules(rules) || tier <= 0 || tier > 3)
		return {};
	const bool orderedTiers = usesUnifiedSkillProgression(rules) || usesExclusivePerkTiers(rules);
	if(orderedTiers && hasTier(skillId, tier))
		return {};
	const auto & skills = rules["skills"].Struct();
	const auto parent = skills.find(skillId);
	if(parent == skills.end()) return {};
	std::vector<PerkOfferCandidate> result;
	for(const auto & row : parent->second["perks"].Vector())
	{
		const auto & id = row["id"].String();
		if(row["effect"]["status"].String() != "active"
			|| std::any_of(working.begin(), working.end(), [&](const auto & choice)
				{ return choice.skillId == skillId && choice.perkId == id; }))
			continue;
		const auto rank = perkRequiredRank(row["requires"].String());
		if(rank != tier || (orderedTiers && !canAcquire(skillId, id, tier)))
			continue;
		result.push_back({{skillId, id}, row["name"].String(), row["description"].String(), rank});
	}
	return result;
}

std::vector<PerkOfferCandidate> PerkSelectionOperation::eligibleCandidates(const std::string & skill, int rank) const
{
	requireCurrentCapture();
	std::vector<PerkOfferCandidate> result;
	const auto & skills = source.getRules()["skills"].Struct();
	const auto parent = skills.find(skill);
	if(parent == skills.end()) return result;
	for(const auto & row : parent->second["perks"].Vector())
	{
		const auto & perk = row["id"].String();
		if(canAcquire(skill, perk, rank))
			result.push_back({{skill, perk}, row["name"].String(), row["description"].String(),
				perkRequiredRank(row["requires"].String())});
	}
	return result;
}

std::vector<PerkOfferCandidate> PerkSelectionOperation::next(
	const std::function<int(const std::string &)> & rankLookup) const
{
	requireCurrentCapture();
	if(!usesPersistentHeroProgression(source.getRules())) return {};
	for(const auto & [skill, definition] : source.getRules()["skills"].Struct())
	{
		const int rank = rankLookup(skill);
		if(rank < 0 || rank > 3)
			throw std::runtime_error("Invalid Skill rank during mandatory perk reconciliation");
		for(int tier = 1; tier <= rank; ++tier)
		{
			if(hasTier(skill, tier)) continue;
			auto result = candidates(skill, tier);
			if(result.empty())
				throw std::runtime_error("Acquired Skill rank has no eligible mandatory perk");
			return result;
		}
	}
	return {};
}

void PerkSelectionOperation::select(const std::string & skill, const std::string & perk, int rank)
{
	requireCurrentCapture();
	if(committed)
		throw std::logic_error("Perk operation already committed");
	const auto & rules = source.getRules();
	if(usesStrictPerkProgression(rules))
	{
		if(!canAcquire(skill, perk, rank))
			throw std::runtime_error("Unavailable strict New Horizons perk selection");
	}
	else
	{
		if(rank < 0 || rank > 3)
			throw std::runtime_error("Invalid New Horizons skill rank");
		const auto definition = catalog.definition(skill, perk);
		if(!definition || definition->effect["status"].String() != "active"
			|| rank < perkRequiredRank(definition->requiredRank)
			|| std::any_of(working.begin(), working.end(), [&](const auto & choice)
				{ return choice.skillId == skill && choice.perkId == perk; }))
			throw std::runtime_error("Unavailable New Horizons perk selection");
		const int tier = perkRequiredRank(definition->requiredRank);
		for(int previous = 1; previous < tier; ++previous)
			if(!hasTier(skill, previous))
				throw std::runtime_error("Earlier New Horizons perk tier is still required");
		if(!usesR2PerkOffers(rules) && hasTier(skill, tier))
			throw std::runtime_error("New Horizons perk tier already occupied for skill");
		const auto count = std::count_if(working.begin(), working.end(), [&](const auto & choice)
			{ return choice.skillId == skill; });
		if(count >= rules["maxPerksPerSkill"].Integer())
			throw std::runtime_error("New Horizons perk cap reached for skill");
	}
	auto next = working;
	next.push_back({skill, perk});
	source.validateSelections(next);
	working = std::move(next);
}

void PerkSelectionOperation::commit(PerkState & destination)
{
	requireCurrentCapture();
	if(committed || &destination != &source)
		throw std::logic_error("Perk operation cannot commit into another captured state");
	source.validateSelections(working);
	destination.replaceSelected(std::move(working));
	committed = true;
}

void PerkState::select(const std::string & skill, const std::string & perk, int rank)
{
	PerkSelectionOperation operation(*this);
	operation.select(skill, perk, rank);
	operation.commit(*this);
}
}
