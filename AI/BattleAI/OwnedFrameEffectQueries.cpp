/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedFrameEffectQueries.h"
#include "OwnedFrameAccessibility.h"
#include "OwnedStackQuerySource.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/OwnedBattleFrameBatch.h"
#include "../../lib/battle/OwnedUnitBonusFrameData.h"
#include "../../lib/battle/ScopedStackEffectReplay.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include <stdexcept>
OwnedFrameEffectQueries::OwnedFrameEffectQueries(const HypotheticBattle & owner,
	StackWithBonuses & target, const std::vector<Bonus> & records) : owner(owner)
{
	initialize(&target, {&records});
}
OwnedFrameEffectQueries::OwnedFrameEffectQueries(const HypotheticBattle & owner, const SetStackEffect & packet) : owner(owner)
{
	std::vector<const std::vector<Bonus> *> records;
	for(const auto * changes : {&packet.toRemove, &packet.toUpdate, &packet.toAdd})
		for(const auto & entry : *changes) records.push_back(&entry.second);
	initialize(nullptr, records);
}
void OwnedFrameEffectQueries::initialize(StackWithBonuses * target, const std::vector<const std::vector<Bonus> *> & inputs)
{
	this->target = target;
	if(target)
	{
		const auto registered = owner.stackStates.find(target->unitId());
		isolated = registered == owner.stackStates.end() || registered->second.get() != target;
	}
	owner.requireOwnedFrameSource();
	if(!owner.ownedFrameBatch) throw std::invalid_argument("Current effect replay requires its admitted complete frame domain");
	if(target) target->ownedQuerySource->current();
	else for(const auto & [id, unit] : owner.stackStates)
		if(unit->ownedQuerySource) { unit->ownedQuerySource->current(); break; }
	for(const auto & occurrence : owner.ownedFrameBatch->occurrences())
	{
		const auto basis = target && occurrence.identity.id == target->unitId()
			? target->ownedQuerySource->retainedFrame() : owner.ownedFrameFor(occurrence.identity.id);
		if(!basis) throw std::invalid_argument("Current effect replay lost a captured occurrence");
		actual.push_back(basis); auto next = basis->fork(); order.push_back(next);
		if(!frames.emplace(occurrence.identity.id, std::move(next)).second)
			throw std::invalid_argument("Current effect replay requires unique captured occurrences");
	}
	if(order.empty()) throw std::invalid_argument("Current effect replay requires its captured roster");
	std::vector<std::shared_ptr<const Bonus>> roots;
	for(const auto * records : inputs) for(const auto & value : *records)
	{
		if(value.propagator || value.propagationUpdater)
			throw std::logic_error("Current frame domain lacks source-propagation transition metadata");
		// These canonical special-case paths still need captured identity and
		// entitlement adapters. Refuse the incomplete domain BEFORE any replay,
		// never substitute hidden policy, use LIBRARY or prune an AI candidate.
		if(newHorizonsSerpentsEmbrace::isMarker(value) || newHorizonsParalyzingGaze::isMarker(value))
			throw std::logic_error("Current frame domain lacks captured specialty-record admission metadata");
		inputRecords.emplace(&value, roots.size()); roots.push_back(std::make_shared<Bonus>(value));
	}
	inputOwner = std::shared_ptr<const ownedBonusPayloads::Captured>(ownedBonusPayloads::copyOwnedValues(roots));
}
std::shared_ptr<battle::OwnedUnitBonusFrame> OwnedFrameEffectQueries::frame(uint32_t id) const
{
	const auto found = frames.find(id);
	if(found == frames.end()) throw std::invalid_argument("Missing current effect occurrence");
	return found->second;
}
void OwnedFrameEffectQueries::synchronize()
{
	owner.requireOwnedFrameSource();
	if(!dirty) return;
	std::vector<battle::OwnedFrameBranchQueries::CurrentUnit> rows;
	for(const auto & value : order) rows.push_back({value, nullptr});
	battle::OwnedFrameBranchQueries::synchronize(*order.front(), rows); dirty = false;
}
const battle::Unit * OwnedFrameEffectQueries::find(uint32_t id)
{
	synchronize(); const auto found = frames.find(id);
	return found == frames.end() ? nullptr : &found->second->unit();
}
const battle::Unit & OwnedFrameEffectQueries::current(uint32_t id) { synchronize(); return frame(id)->unit(); }
battle::CUnitState & OwnedFrameEffectQueries::state(uint32_t id)
{
	synchronize(); dirty = true; return battle::OwnedFrameBranchQueries::effectQueryState(*frame(id));
}
BonusList OwnedFrameEffectQueries::exportedRecords(uint32_t id)
{
	synchronize(); return battle::OwnedFrameBranchQueries::sourceExports(*frame(id));
}
void OwnedFrameEffectQueries::appendExported(uint32_t id, const std::shared_ptr<Bonus> & value)
{
	auto & data = *frame(id)->data;
	data.ledger.added.push_back(value); data.pendingPayloads.push_back(inputOwner);
	if(value->type == BonusType::PHYSICAL_AFFLICTION) data.ledger.capturedGroups.emplace(value->source, value->sid);
	dirty = true;
}
void OwnedFrameEffectQueries::removeExported(uint32_t id, const std::shared_ptr<Bonus> & value)
{
	const auto exports = exportedRecords(id);
	if(std::find(exports.begin(), exports.end(), value) == exports.end())
		throw std::invalid_argument("Effect removal requires an exact source-export alias");
	removeMatchingExports(id, CSelector([&](const Bonus * candidate) { return candidate == value.get(); }));
}
void OwnedFrameEffectQueries::removeMatchingExports(uint32_t id, const CSelector & selected)
{
	battle::OwnedFrameBranchQueries::removeMatching(*frame(id), selected); dirty = true;
}
void OwnedFrameEffectQueries::refreshMatchingExports(uint32_t id, const Bonus & value)
{
	battle::OwnedFrameBranchQueries::refresh(*frame(id), value); dirty = true;
}
AccessibilityInfo OwnedFrameEffectQueries::accessibility(uint32_t id)
{
	synchronize(); battle::Units units;
	for(const auto & value : order) units.push_back(&value->unit());
	return battle::withAccessibleHexes(ownedFrameAccessibility(owner, owner.ownedFrameBatch->accessibilityScene(), std::move(units)), frame(id)->unit().getHexes());
}
void OwnedFrameEffectQueries::replay(uint32_t id, const std::vector<Bonus> & records, newHorizonsGraveDust::EffectOperation operation)
{
	std::vector<Bonus> owned;
	for(const auto & value : records) owned.push_back(*inputOwner->roots().at(inputRecords.at(&value)));
	battle::replayStackEffectVector(*this, owner, id, owned, operation);
}
