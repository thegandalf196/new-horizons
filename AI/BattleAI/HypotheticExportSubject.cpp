/*
 * HypotheticExportSubject.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "InitialExportBattleSubject.h"
#include "ProjectedAdjacencyBonuses.h"
#include <stdexcept>

HypotheticSourceBinding::HypotheticSourceBinding(const std::shared_ptr<CBattleInfoCallback> & subject,
	const CBattleInfoCallback & policySource)
	: boundSubjectPointer(subject.get()),
	boundSubjectOwner(subject),
	initialExportView(dynamic_cast<InitialExportBattleSubject *>(subject.get())),
	adjacency(ProjectedAdjacencyBonuses::captureSources(policySource))
{
}

bool HypotheticSourceBinding::matches(const std::shared_ptr<CBattleInfoCallback> & subject) const
{
	return !(subject.get() != boundSubjectPointer || boundSubjectOwner.expired()
		|| boundSubjectOwner.owner_before(subject) || subject.owner_before(boundSubjectOwner));
}

std::shared_ptr<InitialExportBattleSubject> HypotheticSourceBinding::resolve(
	const std::shared_ptr<CBattleInfoCallback> & subject) const
{
	if(!matches(subject))
		return std::dynamic_pointer_cast<InitialExportBattleSubject>(subject);
	if(!initialExportView)
		return {};
	return std::shared_ptr<InitialExportBattleSubject>(subject, initialExportView);
}

std::shared_ptr<InitialExportBattleSubject> HypotheticBattle::initialExportSubject() const
{
	return sourceBinding.resolve(subject);
}

battle::Units HypotheticBattle::getUnitsIf(const battle::UnitFilter & predicate) const
{
	if(initialExportSubject())
		return ownedExportUnitsIf(predicate);
	const auto original = BattleProxy::getUnitsIf([](const battle::Unit *)
	{
		return true;
	});
	battle::Units result;
	result.reserve(original.size() + stackStates.size());
	std::set<uint32_t> originalIds;
	for(const auto * unit : original)
	{
		originalIds.insert(unit->unitId());
		const auto replacement = stackStates.find(unit->unitId());
		const auto * current = replacement == stackStates.end() ? unit : replacement->second.get();
		if(predicate(current))
			result.push_back(current);
	}
	// Preserve real battle order for replacements; append only newly projected units.
	for(const auto & [id, unit] : stackStates)
		if(!originalIds.contains(id) && predicate(unit.get()))
			result.push_back(unit.get());
	return result;
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
		const auto unit = const_cast<HypotheticBattle *>(this)->getForUpdateImpl(id);
		if(predicate(unit.get()))
			result.push_back(unit.get());
	}
	for(const auto & [id, unit] : stackStates)
		if(!originals.contains(id) && predicate(unit.get()))
			result.push_back(unit.get()); // Never silently drop a newly projected unit.
	return result;
}
