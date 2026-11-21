/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/ConsecratedArmsChoice.h"
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

/// Consecrated Arms 4 (Chosen Prayers) picker state. Pure presentation state:
/// it never touches the battle; `apply` only fills the request fields of a
/// copied action after rechecking them against a freshly computed request.
namespace consecratedArmsPrayerChoice
{
using consecratedArms::Prayer;

class Selection
{
	size_t required = 0;
	std::vector<uint32_t> eligible;
	std::vector<Prayer> prayers;
	std::optional<uint32_t> recipient;

public:
	Selection(size_t requiredPrayers, std::vector<uint32_t> eligibleRecipients)
		: required(requiredPrayers), eligible(std::move(eligibleRecipients))
	{
		// A single possible recipient needs no second decision.
		if(eligible.size() == 1)
			recipient = eligible.front();
	}

	size_t requiredPrayers() const { return required; }
	const std::vector<uint32_t> & recipients() const { return eligible; }
	const std::vector<Prayer> & chosenPrayers() const { return prayers; }
	std::optional<uint32_t> chosenRecipient() const { return recipient; }

	bool chosen(Prayer prayer) const
	{
		return std::find(prayers.begin(), prayers.end(), prayer) != prayers.end();
	}
	bool prayersComplete() const { return required != 0 && prayers.size() == required; }
	bool complete() const { return prayersComplete() && recipient.has_value(); }

	/// A single required prayer behaves as a radio choice; with two required
	/// prayers an unchosen prayer is unavailable until one is deselected.
	bool canToggle(Prayer prayer) const
	{
		return chosen(prayer) || prayers.size() < required || required == 1;
	}
	bool togglePrayer(Prayer prayer)
	{
		if(!canToggle(prayer))
			return false;
		if(const auto found = std::find(prayers.begin(), prayers.end(), prayer); found != prayers.end())
			prayers.erase(found);
		else
		{
			if(required == 1)
				prayers.clear();
			prayers.push_back(prayer);
		}
		return true;
	}

	/// The recipient follows the prayer choice, so it opens once prayers are complete.
	bool canChooseRecipient(uint32_t id) const
	{
		return prayersComplete() && std::find(eligible.begin(), eligible.end(), id) != eligible.end();
	}
	bool chooseRecipient(uint32_t id)
	{
		if(!canChooseRecipient(id))
			return false;
		recipient = id;
		return true;
	}

	/// Fills `action` only when the selection still matches the current request.
	bool apply(BattleAction & action, const consecratedArms::ChoiceRequest & current) const
	{
		if(!complete() || current.prayers != required
			|| !std::binary_search(current.recipients.begin(), current.recipients.end(), *recipient))
			return false;
		action.prayerChoices.clear();
		for(const auto prayer : prayers)
			action.prayerChoices.push_back(static_cast<uint8_t>(prayer));
		action.prayerRecipient = *recipient;
		return true;
	}
};

inline std::string promptText(const Selection & selection)
{
	if(!selection.prayersComplete())
	{
		if(selection.requiredPrayers() == 2)
			return selection.chosenPrayers().empty() ? "Choose two different prayers."
				: "Choose one more prayer.";
		return "Choose one prayer.";
	}
	if(!selection.chosenRecipient())
		return "Choose the stack that receives the prayer"
			+ std::string(selection.requiredPrayers() == 2 ? "s." : ".");
	return "Ready. Confirm to issue the Order.";
}
}
