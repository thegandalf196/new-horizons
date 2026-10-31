/*
 * InitialExportWorld.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "InitialExportWorld.h"
#include "InitialExportEffectQueries.h"
#include "../../lib/battle/BattleAccessibilityQuery.h"
#include "MovementBonusProvenance.h"
#include "../../lib/CStack.h"
#include <optional>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <variant>

struct InitialExportWorld::ExportHandle::Identity
{
	Source source;
	std::shared_ptr<Bonus> record;
};

struct InitialExportWorld::Data
{
	using Identity = ExportHandle::Identity;
	using Record = std::shared_ptr<const Identity>;
	struct Add { Record record; };
	struct Remove { Record record; };
	struct Expire { Source source; Expiry expiry; };
	struct Update { uint32_t id; JsonNode state; int64_t healthDelta; };
	struct Snapshot { Source source; JsonNode state; };
	struct RemoveRecords
	{
		Source source;
		std::vector<ProjectedExportRemoval> receipts;
		std::optional<std::vector<recipientBonusOverlay::Layer>> recipientLayers;
	};
	struct UpdateFrom { Source source; JsonNode before; JsonNode after; int64_t healthDelta; };
	struct DurationChanges { std::vector<ProjectedDurationChange> receipts; };
	struct EnvironmentSnapshot { battle::ProjectedUnitEnvironmentState values; };
	struct RecipientLayers { Source source; std::vector<recipientBonusOverlay::Layer> layers; };
	using Operation = std::variant<Add, Remove, Expire, Update, Snapshot, RemoveRecords, UpdateFrom,
		DurationChanges, EnvironmentSnapshot, RecipientLayers>;
	BattleInfo & original;
	std::vector<Operation> program;
	std::set<const Identity *> added;
	std::set<size_t> unrecordedUnits;
	bool unrecordedEnvironment = false;
	bool unrecordedProgramInputs = false;
	std::unique_ptr<battle::ProjectedBattleWorldArena> arena;
	bool failed = false;
	bool effectQueriesOnly = false;

	explicit Data(BattleInfo & original) : original(original), arena(std::make_unique<battle::ProjectedBattleWorldArena>(original)) {}
	void requireUsable() const
	{
		if(failed)
			throw std::logic_error("Initial export world failed; discard its private owner");
		(void)arena->size(); // Also retain the arena's failure/finalization gate.
	}
	void requireSource(Source source) const
	{
		const auto & actual = arena->originalAt(source.occurrence);
		if(&actual != source.original || actual.unitId() != source.unitId)
			throw std::invalid_argument("Export requires its exact original occurrence, not an effective recipient");
	}
	void recordProgramInputs()
	{
		unrecordedProgramInputs = unrecordedProgramInputs || !unrecordedUnits.empty() || unrecordedEnvironment;
	}
	std::optional<battle::ProjectedBattleWorldArena::UpdateDecision> apply(const Operation & operation)
	{
		return std::visit([this](const auto & step) -> std::optional<battle::ProjectedBattleWorldArena::UpdateDecision>
		{
			using Step = std::decay_t<decltype(step)>;
			if constexpr(std::is_same_v<Step, Update>)
				return arena->updateUnit(step.id, step.state, step.healthDelta);
			else if constexpr(std::is_same_v<Step, Snapshot>)
			{
				requireSource(step.source);
				arena->restoreQuerySnapshotAt(step.source.occurrence, step.state);
			}
			else if constexpr(std::is_same_v<Step, RemoveRecords>)
			{
				requireSource(step.source);
				arena->replayExportedRemovals(step.receipts);
				if(step.recipientLayers)
					arena->replayRecipientOverlaysAt(step.source.occurrence, *step.recipientLayers);
			}
			else if constexpr(std::is_same_v<Step, DurationChanges>)
				arena->replayDurationChanges(step.receipts);
			else if constexpr(std::is_same_v<Step, EnvironmentSnapshot>)
				arena->copyEnvironmentValues(step.values);
			else if constexpr(std::is_same_v<Step, RecipientLayers>)
			{
				requireSource(step.source);
				arena->installRecipientOverlaysAt(step.source.occurrence, step.layers);
			}
			else if constexpr(std::is_same_v<Step, UpdateFrom>)
			{
				requireSource(step.source);
				arena->restoreQuerySnapshotAt(step.source.occurrence, step.before);
				return arena->updateUnit(step.source.unitId, step.after, step.healthDelta);
			}
			else if constexpr(std::is_same_v<Step, Expire>)
			{
				requireSource(step.source);
				switch(step.expiry)
				{
				case Expiry::UNTIL_ATTACK: arena->expireExportedBonusesAt(step.source.occurrence, Bonus::UntilAttack); break;
				case Expiry::UNTIL_OWN_ATTACK: arena->expireExportedBonusesAt(step.source.occurrence, Bonus::UntilOwnAttack); break;
				case Expiry::UNTIL_BEING_ATTACKED: arena->expireExportedBonusesAt(step.source.occurrence, Bonus::UntilBeingAttacked); break;
				default: throw std::invalid_argument("Unknown initial export expiry stage");
				}
			}
			else
			{
				if(!step.record)
					throw std::invalid_argument("An effective bonus list is not an originating export handle");
				requireSource(step.record->source);
				if constexpr(std::is_same_v<Step, Add>)
				{
					arena->addExportedBonusAt(step.record->source.occurrence, step.record->record);
					added.insert(step.record.get());
				}
				else
				{
					if(!added.contains(step.record.get()))
						throw std::invalid_argument("Export handle has no originating ADD in this branch");
					arena->removeExportedBonusAt(step.record->source.occurrence, step.record->record);
				}
			}
			return std::nullopt;
		}, operation);
	}
	std::optional<battle::ProjectedBattleWorldArena::UpdateDecision> append(Operation operation)
	{
		requireUsable();
		try
		{
			if(effectQueriesOnly) throw std::logic_error("Disposable effect queries cannot append a replay program");
			if(program.size() >= 512)
				throw std::length_error("Initial export forecast program exceeds its bounded scope");
			if(!std::holds_alternative<Snapshot>(operation) && !std::holds_alternative<EnvironmentSnapshot>(operation))
				recordProgramInputs();
			program.push_back(std::move(operation));
			return apply(program.back());
		}
		catch(...) { failed = true; throw; }
	}
};

InitialExportWorld::ExportHandle::ExportHandle(std::shared_ptr<const Identity> identity) : identity(std::move(identity)) {}
InitialExportWorld::InitialExportWorld(BattleInfo & original) : impl(std::make_unique<Data>(original)) {}
InitialExportWorld::~InitialExportWorld() = default;

size_t InitialExportWorld::size() const
{
	impl->requireUsable();
	return impl->arena->size();
}

InitialExportWorld::Source InitialExportWorld::sourceAt(size_t occurrence) const
{
	impl->requireUsable();
	const auto & original = impl->arena->originalAt(occurrence);
	return {occurrence, original.unitId(), &original};
}

InitialExportWorld::ExportHandle InitialExportWorld::add(Source source, const Bonus & record)
{
	auto identity = std::make_shared<const ExportHandle::Identity>(ExportHandle::Identity{source, std::make_shared<Bonus>(record)});
	impl->append(Data::Add{identity});
	return ExportHandle(std::move(identity));
}

void InitialExportWorld::remove(const ExportHandle & handle) { impl->append(Data::Remove{handle.identity}); }
void InitialExportWorld::expire(Source source, Expiry expiry) { impl->append(Data::Expire{source, expiry}); }

void InitialExportWorld::copyQueryValues(Source source, const battle::CUnitState & state)
{
	impl->requireUsable();
	impl->requireSource(source);
	impl->unrecordedUnits.insert(source.occurrence);
	impl->arena->copyQueryValuesAt(source.occurrence, state);
}

void InitialExportWorld::recordQuerySnapshot(Source source)
{
	impl->requireUsable();
	impl->requireSource(source);
	impl->append(Data::Snapshot{source, impl->arena->snapshotAt(source.occurrence)});
	impl->unrecordedUnits.erase(source.occurrence);
}

battle::ProjectedBattleWorldArena::UpdateDecision InitialExportWorld::updateUnitFrom(Source source,
	const JsonNode & before, const JsonNode & after, int64_t healthDelta)
{
	return impl->append(Data::UpdateFrom{source, before, after, healthDelta}).value();
}

void InitialExportWorld::removeMatching(Source source, const CSelector & selector)
{
	impl->requireUsable();
	try
	{
		impl->requireSource(source);
		if(impl->program.size() >= 512)
			throw std::length_error("Initial export forecast program exceeds its bounded scope");
		impl->recordProgramInputs();
		auto receipts = impl->arena->removeExportedBonusesRecordingAt(source.occurrence, selector);
		auto layers = impl->arena->removeRecipientOverlaysMatchingAt(source.occurrence, selector);
		impl->program.push_back(Data::RemoveRecords{source, std::move(receipts), std::move(layers)});
	}
	catch(...) { impl->failed = true; throw; }
}

void InitialExportWorld::reduceDurations(Source source, const CSelector & selector)
{
	impl->requireUsable();
	try
	{
		impl->requireSource(source);
		if(impl->program.size() >= 512)
			throw std::length_error("Initial export forecast program exceeds its bounded scope");
		impl->recordProgramInputs();
		auto receipts = impl->arena->reduceExportedDurationsAt(source.occurrence, selector);
		impl->program.push_back(Data::DurationChanges{std::move(receipts)});
	}
	catch(...) { impl->failed = true; throw; }
}

void InitialExportWorld::copyEnvironmentValues(const battle::ProjectedUnitEnvironmentState & values)
{
	impl->requireUsable();
	impl->arena->copyEnvironmentValues(values);
	impl->unrecordedEnvironment = true;
}

void InitialExportWorld::recordEnvironmentSnapshot(const battle::ProjectedUnitEnvironmentState & values)
{
	impl->append(Data::EnvironmentSnapshot{values});
	impl->unrecordedEnvironment = false;
}

bool InitialExportWorld::hasMovementInvariantSources() const
{
	impl->requireUsable();
	for(size_t index = 0; index < size(); ++index)
		if(!movementBonusProvenance::invariantSources(*sourceAt(index).original))
			return false;
	for(const auto & operation : impl->program)
	{
		if(const auto * add = std::get_if<Data::Add>(&operation))
			if(!movementBonusProvenance::invariantRecord(*add->record->record))
				return false;
		if(const auto * layers = std::get_if<Data::RecipientLayers>(&operation))
			if(std::ranges::any_of(layers->layers, [](const auto & layer) { return layer.hasEdits(); }))
				return false; // Query overlays are not a raw-source invariance proof.
	}
	return true;
}

void InitialExportWorld::installRecipientOverlays(Source source,
	std::vector<recipientBonusOverlay::Layer> layers)
{
	impl->append(Data::RecipientLayers{source, std::move(layers)});
}

bool InitialExportWorld::hasUnresolvedRecipientOverlays() const
{
	impl->requireUsable();
	return impl->arena->hasUnresolvedRecipientOverlays();
}

bool InitialExportWorld::hasRecordedRecipientInjuryInputs() const
{
	impl->requireUsable();
	return impl->unrecordedUnits.empty() && !impl->unrecordedEnvironment && !impl->unrecordedProgramInputs;
}

battle::ProjectedBattleWorldArena::UpdateDecision InitialExportWorld::updateUnit(uint32_t id,
	const JsonNode & state, int64_t healthDelta)
{
	return impl->append(Data::Update{id, state, healthDelta}).value();
}

std::unique_ptr<InitialExportWorld> InitialExportWorld::fork() const
{
	impl->requireUsable();
	if(impl->effectQueriesOnly) throw std::logic_error("Disposable effect queries cannot fork unrecorded changes");
	auto result = std::make_unique<InitialExportWorld>(impl->original);
	for(const auto & operation : impl->program)
		result->impl->append(operation);
	// Replaying missing inputs does not repair an earlier operation which
	// observed them. Descendants must retain that ownership failure witness.
	result->impl->unrecordedUnits = impl->unrecordedUnits;
	result->impl->unrecordedEnvironment = impl->unrecordedEnvironment;
	result->impl->unrecordedProgramInputs = impl->unrecordedProgramInputs;
	return result;
}

const battle::CUnitStateDetached & InitialExportWorld::unitAt(size_t occurrence) const
{
	impl->requireUsable();
	return impl->arena->unitAt(occurrence);
}

JsonNode InitialExportWorld::snapshotAt(size_t occurrence)
{
	impl->requireUsable();
	return impl->arena->snapshotAt(occurrence);
}

JsonNode InitialExportWorld::snapshotAfterPhysicalDamage(Source source, int64_t & damage) const
{
	impl->requireUsable();
	impl->requireSource(source);
	if(!impl->unrecordedUnits.empty() || impl->unrecordedEnvironment || impl->unrecordedProgramInputs)
		throw std::logic_error("Physical damage scratch requires recorded inputs before ordered operations");
	auto scratch = fork();
	return scratch->impl->arena->damagePhysicalAndSnapshotAt(source.occurrence, damage);
}

TConstBonusListPtr InitialExportWorld::projectedHeroBonuses(BattleSide side, bool unstacked) const
{
	impl->requireUsable();
	return impl->arena->projectedHeroBonuses(side, unstacked);
}

std::unique_ptr<InitialExportWorld::EffectQueries> InitialExportWorld::effectQueries() const
{
	auto copy = fork();
	copy->impl->effectQueriesOnly = true;
	return std::unique_ptr<EffectQueries>(new EffectQueries(std::move(copy)));
}
InitialExportWorld::EffectQueries::EffectQueries(std::unique_ptr<InitialExportWorld> value) : world(std::move(value))
{
	std::set<uint32_t> identities;
	for(size_t index = 0; index < world->size(); ++index)
	{
		const auto source = world->sourceAt(index);
		if(!identities.insert(source.unitId).second) throw std::invalid_argument("Effect queries require unique ledger identities");
		sources.push_back(source);
	}
}
InitialExportWorld::EffectQueries::~EffectQueries() = default;
InitialExportWorld::Source InitialExportWorld::EffectQueries::target(uint32_t id) const
{
	world->impl->requireUsable();
	for(const auto & source : sources)
		if(source.unitId == id)
		{
			world->impl->requireSource(source);
			return source;
		}
	throw std::invalid_argument("Effect query target is outside its actual ledger");
}
const battle::Unit * InitialExportWorld::EffectQueries::find(uint32_t id) const
{
	for(const auto & source : sources)
		if(source.unitId == id) return &current(id);
	return nullptr;
}
const battle::Unit & InitialExportWorld::EffectQueries::current(uint32_t id) const
{
	return world->unitAt(target(id).occurrence);
}
battle::CUnitState & InitialExportWorld::EffectQueries::state(uint32_t id)
{
	return world->impl->arena->effectQueryStateAt(target(id).occurrence);
}
const BonusList & InitialExportWorld::EffectQueries::exportedRecords(uint32_t id) const
{
	return world->impl->arena->exportedBonusesAt(target(id).occurrence);
}
void InitialExportWorld::EffectQueries::appendExported(uint32_t id, const std::shared_ptr<Bonus> & value)
{
	world->impl->arena->addExportedBonusAt(target(id).occurrence, value);
}
void InitialExportWorld::EffectQueries::removeExported(uint32_t id, const std::shared_ptr<Bonus> & value)
{
	world->impl->arena->removeExportedBonusAt(target(id).occurrence, value);
}
void InitialExportWorld::EffectQueries::removeMatchingExports(uint32_t id, const CSelector & selector)
{
	world->impl->arena->expireExportedBonusesAt(target(id).occurrence, selector);
}
void InitialExportWorld::EffectQueries::refreshMatchingExports(uint32_t id, const Bonus & value)
{
	world->impl->arena->refreshExportedBonusesAt(target(id).occurrence, value);
}
AccessibilityInfo InitialExportWorld::EffectQueries::accessibility(uint32_t id) const
{
	return battle::withAccessibleHexes(world->impl->arena->accessibility(), current(id).getHexes());
}
