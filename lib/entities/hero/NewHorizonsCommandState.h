/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsCommandRules.h"
#include "../../serializer/ESerializationVersion.h"
#include <vector>

namespace newHorizonsHeroes
{
struct DLL_LINKAGE CommandEnhancementAward
{
	int64_t milestone = 0;
	CommandRecipient recipient = CommandRecipient::CORE_BLOCK;
	uint32_t acquisitionLevel = 0;
	std::optional<CommandEnhancement> enhancement;
	int64_t contribution = 0;
	bool operator==(const CommandEnhancementAward &) const = default;
	template<typename Handler> void serialize(Handler & h)
	{
		h & milestone;
		h & recipient;
		h & acquisitionLevel;
		h & enhancement;
		h & contribution;
	}
};

struct DLL_LINKAGE CommandSlotModifiers
{
	CommandModifiers enhancements;
	CommandModifiers authority;
	/// UI readback only. Damage must retain the independent enhancement/Authority
	/// mitigation sources, not turn this sum into one magical-reduction factor.
	CommandModifiers additiveReadback() const;
	int boundedEnhancementWardingPercent() const;
};

/// Persistent receipts only; reading, copying and loading never earn a milestone.
/// Authoritative advancement must call capture once per coherent level/rank event.
/// Do not bootstrap loaded heroes by calling capture at their final current level.
class DLL_LINKAGE CommandState
{
	int64_t highestCommand = 0;
	uint32_t lastCaptureLevel = 0;
	std::vector<CommandEnhancementAward> awards;
public:
	int64_t getHighestCommand() const { return highestCommand; }
	uint32_t getLastCaptureLevel() const { return lastCaptureLevel; }
	const std::vector<CommandEnhancementAward> & getAwards() const { return awards; }
	bool empty() const { return highestCommand == 0 && lastCaptureLevel == 0 && awards.empty(); }
	void validate(const std::optional<CommandRules> & rules) const;
	void validateSerialization(const std::optional<CommandRules> & rules, bool supported) const;
	/// Returns number of newly earned choices. Repeated/lowered eligibility is idempotent.
	size_t capture(const CommandRules & rules, uint32_t level, int authorityRank);
	std::optional<CommandEnhancementAward> nextPending() const;
	/// Earliest pending milestone only; rejection leaves every receipt unchanged.
	void select(const CommandRules & rules, int64_t milestone, CommandEnhancement enhancement);
	CommandSlotModifiers modifiers(const CommandRules & rules, int64_t currentCommand, SlotID slot,
		const std::array<bool, 10> & activeAuthorityPerks) const;
	JsonNode toJson(const std::optional<CommandRules> & rules) const;
	static CommandState fromJson(const JsonNode & node, const std::optional<CommandRules> & rules);
	bool operator==(const CommandState &) const = default;

	template<typename Handler> void serialize(Handler & h, const std::optional<CommandRules> & rules)
	{
		const bool supported = h.hasFeature(Handler::Version::NEW_HORIZONS_COMMAND_AUTHORITY);
		if(h.saving)
			validateSerialization(rules, supported);
		if(!supported)
		{
			if(!h.saving)
			{
				CommandState candidate;
				candidate.validateSerialization(rules, false);
				*this = std::move(candidate);
			}
			return;
		}
		if(h.saving)
		{
			h & highestCommand;
			h & lastCaptureLevel;
			h & awards;
		}
		else
		{
			CommandState candidate;
			h & candidate.highestCommand;
			h & candidate.lastCaptureLevel;
			h & candidate.awards;
			candidate.validate(rules);
			*this = std::move(candidate);
		}
	}
};
}
