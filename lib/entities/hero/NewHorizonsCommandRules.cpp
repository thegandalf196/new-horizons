/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsCommandRules.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace newHorizonsHeroes
{
namespace
{
void fields(const JsonNode & node, std::initializer_list<std::string_view> keys)
{
	if(!node.isStruct() || node.Struct().size() != keys.size())
		throw std::runtime_error("Invalid army Command policy fields");
	for(const auto key : keys)
		if(!node.Struct().contains(std::string(key)))
			throw std::runtime_error("Missing army Command policy field: " + std::string(key));
}
int64_t integer(const JsonNode & node, int64_t minimum, int64_t maximum)
{
	if(node.getType() != JsonNode::JsonType::DATA_INTEGER || node.Integer() < minimum || node.Integer() > maximum)
		throw std::runtime_error("Invalid army Command policy integer");
	return node.Integer();
}
CommandRules numericalPolicy(const JsonNode & node)
{
	if(integer(node["version"], 1, 1) != 1)
		throw std::runtime_error("Unsupported army Command policy");
	CommandRules result;
	const auto & bonuses = node["authorityRankBonuses"];
	if(!bonuses.isVector() || bonuses.Vector().size() != 4)
		throw std::runtime_error("Army Command requires four Authority rank bonuses");
	for(size_t i = 0; i < 4; ++i)
		result.authorityRankBonuses[i] = static_cast<int32_t>(integer(bonuses.Vector()[i], 0, 1000));
	if(!node["enhancements"].isStruct() || node["enhancements"].Struct().size() != COMMAND_ENHANCEMENT_KEYS.size()
		|| !node["authorityPerks"].isStruct() || node["authorityPerks"].Struct().size() != AUTHORITY_PERK_KEYS.size())
		throw std::runtime_error("Army Command requires the exact enhancement and Authority pools");
	for(size_t i = 0; i < COMMAND_ENHANCEMENT_KEYS.size(); ++i)
	{
		const auto & formula = node["enhancements"][std::string(COMMAND_ENHANCEMENT_KEYS[i])];
		fields(formula, {"levelDivisor", "multiplier"});
		result.enhancements[i] = {static_cast<uint32_t>(integer(formula["levelDivisor"], 1, 1000000)),
			integer(formula["multiplier"], 0, 1000000)};
	}
	for(size_t i = 0; i < AUTHORITY_PERK_KEYS.size(); ++i)
		result.authorityPerkValues[i] = static_cast<int32_t>(integer(node["authorityPerks"][std::string(AUTHORITY_PERK_KEYS[i])], 0, 1000000));
	result.validate();
	return result;
}
}

void CommandRules::validate() const
{
	if(levelInterval == 0 || levelInterval > 1000000 || authorityRankBonuses[0] != 0)
		throw std::runtime_error("Invalid army Command progression");
	int32_t previous = 0;
	for(const auto bonus : authorityRankBonuses)
	{
		if(bonus < previous || bonus > 1000)
			throw std::runtime_error("Invalid Authority rank bonus");
		previous = bonus;
	}
	for(const auto & formula : enhancements)
		if(formula.levelDivisor == 0 || formula.levelDivisor > 1000000 || formula.multiplier < 0 || formula.multiplier > 1000000)
			throw std::runtime_error("Invalid Command enhancement formula");
	for(const auto value : authorityPerkValues)
		if(value < 0 || value > 1000000)
			throw std::runtime_error("Invalid Authority passive value");
}

void validateArmyCommandPolicy(const JsonNode & policy, bool resolved)
{
	if(policy.isStruct() && policy.Struct().contains("defaultStartingArmyVersion"))
	{
		fields(policy, {"version", resolved ? "levelInterval" : "classLevelIntervals",
			"authorityRankBonuses", "enhancements", "authorityPerks", "defaultStartingArmyVersion"});
		integer(policy["defaultStartingArmyVersion"], 1, 1);
	}
	else
		fields(policy, {"version", resolved ? "levelInterval" : "classLevelIntervals",
			"authorityRankBonuses", "enhancements", "authorityPerks"});
	auto parsed = numericalPolicy(policy);
	if(resolved)
	{
		parsed.levelInterval = static_cast<uint32_t>(integer(policy["levelInterval"], 1, 1000000));
		parsed.validate();
		return;
	}
	const auto & intervals = policy["classLevelIntervals"];
	if(!intervals.isStruct() || intervals.Struct().empty())
		throw std::runtime_error("Army Command requires class progression intervals");
	for(const auto & [key, interval] : intervals.Struct())
	{
		const auto separator = key.find(':');
		if(separator == std::string::npos || separator == 0 || separator + 1 == key.size()
			|| key.find(':', separator + 1) != std::string::npos)
			throw std::runtime_error("Army Command requires scoped class identities");
		integer(interval, 1, 1000000);
	}
}

JsonNode resolveArmyCommandPolicy(const JsonNode & policy, std::string_view classKey)
{
	validateArmyCommandPolicy(policy, false);
	const auto & interval = policy["classLevelIntervals"][std::string(classKey)];
	integer(interval, 1, 1000000);
	auto result = policy;
	result.Struct().erase("classLevelIntervals");
	result["levelInterval"] = interval;
	validateArmyCommandPolicy(result, true);
	return result;
}

std::optional<CommandRules> capturedCommandRules(const JsonNode & capabilities)
{
	if(!capabilities.isStruct() || !capabilities.Struct().contains("armyCommand"))
		return std::nullopt;
	const auto & policy = capabilities["armyCommand"];
	validateArmyCommandPolicy(policy, true);
	auto result = numericalPolicy(policy);
	result.levelInterval = static_cast<uint32_t>(policy["levelInterval"].Integer());
	return result;
}

void validateArmyCommandSerialization(const JsonNode & capabilities, bool supported)
{
	if(!capabilities.isStruct() || !capabilities.Struct().contains("armyCommand"))
		return;
	if(!supported)
		throw std::runtime_error("Army Command requires a new game/save format");
	const auto & policy = capabilities["armyCommand"];
	validateArmyCommandPolicy(policy, !policy.Struct().contains("classLevelIntervals"));
}

CommandValue commandValue(const CommandRules & rules, uint32_t heroLevel, int authorityRank)
{
	rules.validate();
	if(heroLevel == 0 || authorityRank < 0 || authorityRank > 3)
		throw std::invalid_argument("Invalid Command hero level or Authority rank");
	return {1 + (static_cast<int64_t>(heroLevel) - 1) / rules.levelInterval, rules.authorityRankBonuses[authorityRank]};
}

CommandSlotView commandSlotView(int64_t command, SlotID slot)
{
	if(command < 1 || !slot.validSlot())
		throw std::invalid_argument("Invalid Command or army slot");
	const int index = slot.getNum();
	const auto grade = index < 3 ? CommandSlotGrade::CORE : index < 6 ? CommandSlotGrade::ELITE : CommandSlotGrade::CHAMPION;
	const int unlock = index < 3 ? 1 : index - 1;
	const int firstEnhancement = index < 3 ? 6 : index + 4;
	return {grade, unlock, command >= unlock, command < firstEnhancement ? 0 : 1 + (command - firstEnhancement) / 5};
}

CommandRecipient commandMilestoneRecipient(int64_t milestone)
{
	if(milestone < 6)
		throw std::invalid_argument("Command enhancement milestones start at six");
	return static_cast<CommandRecipient>((milestone - 6) % 5);
}

bool commandRecipientContains(CommandRecipient recipient, SlotID slot)
{
	if(!slot.validSlot() || static_cast<unsigned>(recipient) > static_cast<unsigned>(CommandRecipient::CHAMPION))
		throw std::invalid_argument("Invalid Command recipient or army slot");
	return recipient == CommandRecipient::CORE_BLOCK ? slot.getNum() < 3 : slot.getNum() == static_cast<int>(recipient) + 2;
}

int64_t commandEnhancementValue(const CommandRules & rules, CommandEnhancement enhancement, uint32_t level)
{
	rules.validate();
	const auto index = static_cast<size_t>(enhancement);
	if(level == 0 || index >= rules.enhancements.size())
		throw std::invalid_argument("Invalid Command enhancement or acquisition level");
	const auto & formula = rules.enhancements[index];
	return (1 + (static_cast<int64_t>(level) - 1) / formula.levelDivisor) * formula.multiplier;
}

CommandModifiers commandEnhancementModifiers(CommandEnhancement enhancement, int64_t value)
{
	if(value < 0)
		throw std::invalid_argument("Negative Command enhancement contribution");
	CommandModifiers result;
	switch(enhancement)
	{
	case CommandEnhancement::READINESS: result.initiative = value; break;
	case CommandEnhancement::MOBILITY: result.speed = value; break;
	case CommandEnhancement::COUNTERATTACK: result.retaliations = value; break;
	case CommandEnhancement::WEAPON_DRILL: result.attack = value; break;
	case CommandEnhancement::GUARD_DRILL: result.defense = value; break;
	case CommandEnhancement::FORTUNE: result.luck = value; break;
	case CommandEnhancement::COURAGE: result.morale = value; break;
	case CommandEnhancement::ENDURANCE: result.maximumHealthPercent = value; break;
	case CommandEnhancement::WARDING: result.magicalReductionPercent = value; break;
	default: throw std::invalid_argument("Unknown Command enhancement");
	}
	return result;
}

void CommandModifiers::add(const CommandModifiers & other)
{
	constexpr std::array members = {&CommandModifiers::initiative, &CommandModifiers::speed, &CommandModifiers::retaliations,
		&CommandModifiers::attack, &CommandModifiers::defense, &CommandModifiers::luck, &CommandModifiers::morale,
		&CommandModifiers::maximumHealthPercent, &CommandModifiers::magicalReductionPercent, &CommandModifiers::directAttackDamagePercent};
	for(const auto member : members)
		if(this->*member < 0 || other.*member < 0 || this->*member > std::numeric_limits<int64_t>::max() - other.*member)
			throw std::overflow_error("Command modifier accumulation overflow");
	for(const auto member : members)
		this->*member += other.*member;
}

CommandModifiers authoritySlotModifiers(const CommandRules & rules, SlotID slot, const std::array<bool, 10> & active)
{
	rules.validate();
	const auto grade = commandSlotView(1, slot).grade;
	const auto value = [&](AuthorityPerk perk)
	{
		const auto index = static_cast<size_t>(perk);
		return active[index] ? rules.authorityPerkValues[index] : 0;
	};
	CommandModifiers result;
	if(grade == CommandSlotGrade::CORE)
	{
		result.attack = value(AuthorityPerk::OFFENSIVE_DRILLS);
		result.defense = value(AuthorityPerk::DEFENSIVE_DRILLS);
		result.maximumHealthPercent = value(AuthorityPerk::CONDITIONING);
		result.initiative = value(AuthorityPerk::BATTLE_READINESS);
		result.directAttackDamagePercent = value(AuthorityPerk::VETERAN_CORPS);
	}
	else
	{
		result.directAttackDamagePercent = value(AuthorityPerk::GRAND_MARSHAL);
		if(grade == CommandSlotGrade::ELITE)
			result.attack = result.defense = value(AuthorityPerk::OFFICER_CORPS);
	}
	result.speed = value(AuthorityPerk::MANEUVER_TRAINING);
	result.retaliations = value(AuthorityPerk::COUNTERATTACK_TRAINING);
	result.magicalReductionPercent = value(AuthorityPerk::PROTECTIVE_TRAINING);
	return result;
}
}
