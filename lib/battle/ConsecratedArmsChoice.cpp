/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ConsecratedArmsChoice.h"
#include "ConsecratedArmsOrder.h"
#include "CastleSpecialtyPrayers.h"
#include "BattleAction.h"
#include "CBattleInfoCallback.h"
#include "HeroOrderEffects.h"
#include "HeroOrderRecipients.h"
#include "IBattleState.h"
#include "../mapObjects/CGHeroInstance.h"
#include <algorithm>

namespace consecratedArms
{
namespace
{
bool hasChoiceFields(const BattleAction & action)
{
	return !action.prayerChoices.empty() || action.prayerRecipient != BattleAction::NO_PRAYER_RECIPIENT;
}

std::optional<std::string> chosenRefusal(const BattleAction & action,
	const std::vector<uint32_t> & eligible, size_t required)
{
	if(eligible.empty())
	{
		if(hasChoiceFields(action))
			return std::string("This Order has no eligible prayer recipient to choose");
		return std::nullopt;
	}
	if(action.prayerChoices.size() != required)
		return std::string(required == 2
			? "Antiphonal Prayer requires two different chosen prayers"
			: "Consecrated Arms requires exactly one chosen prayer");
	for(size_t index = 0; index < action.prayerChoices.size(); ++index)
	{
		if(action.prayerChoices[index] > static_cast<uint8_t>(Prayer::HEAVENS_JUDGMENT))
			return std::string("Unknown chosen prayer");
		if(index > 0 && action.prayerChoices[index] == action.prayerChoices[0])
			return std::string("Chosen prayers must be different");
	}
	if(!std::binary_search(eligible.begin(), eligible.end(), action.prayerRecipient))
		return std::string("The prayer recipient must be one of this Order's recipients");
	return std::nullopt;
}

/// Sermon grants exactly one chosen prayer, whatever the Order perks.
constexpr size_t SERMON_PRAYERS = 1;
}

std::vector<uint32_t> eligiblePrayerRecipients(const CBattleInfoCallback & battle,
	BattleSide side, const HeroOrderState & order)
{
	return prayerHolders(battle, side, heroCommands::recipientIds(battle, side, order));
}

size_t requiredPrayerChoices(const Policy & policy)
{
	if(!policy.rank || !policy.chosen)
		return 0;
	return policy.has(Perk::ANTIPHONAL_PRAYER) ? 2 : 1;
}

std::vector<Prayer> chosenPrayerList(const std::vector<uint8_t> & choices)
{
	std::vector<Prayer> result;
	for(const auto choice : choices)
	{
		if(choice > static_cast<uint8_t>(Prayer::HEAVENS_JUDGMENT))
			throw std::runtime_error("Unknown chosen prayer");
		result.push_back(static_cast<Prayer>(choice));
	}
	return result;
}

std::optional<std::string> choiceRefusal(const CBattleInfoCallback & battle,
	const BattleAction & action, const std::optional<HeroOrderState> & prepared)
{
	if(castleSpecialtyPrayers::sermonCast(battle, action))
		return chosenRefusal(action, castleSpecialtyPrayers::sermonRecipients(battle, action.side), SERMON_PRAYERS);
	const bool order = action.actionType == EActionType::HERO_COMMAND;
	const bool sideOk = action.side == BattleSide::ATTACKER || action.side == BattleSide::DEFENDER;
	const auto policy = order && sideOk && prepared
		? capturePolicy(battle.battleGetFightingHero(action.side)) : Policy{};
	if(!requiredPrayerChoices(policy))
	{
		if(hasChoiceFields(action))
			return std::string("Prayer choices are valid only for Chosen Prayers Orders");
		return std::nullopt;
	}
	return chosenRefusal(action, eligiblePrayerRecipients(battle, action.side, *prepared),
		requiredPrayerChoices(policy));
}

ChoiceRequest choiceRequest(const CBattleInfoCallback & battle, const BattleAction & action)
{
	ChoiceRequest result;
	if(castleSpecialtyPrayers::sermonCast(battle, action))
	{
		result.policy = capturePolicy(battle.battleGetFightingHero(action.side));
		result.recipients = castleSpecialtyPrayers::sermonRecipients(battle, action.side);
		result.prayers = result.recipients.empty() ? 0 : SERMON_PRAYERS;
		return result;
	}
	if(action.actionType != EActionType::HERO_COMMAND
		|| (action.side != BattleSide::ATTACKER && action.side != BattleSide::DEFENDER))
		return result;
	result.policy = capturePolicy(battle.battleGetFightingHero(action.side));
	const auto required = requiredPrayerChoices(result.policy);
	if(!required)
		return result;
	std::vector<uint32_t> targets;
	for(const auto & target : action.target)
		if(target.unitValue >= 0)
			targets.push_back(static_cast<uint32_t>(target.unitValue));
	const auto prepared = heroCommands::prepareOrderEffects(battle, action.side, action.command, targets);
	if(!prepared)
		return result;
	result.recipients = eligiblePrayerRecipients(battle, action.side, prepared->order);
	if(!result.recipients.empty())
		result.prayers = required;
	return result;
}
}
