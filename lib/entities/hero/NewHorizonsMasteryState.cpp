/*
 * NewHorizonsMasteryState.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsMasteryState.h"
#include "NewHorizonsHeroRules.h"
#include "../../constants/Enumerations.h"
#include <limits>
#include <set>
#include <stdexcept>

namespace newHorizonsHeroes
{
bool MasteryState::hasChoice(SecondarySkill skill) const
{
	return std::any_of(selected.begin(), selected.end(), [skill](const auto & entry) { return entry.skill == skill; });
}

void MasteryState::validate() const
{
	validateMasteryRules(rules);
	if(!usesRules(rules))
	{
		if(lastSequence || eligibilityLevel || artilleryEligible || logisticsEligible || pending || !selected.empty())
			throw std::runtime_error("Mastery progression without a saved rules identity");
		return;
	}
	if(lastSequence > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
		throw std::runtime_error("Mastery sequence exceeds lossless crossover representation");
	if((eligibilityLevel != 0 && eligibilityLevel < 2) || (artilleryEligible && eligibilityLevel == 0)
		|| (artilleryEligible && hasChoice(SecondarySkill::ARTILLERY)))
		throw std::runtime_error("Invalid mastery eligibility level or duplicate eligibility");
	if(logisticsEligible && (eligibilityLevel == 0 || hasChoice(SecondarySkill::LOGISTICS)
		|| !masteryOptions(rules, SecondarySkill::LOGISTICS)))
		throw std::runtime_error("Invalid Logistics mastery eligibility");
	if(!pending && lastSequence != (selected.empty() ? 0 : selected.back().sequence))
		throw std::runtime_error("Mastery sequence without a pending or completed choice");
	std::set<SecondarySkill> seen;
	uint64_t previousSequence = 0;
	for(const auto & entry : selected)
	{
		const auto options = masteryOptions(rules, entry.skill);
		if(!options || !seen.insert(entry.skill).second || entry.level < 2
			|| entry.sequence <= previousSequence || entry.sequence > lastSequence
			|| std::find(options->begin(), options->end(), entry.option) == options->end())
			throw std::runtime_error("Invalid saved mastery selection");
		previousSequence = entry.sequence;
	}
	if(pending)
	{
		validateMasteryOffer(*pending);
		const auto options = masteryOptions(rules, pending->skill);
		if(!options || pending->options != *options || pending->sequence != lastSequence
			|| pending->sequence <= previousSequence
			|| pending->level != eligibilityLevel
			|| !(pending->skill == SecondarySkill::ARTILLERY ? artilleryEligible : logisticsEligible)
			|| hasChoice(pending->skill))
			throw std::runtime_error("Invalid saved pending mastery offer");
	}
}

namespace
{
int64_t savedInteger(const JsonNode & node, int64_t minimum, int64_t maximum)
{
	if(node.getType() != JsonNode::JsonType::DATA_INTEGER || node.Integer() < minimum || node.Integer() > maximum)
		throw std::runtime_error("Invalid integral mastery crossover field");
	return node.Integer();
}

void savedFields(const JsonNode & node, std::initializer_list<std::string_view> keys)
{
	if(!node.isStruct())
		throw std::runtime_error("Mastery crossover object required");
	for(const auto & [key, value] : node.Struct())
		if(std::find(keys.begin(), keys.end(), key) == keys.end())
			throw std::runtime_error("Unknown mastery crossover field " + key);
}
}

JsonNode MasteryState::toJson() const
{
	validate();
	if(!usesRules(rules))
		return JsonNode();
	JsonNode result;
	const bool logistics = rules["rulesetVersion"].Integer() == 2;
	result["stateVersion"].Integer() = logistics ? 2 : 1;
	if(logistics)
		result["logisticsEligible"].Bool() = logisticsEligible;
	result["rules"] = rules;
	result["lastSequence"].Integer() = static_cast<int64_t>(lastSequence);
	result["eligibilityLevel"].Integer() = eligibilityLevel;
	result["artilleryEligible"].Bool() = artilleryEligible;
	result["selected"].Vector();
	for(const auto & entry : selected)
	{
		JsonNode node;
		node["skill"].Integer() = entry.skill.getNum();
		node["id"].String() = entry.option.id.value;
		node["level"].Integer() = entry.level;
		node["sequence"].Integer() = static_cast<int64_t>(entry.sequence);
		result["selected"].Vector().push_back(std::move(node));
	}
	if(pending)
	{
		auto & node = result["pending"];
		node["hero"].Integer() = pending->hero.getNum();
		node["player"].Integer() = pending->player.getNum();
		node["skill"].Integer() = pending->skill.getNum();
		node["level"].Integer() = pending->level;
		node["sequence"].Integer() = static_cast<int64_t>(pending->sequence);
	}
	return result;
}

MasteryState MasteryState::fromJson(const JsonNode & node)
{
	MasteryState result;
	if(node.isNull() || (node.isStruct() && node.Struct().empty()))
		return result;
	const auto version = savedInteger(node["stateVersion"], 1, 2);
	if(version == 1)
		savedFields(node, {"stateVersion", "rules", "lastSequence", "eligibilityLevel", "artilleryEligible", "selected", "pending"});
	else
		savedFields(node, {"stateVersion", "rules", "lastSequence", "eligibilityLevel", "artilleryEligible", "logisticsEligible", "selected", "pending"});
	result.rules = node["rules"];
	validateMasteryRules(result.rules);
	const JsonNode & savedRules = result.rules;
	if((usesRules(savedRules) && savedRules["rulesetVersion"].Integer() != version)
		|| (!usesRules(savedRules) && version != 1))
		throw std::runtime_error("Mastery crossover version/rules mismatch");
	if(version == 2)
	{
		if(!node["logisticsEligible"].isBool())
			throw std::runtime_error("Invalid Logistics crossover eligibility");
		result.logisticsEligible = node["logisticsEligible"].Bool();
	}
	result.lastSequence = savedInteger(node["lastSequence"], 0, std::numeric_limits<int64_t>::max());
	result.eligibilityLevel = static_cast<uint32_t>(savedInteger(node["eligibilityLevel"], 0, std::numeric_limits<uint32_t>::max()));
	if(!node["artilleryEligible"].isBool() || !node["selected"].isVector())
		throw std::runtime_error("Invalid mastery crossover eligibility or choices");
	result.artilleryEligible = node["artilleryEligible"].Bool();
	for(const auto & entry : node["selected"].Vector())
	{
		savedFields(entry, {"skill", "id", "level", "sequence"});
		MasterySelection selection;
		selection.skill = SecondarySkill(static_cast<int>(savedInteger(entry["skill"], 0, std::numeric_limits<int32_t>::max())));
		const auto options = masteryOptions(result.rules, selection.skill);
		if(!options || !entry["id"].isString())
			throw std::runtime_error("Mastery crossover choice without saved options");
		const auto found = std::find_if(options->begin(), options->end(), [&](const auto & option) { return option.id.value == entry["id"].String(); });
		if(found == options->end())
			throw std::runtime_error("Unknown saved mastery choice");
		selection.option = *found; // Resolve from the saved registry, never installed defaults.
		selection.level = static_cast<uint32_t>(savedInteger(entry["level"], 2, std::numeric_limits<uint32_t>::max()));
		selection.sequence = savedInteger(entry["sequence"], 1, std::numeric_limits<int64_t>::max());
		result.selected.push_back(std::move(selection));
	}
	if(!node["pending"].isNull())
	{
		const auto & entry = node["pending"];
		savedFields(entry, {"hero", "player", "skill", "level", "sequence"});
		MasteryOffer offer;
		offer.hero = ObjectInstanceID(static_cast<int>(savedInteger(entry["hero"], 0, std::numeric_limits<int32_t>::max())));
		offer.player = PlayerColor(static_cast<int>(savedInteger(entry["player"], 0, PlayerColor::PLAYER_LIMIT_I - 1)));
		offer.skill = SecondarySkill(static_cast<int>(savedInteger(entry["skill"], 0, std::numeric_limits<int32_t>::max())));
		offer.level = static_cast<uint32_t>(savedInteger(entry["level"], 2, std::numeric_limits<uint32_t>::max()));
		offer.sequence = savedInteger(entry["sequence"], 1, std::numeric_limits<int64_t>::max());
		const auto options = masteryOptions(result.rules, offer.skill);
		if(!options)
			throw std::runtime_error("Pending mastery crossover without saved rules");
		offer.options = *options;
		result.pending = std::move(offer);
	}
	result.validate();
	return result;
}

void MasteryState::captureBeforeLevel(uint32_t nextLevel, int artilleryRank, int logisticsRank)
{
	validate();
	if(!usesRules(rules))
		return;
	if(pending || nextLevel < 2 || nextLevel <= eligibilityLevel)
		throw std::runtime_error("Cannot advance mastery eligibility with a pending or stale level");
	eligibilityLevel = nextLevel;
	artilleryEligible = artilleryRank == MasteryLevel::EXPERT && !hasChoice(SecondarySkill::ARTILLERY);
	logisticsEligible = logisticsRank == MasteryLevel::EXPERT && !hasChoice(SecondarySkill::LOGISTICS)
		&& masteryOptions(rules, SecondarySkill::LOGISTICS).has_value();
}

std::optional<MasteryOffer> MasteryState::prepareOffer(ObjectInstanceID hero, PlayerColor player, uint32_t level) const
{
	validate();
	if((!artilleryEligible && !logisticsEligible) || eligibilityLevel != level)
		return std::nullopt;
	// Campaign placement may legitimately assign a new object ID/owner. Rebind
	// the identical saved choices with a fresh sequence, never adopt live rules.
	if(pending && pending->hero == hero && pending->player == player)
		return std::nullopt;
	if(lastSequence == static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
		throw std::runtime_error("Mastery offer sequence exhausted");
	MasteryOffer result;
	result.hero = hero;
	result.player = player;
	result.skill = pending ? pending->skill : SecondarySkill(artilleryEligible ? SecondarySkill::ARTILLERY : SecondarySkill::LOGISTICS);
	result.level = level;
	result.sequence = lastSequence + 1;
	result.options = *masteryOptions(rules, result.skill);
	validateMasteryOffer(result);
	return result;
}

void MasteryState::applyOffer(const MasteryOffer & offer)
{
	const auto expected = prepareOffer(offer.hero, offer.player, offer.level);
	if(!expected || expected->sequence != offer.sequence || expected->skill != offer.skill
		|| expected->options != offer.options)
		throw std::runtime_error("Mastery offer does not match saved eligibility");
	pending = offer;
	lastSequence = offer.sequence;
}

MasteryReplyError MasteryState::accept(ObjectInstanceID hero, PlayerColor player, uint64_t sequence,
	uint32_t level, int skillRank, int choice)
{
	validate();
	if(!pending)
		return MasteryReplyError::STALE_OFFER;
	const auto error = validateMasteryReply(*pending, hero, player, sequence, level, skillRank,
		hasChoice(pending->skill), choice);
	if(error != MasteryReplyError::NONE)
		return error;
	selected.push_back({pending->skill, pending->options[choice], level, sequence});
	if(pending->skill == SecondarySkill::ARTILLERY)
		artilleryEligible = false;
	else
		logisticsEligible = false;
	pending.reset();
	return MasteryReplyError::NONE;
}
}
