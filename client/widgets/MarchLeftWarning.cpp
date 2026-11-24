/*
 * Part of VCMI / New Horizons.
 * Authors: listed in AUTHORS. GPL-2.0-or-later; see license.txt.
 */
#include "StdInc.h"
#include "MarchLeftWarning.h"

#include "../CPlayerInterface.h"
#include "../GameInstance.h"

#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/texts/MetaString.h"

namespace marchLeftWarning
{
Transfers move(const CArmedInstance * from, SlotID fromSlot, const CArmedInstance * to, SlotID toSlot)
{
	return {{from, fromSlot, to, toSlot, false}};
}

Transfers swap(const CArmedInstance * first, SlotID firstSlot, const CArmedInstance * second, SlotID secondSlot)
{
	return {{first, firstSlot, second, secondSlot, true}, {second, secondSlot, first, firstSlot, true}};
}

Transfers mergeOrSwap(const CArmedInstance * from, SlotID fromSlot, const CArmedInstance * to, SlotID toSlot)
{
	const auto * moving = from ? from->getCreature(fromSlot) : nullptr;
	const auto * resident = to ? to->getCreature(toSlot) : nullptr;
	if(resident && resident != moving)
		return swap(from, fromSlot, to, toSlot);
	return move(from, fromSlot, to, toSlot);
}

Transfers wholeArmy(const CArmedInstance * from, const CArmedInstance * to)
{
	Transfers result;
	if(!from || !to)
		return result;
	for(const auto & slot : from->Slots())
		result.push_back({from, slot.first, to, to->getSlotFor(slot.second->getCreature()), false});
	return result;
}

std::string describe(const Transfers & transfers)
{
	const int day = GAME->interface()->cb->getCalendar().getCurrentDay();
	std::string result;
	for(const auto & effect : newHorizonsMarchLeft::projectTransfers(transfers, day))
	{
		MetaString line = MetaString::createFromTextID("new-horizons.marchLeft.exchangeWarning");
		line.replaceTokenRawString("%PERCENT%", std::to_string(newHorizonsMarchLeft::percentLeft(effect.troopMarchLeft)));
		line.replaceTokenRawString("%HERO%", GAME->translator().translate(effect.hero->getNameTextID()));
		line.replaceTokenRawString("%LEFT%", std::to_string(effect.movementAfter));
		line.replaceTokenRawString("%WAS%", std::to_string(effect.movementBefore));
		if(!result.empty())
			result += "\n\n";
		result += line.toString(&GAME->translator());
	}
	return result;
}

void confirm(const Transfers & transfers, const std::function<void()> & send)
{
	const auto warning = describe(transfers);
	if(warning.empty())
	{
		send();
		return;
	}
	GAME->interface()->showYesNoDialog(warning, send, nullptr);
}
}
