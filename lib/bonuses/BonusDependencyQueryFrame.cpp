/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BonusDependencyQueryFrame.h"
namespace bonusDependencies
{
bool OwnedBonusQueryFrame::matches(uint32_t id, int32_t count, CreatureID creature) const noexcept
{
	return context.recipientId == id && context.count == count && context.creature == creature;
}
TConstBonusListPtr OwnedBonusQueryFrame::query(const CSelector & selector, bool beforeStacking) const
{
	// The returned list keeps nested rule/value owners alive across frame switches,
	// program retirement and destruction of the original native source graph.
	struct Loan
	{
		std::shared_ptr<const OwnedBonusQueryFrame> owner;
		BonusList selected;
	};
	auto loan = std::make_shared<Loan>();
	loan->owner = shared_from_this();
	const auto & source = beforeStacking ? unstacked : stacked;
	source->getBonuses(loan->selected, selector);
	return TConstBonusListPtr(loan, &loan->selected);
}
TConstBonusListPtr OwnedBonusQueryFrame::getAllBonuses(const CSelector & selector, const std::string &) const
{
	return query(selector, false);
}
TConstBonusListPtr OwnedBonusQueryFrame::getUnstackedBonuses(const CSelector & selector) const
{
	return query(selector, true);
}
bool OwnedBonusQueryFrame::mayHaveBonusType(BonusType type) const
{
	const auto index = static_cast<size_t>(type);
	return index >= rawTypes.size() || rawTypes[index];
}
}
