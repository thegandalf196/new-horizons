/* Part of VCMI engine; GNU General Public License v2.0 or later. */
#include "StdInc.h"
#include "EstatesPacks.h"
#include "NetPackVisitor.h"

void SetObjectProperty::validateEstatesProperty(bool supported) const
{
	if(what != ObjProperty::NEW_HORIZONS_HOARDER_LAST_WEEK
		&& what != ObjProperty::NEW_HORIZONS_COMPANY_DEFENSE_WEEK
		&& what != ObjProperty::NEW_HORIZONS_COMPANY_DEFENSE_OWNER
		&& what != ObjProperty::NEW_HORIZONS_COMPANY_DEFENSE_SLOT)
		return;
	if(!supported)
		throw std::runtime_error("Estates property requires revised Estates format");
	if(what == ObjProperty::NEW_HORIZONS_COMPANY_DEFENSE_OWNER)
	{
		const PlayerColor owner(identifier.getNum());
		if(identifier != ObjPropertyID(owner) || !owner.isValidPlayer())
			throw std::runtime_error("Invalid defensive Company owner property");
		return;
	}
	const int lower = what == ObjProperty::NEW_HORIZONS_HOARDER_LAST_WEEK ? -1 : 0;
	if(identifier != ObjPropertyID(NumericID(identifier.getNum())) || identifier.getNum() < lower
		|| (what == ObjProperty::NEW_HORIZONS_COMPANY_DEFENSE_SLOT && !SlotID(identifier.getNum()).validSlot()))
		throw std::runtime_error("Invalid defensive Company or Hoarder property");
}

void EstatesKingdomChanged::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitEstatesKingdomChanged(*this);
}

void AssignTownSteward::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitAssignTownSteward(*this);
}
