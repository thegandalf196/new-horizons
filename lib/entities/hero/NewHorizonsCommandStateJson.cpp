/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsCommandState.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace newHorizonsHeroes
{
namespace
{
int64_t integer(const JsonNode & node, int64_t minimum, int64_t maximum)
{
	if(node.getType() != JsonNode::JsonType::DATA_INTEGER || node.Integer() < minimum || node.Integer() > maximum)
		throw std::runtime_error("Invalid Command acquisition JSON integer");
	return node.Integer();
}
void fields(const JsonNode & node, std::initializer_list<std::string_view> keys)
{
	if(!node.isStruct() || node.Struct().size() != keys.size())
		throw std::runtime_error("Invalid Command acquisition JSON fields");
	for(const auto key : keys)
		if(!node.Struct().contains(std::string(key)))
			throw std::runtime_error("Missing Command acquisition JSON field");
}
}

JsonNode CommandState::toJson(const std::optional<CommandRules> & rules) const
{
	validate(rules);
	JsonNode result;
	if(!rules)
		return result;
	result["version"].Integer() = 1;
	result["highestCommand"].Integer() = highestCommand;
	result["lastCaptureLevel"].Integer() = lastCaptureLevel;
	result["awards"].Vector();
	for(const auto & award : awards)
	{
		JsonNode row;
		row["milestone"].Integer() = award.milestone;
		row["recipient"].Integer() = static_cast<int>(award.recipient);
		row["acquisitionLevel"].Integer() = award.acquisitionLevel;
		row["enhancement"] = award.enhancement
			? JsonNode(std::string(COMMAND_ENHANCEMENT_KEYS.at(static_cast<size_t>(*award.enhancement)))) : JsonNode();
		row["contribution"].Integer() = award.contribution;
		result["awards"].Vector().push_back(std::move(row));
	}
	return result;
}

CommandState CommandState::fromJson(const JsonNode & node, const std::optional<CommandRules> & rules)
{
	CommandState result;
	if(node.isNull())
	{
		if(rules)
			throw std::runtime_error("Captured army Command hero is missing acquisition history");
		return result;
	}
	fields(node, {"version", "highestCommand", "lastCaptureLevel", "awards"});
	integer(node["version"], 1, 1);
	result.highestCommand = integer(node["highestCommand"], 0, std::numeric_limits<int64_t>::max());
	result.lastCaptureLevel = static_cast<uint32_t>(integer(node["lastCaptureLevel"], 0, std::numeric_limits<uint32_t>::max()));
	if(!node["awards"].isVector())
		throw std::runtime_error("Command acquisition JSON awards must be an array");
	for(const auto & row : node["awards"].Vector())
	{
		fields(row, {"milestone", "recipient", "acquisitionLevel", "enhancement", "contribution"});
		CommandEnhancementAward award;
		award.milestone = integer(row["milestone"], 6, std::numeric_limits<int64_t>::max());
		award.recipient = static_cast<CommandRecipient>(integer(row["recipient"], 0, 4));
		award.acquisitionLevel = static_cast<uint32_t>(integer(row["acquisitionLevel"], 1, std::numeric_limits<uint32_t>::max()));
		award.contribution = integer(row["contribution"], 0, std::numeric_limits<int64_t>::max());
		if(!row["enhancement"].isNull())
		{
			if(!row["enhancement"].isString())
				throw std::runtime_error("Command enhancement JSON identity must be a string or null");
			const auto found = std::find(COMMAND_ENHANCEMENT_KEYS.begin(), COMMAND_ENHANCEMENT_KEYS.end(), row["enhancement"].String());
			if(found == COMMAND_ENHANCEMENT_KEYS.end())
				throw std::runtime_error("Unknown Command enhancement JSON identity");
			award.enhancement = static_cast<CommandEnhancement>(found - COMMAND_ENHANCEMENT_KEYS.begin());
		}
		result.awards.push_back(std::move(award));
	}
	result.validate(rules);
	return result;
}
}
