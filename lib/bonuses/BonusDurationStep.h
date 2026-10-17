/*
 * BonusDurationStep.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "BonusList.h"
#include "CBonusSystemNode.h"

namespace bonusDuration
{
/// One node's ORIGINAL exported snapshot, then decrement/removal in list order.
/// Recursion stays with the native/projected graph owner; diamond paths must
/// not be deduplicated. Storage changes identity only for private COW values.
template<class Storage>
void reduceSelected(const BonusList & exported, const CSelector & selector, Storage & storage)
{
	BonusList selected;
	exported.getBonuses(selected, selector);
	for(const auto & record : selected)
	{
		const auto current = storage.decrement(record);
		if(current->turnsRemain <= 0)
			storage.remove(current);
	}
}

struct NativeStorage
{
	CBonusSystemNode & node;
	std::shared_ptr<Bonus> decrement(const std::shared_ptr<Bonus> & record)
	{
		record->turnsRemain--;
		return record;
	}
	void remove(const std::shared_ptr<Bonus> & record) { node.removeBonus(record); }
};
}
