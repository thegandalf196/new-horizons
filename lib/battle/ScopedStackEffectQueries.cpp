/* Part of VCMI. Authors: see AUTHORS. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ScopedStackEffectQueryData.h"
#include "BattleInfo.h"
#include "StackEffectRefresh.h"
#include "../CStack.h"
#include "../bonuses/BonusSelector.h"
#include <algorithm>
#include <stdexcept>

namespace battle
{
ScopedStackEffectQueries::ScopedStackEffectQueries(const BattleInfo & battle, const std::vector<const CStack *> & targets)
	: data(std::make_unique<Data>(battle, targets)) {}
ScopedStackEffectQueries::~ScopedStackEffectQueries() = default;
const Unit & ScopedStackEffectQueries::current(uint32_t id) const { return data->target(id).state; }
const Unit * ScopedStackEffectQueries::find(uint32_t id) const
{
	const Unit * result = nullptr;
	for(const auto * unit : data->roster->orderedUnits())
		if(unit->unitId() == id)
		{
			if(result) return nullptr; // Canonical source/recipient lookup requires exactly one occurrence.
			result = unit;
		}
	return result;
}
CUnitState & ScopedStackEffectQueries::state(uint32_t id) { return data->target(id).state; }
const BonusList & ScopedStackEffectQueries::exportedRecords(uint32_t id) const { return data->storage(data->target(id).source).exported; }
AccessibilityInfo ScopedStackEffectQueries::accessibility(uint32_t id) const { return data->accessibility(id); }

void ScopedStackEffectQueries::appendExported(uint32_t id, const std::shared_ptr<Bonus> & original)
{
	const auto & root = data->target(id).source;
	auto & storage = data->storage(root);
	if(std::find(storage.exported.begin(), storage.exported.end(), original) != storage.exported.end())
		throw std::invalid_argument("Duplicate scoped stack-effect export");
	auto value = data->copy(original);
	if(std::find(storage.exported.begin(), storage.exported.end(), value) != storage.exported.end())
		throw std::invalid_argument("Duplicate scoped stack-effect export");
	storage.exported.push_back(value);
	if(value->propagator)
		data->propagate(root, value, root, true);
	else if(std::find(storage.local.begin(), storage.local.end(), value) == storage.local.end())
		storage.local.push_back(value);
}

void ScopedStackEffectQueries::removeExported(uint32_t id, const std::shared_ptr<Bonus> & value)
{
	const auto & root = data->target(id).source;
	const auto & exported = data->storage(root).exported;
	if(std::find(exported.begin(), exported.end(), value) == exported.end())
		throw std::invalid_argument("Scoped local removal requires an exact exported occurrence");
	data->remove(root, value);
}

void ScopedStackEffectQueries::removeMatchingExports(uint32_t id, const CSelector & selector)
{
	data->removeMatching(data->target(id).source, selector);
}

void ScopedStackEffectQueries::refreshMatchingExports(uint32_t id, const Bonus & value)
{
	for(const auto & existing : exportedRecords(id))
		if(stackEffectRefresh::matchesIdentity(value, existing.get()))
			stackEffectRefresh::refreshFields(*existing, value);
}

ScopedStackEffectQueries::Data::Row & ScopedStackEffectQueries::Data::target(uint32_t id) const
{
	const auto found = targets.find(id);
	if(found == targets.end())
		throw std::invalid_argument("Scoped stack-effect target is outside the packet");
	return *found->second;
}

ScopedStackEffectQueries::Data::View::View(Data & data, Row & row) : data(data), row(row) {}
const Unit * ScopedStackEffectQueries::Data::View::evaluatedUnit() const { return &row.state; }
TConstBonusListPtr ScopedStackEffectQueries::Data::View::getAllBonuses(const CSelector & selector, const std::string &) const { return data.query(row, selector, false); }
TConstBonusListPtr ScopedStackEffectQueries::Data::View::getUnstackedBonuses(const CSelector & selector) const { return data.query(row, selector, true); }
void ScopedStackEffectQueries::Data::View::onBattleFormChanged(const CUnitState & unit) const
{
	if(&unit != &row.state)
		throw std::invalid_argument("Foreign scoped stack-effect form view");
	data.changeForm(row);
}
std::vector<Bonus> ScopedStackEffectQueries::Data::View::sourceLocalSpellEffects() const
{
	std::vector<Bonus> result;
	for(const auto & value : data.storage(row.source).exported)
		if(value->source == BonusSource::SPELL_EFFECT)
			result.push_back(*value);
	return result;
}
}
