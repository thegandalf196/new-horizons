/*
 * ProjectedBattleWorldArenaAdd.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "ProjectedBattleWorldArenaState.h"
#include "BattleInfo.h"
#include "StackInitializationReceipts.h"
#include "UnitStateValueFields.h"
#include "UnitAdditionRules.h"
#include "NewHorizonsOffense.h"
#include "../mapObjects/army/CArmedInstance.h"
#include "../mapObjects/army/CStackBasicDescriptor.h"
#include <stdexcept>

int32_t battle::ProjectedBattleWorldArena::Data::summonCapacity(const UnitInfo & info)
{
	validateRebirthAdditionSide(info);
	auto query = newHorizonsElementalRebirth::ProjectedSummonHealthQuery::create(*graph, queryArmies.at(info.side),
		info.type, sideValues.inputs(info.side).color, info.side);
	if(!query)
		return 0;
	const auto value = query->maximum();
	try
	{
		queries.push_back(std::move(query));
	}
	catch(...)
	{
		abandon(); // query is still retained here, BEFORE captured identity retirement
		throw;
	}
	return value;
}

const battle::CUnitStateDetached & battle::ProjectedBattleWorldArena::Data::add(uint32_t id, const JsonNode & data)
{
	const auto info = decodeUnitAddition(id, data, commandRules, slots.size());
	if(info.rebirthOriginalAggregateHP > 0)
		validateUnitAdditionCapacity(info, summonCapacity(info));
	const auto owner = sideValues.inputs(info.side).color;
	// Explicit invalid-domain guard: native doubleWide initialization would
	// dereference an absent creature. This is NOT a live-route policy change.
	if(!info.type.hasValue() || !info.type.toCreature())
		throw std::runtime_error("Projected unit addition requires a registered creature");
	CStackBasicDescriptor descriptor(info.type, info.count);
	auto slot = std::make_unique<Slot>();
	slot->owned = std::make_unique<CStack>(&descriptor, owner, info.id, info.side, SlotID::SUMMONED_SLOT_PLACEHOLDER);
	slot->original = slot->owned.get();
	slot->original->initialPosition = info.position;
	slot->original->summoned = info.summoned;
	slot->original->natureSummoned = info.natureSummoned;
	slots.push_back(std::move(slot)); // stable ownership BEFORE view/roster allocation
	auto & held = *slots.back();
	held.state = std::make_unique<CUnitStateDetached>(held.original, &graph->view(*held.original));
	UnitStateValueFields::copy(*held.state, *held.original);
	graph->appendColdOccurrence(*held.original, *held.state, roster, &source);
	initializeAdded(held, info);
	held.creationIdentity = std::make_unique<const BattleUnitRootIdentity>(
		PreparedBattleUnitRoots::captureIdentity(*held.original));
	return *held.state;
}

void battle::ProjectedBattleWorldArena::Data::initializeAdded(Slot & slot, const UnitInfo & info)
{
	const StackInitializationReceipts receipts(*slot.state);
	assert(info.type.hasValue());
	auto * army = armies.at(info.side);
	assert(army);
	// A newly constructed basic descriptor has NO exported training pointers.
	assert(slot.original->getExportedBonusList().empty());
	graph->attachToSource(*slot.original, *info.type.toCreature());
	graph->attachTo(*slot.original, *army);
	// Bind/reset occurs AFTER attachment, just as in CStack::localInit.
	receipts.initialize(*slot.state, environment.get(), slot.original->initialPosition);
	slot.state->summoned = info.summoned;
	slot.state->natureSummoned = info.natureSummoned;
	slot.original->initializeRebirthOriginalAggregateHP(info.rebirthOriginalAggregateHP);
	if(info.phantomIntegrity > 0)
		slot.state->initializePhantomProfile(info.phantomIntegrity, info.phantomDuration);
	const auto & sideOrders = orders.at(info.side);
	const auto found = std::find_if(sideOrders.begin(), sideOrders.end(), [](const HeroOrderState & order)
	{
		return order.command == HeroCommand::RIPOSTE;
	});
	const auto * riposte = found == sideOrders.end() ? nullptr : &*found;
	if(unitAdditionNeedsVengeance(*slot.state, riposte, round, heroes.at(info.side)))
		graph->addExported(*slot.original, std::make_shared<Bonus>(newHorizonsOffense::vengeanceRetaliationBonus()));
	graph->nodeHasChanged(*slot.original);
}

const battle::CUnitStateDetached & battle::ProjectedBattleWorldArena::addUnit(uint32_t id, const JsonNode & data)
{
	impl->requireUsable();
	try { return impl->add(id, data); }
	catch(...) { impl->abandon(); throw; }
}
