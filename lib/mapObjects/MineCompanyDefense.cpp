/* Part of VCMI engine; GNU General Public License v2.0 or later. */
#include "StdInc.h"
#include "MiscObjects.h"
#include "../CCreatureHandler.h"
#include "../callback/IGameEventCallback.h"
#include "../networkPacks/ObjProperty.h"
#include "../networkPacks/StackLocation.h"

#include <limits>

bool CGMine::isCompanySlot(SlotID slot) const
{
	return slot.validSlot() && slot == companyDefenseSlot
		&& companyDefenseOwner.isValidPlayer() && companyDefenseOwner == getOwner();
}

bool CGMine::hasCompanyDefenders() const
{
	return isCompanySlot(companyDefenseSlot) && hasStackAtSlot(companyDefenseSlot);
}

void CGMine::validateCompanyDefenseSerialization(bool supported) const
{
	if(companyDefenseLastWeek < -1 || companyDefenseSlot.getNum() < -1
		|| (companyDefenseSlot.getNum() != -1 && !companyDefenseSlot.validSlot())
		|| (companyDefenseSlot.validSlot() && (!companyDefenseOwner.isValidPlayer()
			|| companyDefenseOwner != getOwner() || companyDefenseLastWeek < 0))
		|| (companyDefenseOwner != PlayerColor::NEUTRAL && !companyDefenseOwner.isValidPlayer()))
		throw std::runtime_error("Invalid defensive Mercenary Company provenance");
	if(companyDefenseSlot.validSlot() && hasStackAtSlot(companyDefenseSlot)
		&& getStack(companyDefenseSlot).getCreatureID() != CreatureID(CreatureID::decode("core:rogue")))
		throw std::runtime_error("Defensive Company provenance must refer to a Rogue stack");
	if(!supported && (companyDefenseLastWeek != -1 || companyDefenseOwner != PlayerColor::NEUTRAL
		|| companyDefenseSlot.getNum() != -1))
		throw std::runtime_error("Defensive Mercenary Company requires revised Estates format");
}

void CGMine::grantWeeklyCompanyDefenders(IGameEventCallback & events, int32_t week, int32_t level) const
{
	validateCompanyDefenseSerialization(true);
	if(week < 0 || level < 0)
		throw std::runtime_error("Invalid defensive Mercenary Company weekly grant");
	if(!getOwner().isValidPlayer() || week <= companyDefenseLastWeek)
		return;
	events.setObjPropertyValue(id, ObjProperty::NEW_HORIZONS_COMPANY_DEFENSE_WEEK, week);
	if(level == 0)
		return;
	const CreatureID rogue(CreatureID::decode("core:rogue"));
	const int64_t cap = std::min<int64_t>(std::numeric_limits<TQuantity>::max(), static_cast<int64_t>(level) * 3);
	const int64_t existing = hasCompanyDefenders() ? getStackCount(companyDefenseSlot) : 0;
	const auto addition = static_cast<TQuantity>(std::min<int64_t>(level, std::max<int64_t>(0, cap - existing)));
	if(addition == 0)
		return;
	if(hasCompanyDefenders())
	{
		if(getStack(companyDefenseSlot).getCreatureID() != rogue)
			throw std::runtime_error("Company defender slot no longer contains Rogues");
		if(!events.changeStackCount(StackLocation(id, companyDefenseSlot), addition, ChangeValueMode::RELATIVE))
			throw std::runtime_error("Validated Company defender reinforcement failed");
		return;
	}
	const auto free = getFreeSlots();
	if(free.empty())
		return; // Preserve seven authored guards. No owed/recruitable debt is created.
	const SlotID slot = isCompanySlot(companyDefenseSlot) && !hasStackAtSlot(companyDefenseSlot)
		? companyDefenseSlot : free.front();
	if(!events.insertNewStack(StackLocation(id, slot), rogue.toCreature(), addition))
		throw std::runtime_error("Validated Company defender insertion failed");
	events.setObjPropertyID(id, ObjProperty::NEW_HORIZONS_COMPANY_DEFENSE_OWNER, getOwner());
	events.setObjPropertyValue(id, ObjProperty::NEW_HORIZONS_COMPANY_DEFENSE_SLOT, slot.getNum());
}
