/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "StackWithBonuses.h"
#include "OwnedStackQuerySource.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/UnitCreatureResolver.h"
#include "../../lib/CCreatureHandler.h"

StackWithBonuses::StackWithBonuses(const HypotheticBattle * Owner,
	std::shared_ptr<battle::OwnedUnitBonusFrame> frame)
	: battle::CUnitState(), origBearer(nullptr), owner(Owner),
	type(OwnedStackQuerySource::initialUnit(frame).unitCreatureResolver()->creature(
		OwnedStackQuerySource::initialUnit(frame).battleFormOriginalCreature())),
	sourceCreatureType(type->getId()),
	baseAmount(OwnedStackQuerySource::initialUnit(frame).unitBaseAmount()),
	battleStartMaximumAggregateHP(OwnedStackQuerySource::initialUnit(frame).getBattleStartMaximumAggregateHP()),
	rebirthOriginalAggregateHP(OwnedStackQuerySource::initialUnit(frame).getRebirthOriginalAggregateHP()),
	id(OwnedStackQuerySource::initialUnit(frame).unitId()),
	side(OwnedStackQuerySource::initialUnit(frame).unitSide()),
	player(OwnedStackQuerySource::initialUnit(frame).unitOwner()),
	slot(OwnedStackQuerySource::initialUnit(frame).unitSlot()), treeVersionLocal(0)
{
	ownedQuerySource = std::make_unique<OwnedStackQuerySource>(*Owner, *this, std::move(frame));
	originalUnit = &ownedQuerySource->retainedFrame()->unit();
	origBearer = originalUnit->getBonusBearer();
	localInit(ownedQuerySource.get());
	battle::CUnitState::operator=(ownedQuerySource->retainedFrame()->unit());
	ownedQuerySource->finishInitialization();
}

const battle::UnitCreatureResolver * StackWithBonuses::unitCreatureResolver() const
{
	return ownedQuerySource ? ownedQuerySource->retainedFrame()->unit().unitCreatureResolver() : nullptr;
}
std::shared_ptr<battle::Unit> StackWithBonuses::acquire() const
{
	return ownedQuerySource ? ownedQuerySource->acquireState() : battle::CUnitState::acquire();
}
std::shared_ptr<battle::CUnitState> StackWithBonuses::acquireState() const
{
	return ownedQuerySource ? ownedQuerySource->acquireState() : battle::CUnitState::acquireState();
}
