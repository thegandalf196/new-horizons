/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsCommandState.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace newHorizonsHeroes
{
void CommandState::validate(const std::optional<CommandRules> & rules) const
{
	if(!rules)
	{
		if(!empty())
			throw std::runtime_error("Command receipts without captured army Command rules");
		return;
	}
	rules->validate();
	if(empty())
		return; // Not initialized is distinct from initialized without enhancement awards.
	if(lastCaptureLevel == 0 || highestCommand < 1
		|| highestCommand < commandValue(*rules, lastCaptureLevel, 0).effective()
		|| highestCommand > commandValue(*rules, lastCaptureLevel, 3).effective()
		|| static_cast<uint64_t>(std::max<int64_t>(0, highestCommand - 5)) != awards.size())
		throw std::runtime_error("Invalid Command progression high-water receipt");
	uint32_t previousLevel = 0;
	bool pendingSeen = false;
	for(size_t i = 0; i < awards.size(); ++i)
	{
		const auto & award = awards[i];
		if(award.milestone != static_cast<int64_t>(i) + 6
			|| award.recipient != commandMilestoneRecipient(award.milestone)
			|| award.acquisitionLevel == 0 || award.acquisitionLevel < previousLevel
			|| award.acquisitionLevel > lastCaptureLevel
			|| award.milestone > commandValue(*rules, award.acquisitionLevel, 3).effective())
			throw std::runtime_error("Invalid Command enhancement entitlement");
		previousLevel = award.acquisitionLevel;
		if(!award.enhancement)
		{
			if(award.contribution != 0)
				throw std::runtime_error("Unselected Command enhancement has a contribution");
			pendingSeen = true;
		}
		else if(pendingSeen || award.contribution != commandEnhancementValue(*rules, *award.enhancement, award.acquisitionLevel))
			throw std::runtime_error("Invalid or rescaled Command enhancement award");
	}
}

void CommandState::validateSerialization(const std::optional<CommandRules> & rules, bool supported) const
{
	validate(rules);
	if(!supported && (rules || !empty()))
		throw std::runtime_error("Cannot discard army Command policy or acquisition history");
}

size_t CommandState::capture(const CommandRules & rules, uint32_t level, int authorityRank)
{
	validate(rules);
	const auto command = commandValue(rules, level, authorityRank).effective();
	if(level < lastCaptureLevel)
		throw std::runtime_error("Command acquisition cannot move backwards in hero level");
	// Reserve/copy before publishing: allocation or malformed input cannot partly earn a batch.
	auto candidate = *this;
	const auto previousCount = awards.size();
	const auto nextHighest = std::max(highestCommand, command);
	for(int64_t milestone = std::max<int64_t>(6, highestCommand + 1); milestone <= nextHighest; ++milestone)
		candidate.awards.push_back({milestone, commandMilestoneRecipient(milestone), level, std::nullopt, 0});
	candidate.highestCommand = nextHighest;
	candidate.lastCaptureLevel = level;
	candidate.validate(rules);
	*this = std::move(candidate);
	return awards.size() - previousCount;
}

std::optional<CommandEnhancementAward> CommandState::nextPending() const
{
	const auto found = std::ranges::find_if(awards, [](const auto & award) { return !award.enhancement; });
	return found == awards.end() ? std::nullopt : std::optional<CommandEnhancementAward>(*found);
}

void CommandState::select(const CommandRules & rules, int64_t milestone, CommandEnhancement enhancement)
{
	validate(rules);
	const auto pending = nextPending();
	if(!pending || pending->milestone != milestone)
		throw std::runtime_error("Command choice is stale, already selected or out of order");
	const auto contribution = commandEnhancementValue(rules, enhancement, pending->acquisitionLevel);
	auto & award = awards.at(static_cast<size_t>(milestone - 6));
	award.enhancement = enhancement;
	award.contribution = contribution;
}

CommandSlotModifiers CommandState::modifiers(const CommandRules & rules, int64_t currentCommand, SlotID slot,
	const std::array<bool, 10> & activeAuthorityPerks) const
{
	validate(rules);
	CommandSlotModifiers result;
	if(!commandSlotView(currentCommand, slot).unlocked)
		return result;
	for(const auto & award : awards)
		if(award.enhancement && award.milestone <= currentCommand && commandRecipientContains(award.recipient, slot))
			result.enhancements.add(commandEnhancementModifiers(*award.enhancement, award.contribution));
	result.authority = authoritySlotModifiers(rules, slot, activeAuthorityPerks);
	return result;
}

CommandModifiers CommandSlotModifiers::additiveReadback() const
{
	auto result = enhancements;
	result.add(authority);
	return result;
}

int CommandSlotModifiers::boundedEnhancementWardingPercent() const
{
	return static_cast<int>(std::clamp<int64_t>(enhancements.magicalReductionPercent, 0, 100));
}
}
