/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "ConsecratedArms.h"
#include "BattleSide.h"
#include "HeroCommand.h"
#include <optional>
#include <string>
#include <vector>

class BattleAction;
class CBattleInfoCallback;

/// Consecrated Arms 4 (Chosen Prayers, captured divineMandatePolicy 4): the
/// commander chooses one prayer (two different ones with Antiphonal Prayer) and
/// one recipient among the Order's recipients. Version 3 keeps its random draw
/// and must carry no choice fields.
namespace consecratedArms
{
/// Order recipients that can actually hold a prayer, sorted and unique.
DLL_LINKAGE std::vector<uint32_t> eligiblePrayerRecipients(const CBattleInfoCallback & battle,
	BattleSide side, const HeroOrderState & order);
/// Number of prayers a version 4 Order must choose for this policy (0 when it has none).
DLL_LINKAGE size_t requiredPrayerChoices(const Policy & policy);
/// Decodes already admitted wire choices.
DLL_LINKAGE std::vector<Prayer> chosenPrayerList(const std::vector<uint8_t> & choices);
/// What a version 4 Order action (or a Sermon Light spell action) still has to choose, computed from the same
/// prepared Order snapshot the authority uses. `prayers` is 0 when the action
/// must carry no choice (no Chosen Prayers policy, illegal Order, or no
/// eligible recipient); `recipients` is then empty.
struct DLL_LINKAGE ChoiceRequest
{
	size_t prayers = 0;
	std::vector<uint32_t> recipients;
	Policy policy;
};
DLL_LINKAGE ChoiceRequest choiceRequest(const CBattleInfoCallback & battle, const BattleAction & action);
/// Authority admission before StartAction. `prepared` is the prepared Order
/// snapshot (empty for non-Order actions or non-canonical rules).
DLL_LINKAGE std::optional<std::string> choiceRefusal(const CBattleInfoCallback & battle,
	const BattleAction & action, const std::optional<HeroOrderState> & prepared);
}
