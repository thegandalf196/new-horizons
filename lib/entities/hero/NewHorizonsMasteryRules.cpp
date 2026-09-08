/*
 * NewHorizonsMasteryRules.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsMasteryRules.h"
#include "NewHorizonsHeroRules.h"
#include "../../GameLibrary.h"
#include "../../callback/IGameInfoCallback.h"
#include "../../modding/IdentifierStorage.h"
#include "../../modding/ModScope.h"
#include <cmath>
#include "../../constants/StringConstants.h"
#include "../../constants/Enumerations.h"

#include <set>
#include <stdexcept>

const JsonNode & IGameInfoCallback::getHeroMasteryRules() const
{
	static const JsonNode legacy;
	return legacy;
}

namespace newHorizonsHeroes
{
SecondarySkill masteryParentSkill(MasteryEffect effect)
{
	switch(effect)
	{
	case MasteryEffect::ARTILLERY_VOLLEY:
	case MasteryEffect::ARTILLERY_PRECISION:
	case MasteryEffect::ARTILLERY_REPAIR:
		return SecondarySkill::ARTILLERY;
	case MasteryEffect::LOGISTICS_FORCED_MARCH:
	case MasteryEffect::LOGISTICS_QUARTERMASTER:
	case MasteryEffect::LOGISTICS_PATHFINDER:
		return SecondarySkill::LOGISTICS;
	default:
		throw std::runtime_error("Unsupported mastery effect");
	}
}

void validateMasteryOption(const MasteryOption & option)
{
	const auto prefix = GameConstants::NEW_HORIZONS_MOD_SCOPE + ":";
	if(!option.id.value.starts_with(prefix) || option.id.value.size() == prefix.size()
		|| option.nameTextId.empty() || option.descriptionTextId.empty() || option.iconKey.empty())
		throw std::runtime_error("Invalid New Horizons mastery option identity or presentation");
	switch(option.effect)
	{
	case MasteryEffect::ARTILLERY_VOLLEY:
		if(option.magnitude < 1 || option.magnitude > 8)
			throw std::runtime_error("Invalid mastery extra shot count");
		break;
	case MasteryEffect::ARTILLERY_PRECISION:
	case MasteryEffect::LOGISTICS_QUARTERMASTER:
		if(option.magnitude != 1)
			throw std::runtime_error("Precision mastery is a capability, not a damage percentage");
		break;
	case MasteryEffect::LOGISTICS_FORCED_MARCH:
	case MasteryEffect::LOGISTICS_PATHFINDER:
		if(option.magnitude < 1 || option.magnitude > 10000)
			throw std::runtime_error("Invalid mastery movement point amount");
		break;
	case MasteryEffect::ARTILLERY_REPAIR:
		if(option.magnitude < 1 || option.magnitude > 1000000)
			throw std::runtime_error("Invalid mastery repair amount");
		break;
	default:
		throw std::runtime_error("Unsupported mastery effect");
	}
}

std::string formatMasteryDescription(const MasteryOption & option, std::string localizedTemplate)
{
	validateMasteryOption(option);
	constexpr std::string_view token = "{magnitude}";
	const auto value = std::to_string(option.magnitude);
	size_t position = 0;
	while((position = localizedTemplate.find(token, position)) != std::string::npos)
	{
		localizedTemplate.replace(position, token.size(), value);
		position += value.size();
	}
	return localizedTemplate;
}

void validateMasteryOptions(const std::array<MasteryOption, 3> & options)
{
	std::set<std::string> ids;
	std::set<MasteryEffect> effects;
	for(const auto & option : options)
	{
		validateMasteryOption(option);
		if(masteryParentSkill(option.effect) != masteryParentSkill(options.front().effect))
			throw std::runtime_error("Mixed mastery families");
		if(!ids.insert(option.id.value).second || !effects.insert(option.effect).second)
			throw std::runtime_error("Duplicate mastery ID or effect");
	}
}

namespace
{
void require(bool valid, const std::string & message)
{
	if(!valid)
		throw std::runtime_error("Invalid New Horizons mastery rules: " + message);
}

void fields(const JsonNode & node, std::initializer_list<std::string_view> allowed)
{
	require(node.isStruct(), "object required");
	for(const auto & [key, value] : node.Struct())
		require(std::find(allowed.begin(), allowed.end(), key) != allowed.end(), "unknown field " + key);
}

bool integer(const JsonNode & node, int minimum, int maximum)
{
	return node.isNumber() && std::isfinite(node.Float()) && node.Float() >= minimum
		&& node.Float() <= maximum && std::floor(node.Float()) == node.Float();
}

SecondarySkill parentSkill(const std::string & key)
{
	const auto separator = key.find(':');
	require(separator != std::string::npos && separator > 0 && separator + 1 < key.size(), "scoped parent skill");
	const auto id = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), SecondarySkill::entityType(), key, true);
	require(id.has_value() && (*id == SecondarySkill::ARTILLERY || *id == SecondarySkill::LOGISTICS), "unsupported parent skill " + key);
	return SecondarySkill(*id);
}

std::array<MasteryOption, 3> parseOptions(const JsonNode & family)
{
	fields(family, {"options"});
	const auto & nodes = family["options"];
	require(nodes.isVector() && nodes.Vector().size() == 3, "exactly three options required");
	std::array<MasteryOption, 3> result;
	for(size_t i = 0; i < result.size(); ++i)
	{
		const auto & node = nodes.Vector()[i];
		fields(node, {"id", "effect", "magnitude", "nameTextId", "descriptionTextId", "iconKey"});
		for(const auto * key : {"id", "effect", "nameTextId", "descriptionTextId", "iconKey"})
			require(node[key].isString() && !node[key].String().empty(), std::string("nonempty ") + key);
		require(integer(node["magnitude"], 1, 1000000), "integral effect magnitude");
		auto & option = result[i];
		option.id.value = node["id"].String();
		option.magnitude = static_cast<int>(node["magnitude"].Integer());
		option.nameTextId = node["nameTextId"].String();
		option.descriptionTextId = node["descriptionTextId"].String();
		option.iconKey = node["iconKey"].String();
		const auto & effect = node["effect"].String();
		if(effect == "volley")
			option.effect = MasteryEffect::ARTILLERY_VOLLEY;
		else if(effect == "precision")
			option.effect = MasteryEffect::ARTILLERY_PRECISION;
		else if(effect == "repair")
			option.effect = MasteryEffect::ARTILLERY_REPAIR;
		else if(effect == "forcedMarch")
			option.effect = MasteryEffect::LOGISTICS_FORCED_MARCH;
		else if(effect == "quartermaster")
			option.effect = MasteryEffect::LOGISTICS_QUARTERMASTER;
		else if(effect == "pathfinder")
			option.effect = MasteryEffect::LOGISTICS_PATHFINDER;
		else
			throw std::runtime_error("Unknown mastery effect " + effect);
	}
	validateMasteryOptions(result);
	return result;
}
}

void validateMasteryRules(const JsonNode & rules)
{
	if(!usesRules(rules))
		return;
	fields(rules, {"schemaVersion", "rulesetVersion", "skills"});
	require(integer(rules["schemaVersion"], 1, 1), "schemaVersion");
	require(integer(rules["rulesetVersion"], 1, 2), "rulesetVersion");
	const bool logistics = rules["rulesetVersion"].Integer() == 2;
	require(rules["skills"].isStruct() && rules["skills"].Struct().size() == (logistics ? 2 : 1), "exact mastery family coverage required");
	std::set<SecondarySkill> skills;
	std::set<std::string> identities;
	for(const auto & [key, value] : rules["skills"].Struct())
	{
		const auto skill = parentSkill(key);
		require(logistics || skill == SecondarySkill::ARTILLERY, "version1 Artillery only");
		require(skills.insert(skill).second, "duplicate parent skill");
		const auto options = parseOptions(value);
		require(masteryParentSkill(options.front().effect) == skill, "effect does not belong to parent skill");
		for(const auto & option : options)
			require(identities.insert(option.id.value).second, "duplicate mastery identity across families");
	}
}

std::optional<std::array<MasteryOption, 3>> masteryOptions(const JsonNode & rules, SecondarySkill skill)
{
	validateMasteryRules(rules);
	if(!usesRules(rules))
		return std::nullopt;
	for(const auto & [key, value] : rules["skills"].Struct())
		if(parentSkill(key) == skill)
			return parseOptions(value);
	return std::nullopt;
}

void validateMasteryOffer(const MasteryOffer & offer)
{
	if(offer.hero.getNum() < 0 || !offer.player.isValidPlayer()
		|| (offer.skill != SecondarySkill::ARTILLERY && offer.skill != SecondarySkill::LOGISTICS)
		|| offer.level < 2 || offer.sequence == 0)
		throw std::runtime_error("Invalid New Horizons mastery offer identity");
	validateMasteryOptions(offer.options);
	if(masteryParentSkill(offer.options.front().effect) != offer.skill)
		throw std::runtime_error("Mastery offer family mismatch");
}

MasteryReplyError validateMasteryReply(const MasteryOffer & offer, ObjectInstanceID hero,
	PlayerColor player, uint64_t sequence, uint32_t currentLevel, int currentSkillRank,
	bool alreadyChosen, int choice)
{
	validateMasteryOffer(offer);
	if(hero != offer.hero)
		return MasteryReplyError::WRONG_HERO;
	if(player != offer.player)
		return MasteryReplyError::WRONG_PLAYER;
	if(sequence != offer.sequence || currentLevel != offer.level)
		return MasteryReplyError::STALE_OFFER;
	if(choice < 0 || choice >= static_cast<int>(offer.options.size()))
		return MasteryReplyError::INVALID_CHOICE;
	if(currentSkillRank != MasteryLevel::EXPERT)
		return MasteryReplyError::NOT_EXPERT;
	if(alreadyChosen)
		return MasteryReplyError::ALREADY_CHOSEN;
	return MasteryReplyError::NONE;
}
}
