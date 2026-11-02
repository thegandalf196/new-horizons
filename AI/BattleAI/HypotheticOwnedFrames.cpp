/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "OwnedStackQuerySource.h"
#include "../../lib/battle/OwnedBattleFrameBatch.h"
#include "../../lib/battle/OwnedFrameBranchQueries.h"
#include <stdexcept>
void HypotheticBattle::requireOwnedFrameSource() const
{
	if(!ownedFrameBatch) return;
	if(subject.get() != ownedSourcePointer || ownedSourceOwner.expired()
		|| ownedSourceOwner.owner_before(subject) || subject.owner_before(ownedSourceOwner))
		throw std::invalid_argument("Owned branch cannot reuse a frame batch after source rebinding");
	if(const auto * parent = dynamic_cast<const HypotheticBattle *>(subject.get())) parent->requireOwnedFrameSource();
}
bool HypotheticBattle::usesOwnedFrameQueries() const
{
	requireOwnedFrameSource(); return bool(ownedFrameBatch);
}
bool HypotheticBattle::retainsOwnedFrameBatch(const std::shared_ptr<const battle::OwnedBattleFrameBatch> & batch) const
{
	requireOwnedFrameSource(); return ownedFrameBatch && ownedFrameBatch == batch;
}
const std::shared_ptr<const battle::OwnedBattleFrameBatch> & HypotheticBattle::sourceBatch() const
{
	requireOwnedFrameSource(); return ownedFrameBatch;
}
std::shared_ptr<battle::OwnedUnitBonusFrame> HypotheticBattle::ownedFrameFor(uint32_t id) const
{
	requireOwnedFrameSource();
	const auto found = stackStates.find(id);
	if(found != stackStates.end() && found->second->ownedQuerySource)
		return found->second->ownedQuerySource->retainedFrame();
	const auto base = ownedFrameBases.find(id);
	return base == ownedFrameBases.end() ? nullptr : base->second;
}
void HypotheticBattle::synchronizeOwnedFrame(const StackWithBonuses & current, battle::OwnedUnitBonusFrame & frame) const
{
	requireOwnedFrameSource();
	if(!current.bonusesToAdd.empty() || !current.bonusesToUpdate.empty() || !current.bonusesToRemove.empty())
		throw std::logic_error("Owned queries cannot reinterpret direct legacy overlay edits as raw source writes");
	if(!current.ownedQuerySource || current.owner != this
		|| current.ownedQuerySource->retainedFrame().get() != &frame || current.originalUnit != &frame.unit())
		throw std::invalid_argument("Owned query requires its exact branch/recipient/provider binding");
	const auto registered = stackStates.find(current.unitId());
	const bool isolated = registered == stackStates.end() || registered->second.get() != &current;
	std::vector<battle::OwnedFrameBranchQueries::CurrentUnit> rows;
	if(!ownedFrameBatch) rows.push_back({current.ownedQuerySource->retainedFrame(), &current}); // Explicit pure fixture only.
	else for(const auto & occurrence : ownedFrameBatch->occurrences())
	{
		const auto id = occurrence.identity.id;
		if(id == current.unitId()) rows.push_back({current.ownedQuerySource->retainedFrame(), &current});
		else
		{
			const auto found = stackStates.find(id);
			const auto basis = ownedFrameFor(id);
			rows.push_back({isolated ? basis->fork() : basis, found == stackStates.end() ? nullptr : found->second.get()});
		}
	}
	battle::OwnedFrameBranchQueries::synchronize(frame, rows);
}
void HypotheticBattle::inheritOwnedFrames(const HypotheticBattle & parent)
{
	parent.requireOwnedFrameSource();
	if(!parent.ownedFrameBatch) return;
	// Stage the parent's COMPLETE current roster before freezing this child.
	for(const auto & [id, unit] : parent.stackStates)
		if(unit->ownedQuerySource) { unit->ownedQuerySource->current(); break; }
	ownedFrameBatch = parent.ownedFrameBatch;
	pendingTimeStopHeroActionSides = parent.pendingTimeStopHeroActionSides;
	for(const auto & occurrence : ownedFrameBatch->occurrences())
	{
		const auto frame = parent.ownedFrameFor(occurrence.identity.id);
		if(!frame || !ownedFrameBases.emplace(occurrence.identity.id, frame->fork()).second)
			throw std::invalid_argument("Owned branch requires exact unique captured occurrence order");
	}
	ownedSourcePointer = subject.get(); ownedSourceOwner = subject;
}
battle::Units HypotheticBattle::ownedFrameUnitsIf(const battle::UnitFilter & predicate) const
{
	requireOwnedFrameSource();
	battle::Units result;
	for(const auto & occurrence : ownedFrameBatch->occurrences())
	{
		const auto unit = const_cast<HypotheticBattle *>(this)->getForUpdateImpl(occurrence.identity.id);
		if(predicate(unit.get())) result.push_back(unit.get());
	}
	if(stackStates.size() > ownedFrameBases.size())
		throw std::invalid_argument("New owned occurrences require a complete admitted source-domain transaction");
	return result;
}

bool HypotheticBattle::ownedFrameSource(const Subject & source)
{
	const auto * parent = dynamic_cast<const HypotheticBattle *>(source.get());
	return parent && parent->usesOwnedFrameQueries();
}
void HypotheticBattle::installOwnedRootFrames(const Subject & source,
	std::shared_ptr<const battle::OwnedBattleFrameBatch> batch)
{
	// This constructor completes under the real caller-held source lease. It
	// grants neither current source admission nor permission to dispatch actions.
	if(!batch || ownedFrameBatch || initialExportSubject()
		|| source->getBattle()->getBattleID() != batch->battle()
		|| source->getPlayerID() != std::optional<PlayerColor>(batch->observer())
		|| source->getBattle()->getActiveStackID() != static_cast<int32_t>(batch->activeUnitId())
		|| source->getBattle()->getActivationSerial() != batch->activationSerial())
		throw std::invalid_argument("Owned recipient data does not match its actual prepared source view");
	auto closed = batch->querySubject();
	if(!closed) throw std::invalid_argument("Owned recipient has no factory-sealed query source");
	std::map<uint32_t, std::shared_ptr<battle::OwnedUnitBonusFrame>> bases;
	for(const auto & occurrence : batch->occurrences())
		if(!occurrence.frame || !bases.emplace(occurrence.identity.id, occurrence.frame->fork()).second)
			throw std::invalid_argument("Owned recipient requires exact unique captured occurrences");
	// Called before ANY source unit status pass or projected unit construction.
	// Neither a native adjacency sidecar nor native obstacle/recipient wrapper
	// is created for this route. Bulk source metadata remains lease-bound.
	if(!stackStates.empty() || !projectedObstacles.empty())
		throw std::logic_error("Owned recipient preparation cannot follow native recipient materialization");
	subject = std::move(closed);
	sourceBinding = HypotheticSourceBinding(subject, *subject, true);
	ownedFrameBases = std::move(bases); ownedFrameBatch = std::move(batch);
	ownedSourcePointer = subject.get(); ownedSourceOwner = subject;
	for(const auto & occurrence : ownedFrameBatch->occurrences())
	{
		const auto bindings = occurrence.frame->unit().getBonuses(Selector::type()(BonusType::BIND_EFFECT));
		for(const auto & record : *bindings) if(record) noteSerpentBindingAdmission(*record);
	}
}
VisibleHeroPolicy HypotheticBattle::battleGetVisibleHeroPolicy(BattleSide side) const
{
	requireOwnedFrameSource();
	return ownedFrameBatch ? ownedFrameBatch->heroPolicy(side) : BattleProxy::battleGetVisibleHeroPolicy(side);
}
PlayerColor HypotheticBattle::getSidePlayer(BattleSide side) const
{
	requireOwnedFrameSource();
	return BattleProxy::getSidePlayer(side);
}
