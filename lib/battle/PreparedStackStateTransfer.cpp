/*
 * PreparedStackStateTransfer.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "PreparedStackStateTransfer.h"
#include "CUnitState.h"
#include "UnitStateValueExchange.h"
#include "../CStack.h"
#include "../CCreatureHandler.h"
#include "../GameLibrary.h"
#include "../bonuses/ProjectedBonusGraph.h"
#include <limits>
#include <stdexcept>

namespace
{
std::shared_ptr<const CCreature> retainCreature(const CCreature * creature)
{
	if(!creature)
		return {};
	const auto & retained = LIBRARY->creh->objects.at(creature->getId().getNum());
	if(retained.get() != creature)
		throw std::invalid_argument("Prepared stack source is not the registered creature identity");
	return retained;
}

bool sourceStillRegistered(const std::shared_ptr<const CCreature> & source, int32_t index) noexcept
{
	if(!source)
		return index == -1;
	const auto & objects = LIBRARY->creh->objects;
	return index >= 0 && size_t(index) < objects.size() && objects[index].get() == source.get();
}
}

int32_t battle::PreparedStackStateTransfer::checkedFinalRevision(int32_t originalRevision,
	int32_t boundRevision, int32_t projectedRevision)
{
	if(projectedRevision < boundRevision)
		throw std::invalid_argument("Prepared stack form revision moved backwards after binding");
	const auto final = int64_t(originalRevision) + int64_t(projectedRevision) - boundRevision;
	if(final < std::numeric_limits<int32_t>::min() || final > std::numeric_limits<int32_t>::max())
		throw std::overflow_error("Prepared stack form revision is not representable");
	return static_cast<int32_t>(final);
}

battle::PreparedStackStateTransfer::PreparedStackStateTransfer(CStack & destination,
	CUnitStateDetached & prepared, ProjectedBonusGraph & graph)
	: destination(destination), prepared(prepared)
{
	graph.requireBoundUnit(destination, prepared);
	provider = &graph.view(destination);
	if(!prepared.usesUnitInfo(destination))
		throw std::invalid_argument("Prepared stack values require their exact original descriptor");
	const auto & binding = graph.bindings.at(&destination);
	originalRevision = binding.originalRevision;
	projectedRevision = prepared.getBattleFormViewRevision();
	finalRevision = checkedFinalRevision(originalRevision, binding.projectedRevision, projectedRevision);
	nativeSource = retainCreature(binding.originalCreature.toCreature());
	if(!nativeSource)
		throw std::invalid_argument("Prepared stack requires a registered native creature");
	oldSource = retainCreature(binding.originalFormSource);
	newSource = retainCreature(binding.formSource);
	nativeIndex = nativeSource->getId().getNum();
	oldIndex = oldSource ? oldSource->getId().getNum() : -1;
	newIndex = newSource ? newSource->getId().getNum() : -1;
	const auto * desired = prepared.hasBattleForm() && prepared.battleFormCreature() != destination.typeID
		? prepared.battleFormCreature().toCreature() : nullptr;
	if(prepared.hasBattleForm() && prepared.battleFormCreature() != destination.typeID && !desired)
		throw std::invalid_argument("Prepared stack requires a registered form creature");
	if(desired != newSource.get())
		throw std::invalid_argument("Prepared stack form source has not been synchronized");
	finalCreature = prepared.battleFormCreatureId.getNum();
	finalOriginalCreature = prepared.battleFormOriginalCreatureId.getNum();
	finalActive = prepared.hasBattleForm();
	originalWidth = binding.originalWidth;
	finalWidth = (newSource ? newSource : nativeSource)->isDoubleWide();
	if(!readyToCommit())
		throw std::runtime_error("Prepared stack form view is stale");
}

bool battle::PreparedStackStateTransfer::readyToCommit() const noexcept
{
	return !consumed && destination.formBonusSource == oldSource.get()
		&& destination.doubleWideCached == originalWidth
		&& destination.battleFormViewRevision == originalRevision
		&& prepared.getBattleFormViewRevision() == projectedRevision
		&& prepared.battleFormCreatureId.getNum() == finalCreature
		&& prepared.battleFormOriginalCreatureId.getNum() == finalOriginalCreature
		&& prepared.hasBattleForm() == finalActive
		&& destination.typeID.getNum() == nativeIndex
		&& sourceStillRegistered(nativeSource, nativeIndex)
		&& sourceStillRegistered(oldSource, oldIndex)
		&& sourceStillRegistered(newSource, newIndex)
		&& prepared.usesBonusProvider(*provider)
		&& prepared.usesUnitInfo(destination);
}

bool battle::PreparedStackStateTransfer::commit() noexcept
{
	if(!readyToCommit())
		return false;
	exchangePrepared();
	return true;
}

void battle::PreparedStackStateTransfer::exchangePrepared() noexcept
{
	exchangePreparedUnitStateValues(destination, prepared);
	destination.formBonusSource = newSource.get();
	destination.doubleWideCached = finalWidth;
	destination.battleFormViewRevision = finalRevision;
	consumed = true;
}
