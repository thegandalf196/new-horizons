/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "UnitStateValueSnapshot.h"
#include "UnitStateValueFields.h"
#include "../CCreatureHandler.h"
#include <stdexcept>
namespace battle
{
namespace
{
UnitStateValueSnapshot::Identity identityOf(const CUnitState & unit)
{
	const auto * creature = unit.unitType();
	if(!creature || unit.unitBaseAmount() < 0)
		throw std::invalid_argument("Native value snapshot needs a complete receiver descriptor");
	return {unit.unitId(), unit.battleFormOriginalCreature(), unit.unitBaseAmount(),
		unit.unitSide(), unit.unitOwner(), unit.unitSlot()};
}
}
class UnitStateValueSnapshot::Data
{
public:
	using Storage = decltype(UnitStateValueFields::captureSnapshot(std::declval<const CUnitState &>()));
	Identity identity;
	Status status;
	Storage values;
	explicit Data(const CUnitState & source) : identity(identityOf(source)),
		status{source.getCount(), source.creatureId(), source.isGhost(), source.summoned, source.natureSummoned, source.hasBattleFormState()},
		values(UnitStateValueFields::captureSnapshot(source)) {}
};
UnitStateValueSnapshot::UnitStateValueSnapshot(std::shared_ptr<const Data> value) : data(std::move(value)) {}
UnitStateValueSnapshot UnitStateValueSnapshot::capture(const CUnitState & source)
{
	return UnitStateValueSnapshot(std::make_shared<const Data>(source));
}
UnitStateValueSnapshot::Identity UnitStateValueSnapshot::identity() const
{
	if(!data) throw std::logic_error("Moved-from native value snapshot");
	return data->identity;
}
UnitStateValueSnapshot::Status UnitStateValueSnapshot::status() const
{
	if(!data) throw std::logic_error("Moved-from native value snapshot");
	return data->status;
}
int32_t UnitStateValueSnapshot::retaliationCache() const
{
	if(!data) throw std::logic_error("Moved-from native value snapshot");
	return retaliationCacheOverride.value_or(UnitStateValueFields::snapshotRetaliationCache(data->values));
}
int32_t UnitStateValueSnapshot::retaliationCache(const CUnitState & source)
{
	return UnitStateValueFields::retaliationCache(source);
}
UnitStateValueSnapshot UnitStateValueSnapshot::withRetaliationCache(int32_t value) const
{
	if(!data || value < -1) throw std::invalid_argument("Invalid native retaliation cache overlay");
	auto result = *this;
	result.retaliationCacheOverride = value;
	return result;
}
void UnitStateValueSnapshot::restore(CUnitState & receiver) const
{
	if(!data || identityOf(receiver) != data->identity || receiver.creatureId() != data->status.effectiveCreature)
		throw std::invalid_argument("Native value snapshot requires its already prepared unit/form descriptor");
	UnitStateValueFields::restoreSnapshot(receiver, data->values, retaliationCacheOverride);
}
}
