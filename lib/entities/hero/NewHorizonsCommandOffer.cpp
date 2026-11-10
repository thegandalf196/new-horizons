/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsCommandOffer.h"

namespace newHorizonsHeroes
{
std::optional<CommandEnhancementOffer> prepareCommandEnhancementOffer(ObjectInstanceID hero,
	PlayerColor player, const CommandState & state, const std::optional<CommandRules> & rules)
{
	state.validate(rules);
	const auto pending = state.nextPending();
	if(!rules || !pending)
		return std::nullopt;
	CommandEnhancementOffer offer;
	offer.hero = hero;
	offer.player = player;
	offer.milestone = pending->milestone;
	offer.acquisitionLevel = pending->acquisitionLevel;
	offer.recipient = pending->recipient;
	for(size_t i = 0; i < offer.options.size(); ++i)
	{
		const auto enhancement = static_cast<CommandEnhancement>(i);
		offer.options[i] = {enhancement, commandEnhancementValue(*rules, enhancement, pending->acquisitionLevel)};
	}
	return offer;
}

bool validCommandEnhancementReply(const CommandEnhancementOffer & offer, ObjectInstanceID hero,
	PlayerColor player, int64_t milestone, int choice, const CommandState & state,
	const std::optional<CommandRules> & rules, int64_t currentCommand)
{
	if(choice < 0 || choice >= static_cast<int>(offer.options.size()) || hero != offer.hero
		|| player != offer.player || milestone != offer.milestone || milestone > currentCommand)
		return false;
	const auto expected = prepareCommandEnhancementOffer(hero, player, state, rules);
	return expected && *expected == offer;
}
}
