/*
 * HypotheticExportSubject.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "InitialExportBattleSubject.h"
#include <stdexcept>

std::shared_ptr<InitialExportBattleSubject> HypotheticBattle::initialExportSubject() const
{
	return std::dynamic_pointer_cast<InitialExportBattleSubject>(subject);
}

battle::Units HypotheticBattle::ownedExportUnitsIf(const battle::UnitFilter & predicate) const
{
	const auto initial = initialExportSubject();
	if(!initial)
		throw std::logic_error("Owned export roster requires its branch subject");
	initial->synchronizeValues(*this);
	battle::Units result;
	std::set<uint32_t> originals;
	// Every escaped Unit view must enter the branch query route, not expose a
	// scratch arena unit whose provider could miss a later direct scalar edit.
	for(size_t index = 0; index < initial->world().size(); ++index)
	{
		const auto id = initial->world().sourceAt(index).unitId;
		if(!originals.insert(id).second)
			throw std::invalid_argument("Owned sequence requires unique packet identities");
		const auto unit = const_cast<HypotheticBattle *>(this)->getForUpdate(id);
		if(predicate(unit.get()))
			result.push_back(unit.get());
	}
	for(const auto & [id, unit] : stackStates)
		if(!originals.contains(id) && predicate(unit.get()))
			result.push_back(unit.get()); // Never silently drop a newly projected unit.
	return result;
}
