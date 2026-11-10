/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsCommandState.h"

namespace newHorizonsHeroes
{
struct DLL_LINKAGE CommandEnhancementOption
{
	CommandEnhancement enhancement = CommandEnhancement::READINESS;
	int64_t contribution = 0;
	bool operator==(const CommandEnhancementOption &) const = default;
	template<typename Handler> void serialize(Handler & h)
	{
		h & enhancement;
		h & contribution;
	}
};

/// Immutable server offer, reconstructed solely from a saved entitlement and its captured rules.
struct DLL_LINKAGE CommandEnhancementOffer
{
	ObjectInstanceID hero = ObjectInstanceID::NONE;
	PlayerColor player;
	int64_t milestone = 0;
	uint32_t acquisitionLevel = 0;
	CommandRecipient recipient = CommandRecipient::CORE_BLOCK;
	std::array<CommandEnhancementOption, 9> options;
	bool operator==(const CommandEnhancementOffer &) const = default;
	template<typename Handler> void serialize(Handler & h)
	{
		h & hero;
		h & player;
		h & milestone;
		h & acquisitionLevel;
		h & recipient;
		h & options;
	}
};

DLL_LINKAGE std::optional<CommandEnhancementOffer> prepareCommandEnhancementOffer(
	ObjectInstanceID hero, PlayerColor player, const CommandState & state, const std::optional<CommandRules> & rules);
/// Full envelope and earliest-entitlement validation; never changes the supplied state.
DLL_LINKAGE bool validCommandEnhancementReply(const CommandEnhancementOffer & offer,
	ObjectInstanceID hero, PlayerColor player, int64_t milestone, int choice,
	const CommandState & state, const std::optional<CommandRules> & rules, int64_t currentCommand);
}
