/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeReadViewInternal.h"
#include "../../lib/nativeRules/CapturedRuleSet.h"
#include <limits>
#include <stdexcept>
namespace forecast
{
namespace
{
void require(bool accepted)
{
	if(!accepted) throw std::invalid_argument("Incomplete or mismatched owned native read capability");
}
void validateOccurrence(const NativeReadOccurrence & input, bool validateDescriptor = true)
{
	require(input.descriptor && input.values);
	require(input.scoring ? !input.modifiers && !input.queries : input.modifiers && input.queries);
	const auto & descriptor = *input.descriptor;
	require(descriptor.creatures && descriptor.identity == input.values->identity());
	require(descriptor.identity.side == BattleSide::ATTACKER || descriptor.identity.side == BattleSide::DEFENDER);
	require(descriptor.healthBasis.battleStartMaximumHP >= 0 && descriptor.healthBasis.rebirthOriginalHP >= 0);
	const auto status = input.values->status();
	require(!status.hasForm && status.effectiveCreature == descriptor.identity.originalCreature);
	const auto & binding = input.scoring ? input.scoring->binding() : input.queries->binding();
	require((input.scoring ? input.scoring->matchesProfile(descriptor.identity.id,status.effectiveCreature)
		: input.modifiers->ownsQueryFrame(*input.queries)
			&& input.queries->matches(descriptor.identity.id,status.count,status.effectiveCreature))
		&& binding.profileOwner == descriptor.creatures && binding.owner == descriptor.identity.owner
		&& binding.side == descriptor.identity.side && binding.slot == descriptor.identity.slot
		&& binding.ghost == status.ghost && binding.summoned == status.summoned
		&& binding.natureSummoned == status.natureSummoned);
	if(!validateDescriptor) return; // Canonical immutable prototype was checked at public admission.
	const auto * creature = descriptor.creatures->creature(descriptor.identity.originalCreature);
	require(creature && creature->getId() == descriptor.identity.originalCreature);
	// Current admitted native consumers need these prototype values; absence
	// must fail at preparation rather than reach live registry fallback later.
	(void)creature->getBaseHitPoints();
	(void)creature->getAIValue();
}
}
NativeReadView::Group::Tables::Tables(std::vector<NativeReadOccurrence> inputs,
	std::shared_ptr<const NativeReadEnvironment> metadata)
	: occurrences(std::move(inputs)),
	environment(metadata ? std::make_shared<const NativeReadEnvironment>(*metadata) : nullptr)
{
	require(environment && environment->magic && environment->skeletonUpgrades
		&& environment->legacyMoraleLimits && environment->legacyLuckLimits);
	require(environment->magic->kind() == nativeRules::CapturedRuleKind::WorldMagic);
	require(!occurrences.empty() && occurrences.size() <= 4096);
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		require(environment->inputs.at(side).has_value() && environment->states.at(side).has_value());
		const auto & input = *environment->inputs.at(side);
		require(input.color.isValidPlayer() || input.color == PlayerColor::NEUTRAL || input.color == PlayerColor::UNFLAGGABLE);
		require(input.elementalAttunementProfileKnown);
		if(input.necromancy.luckBonus != 0) require(environment->necromancyLuckSource.has_value());
	}
	for(const auto range : {*environment->legacyMoraleLimits, *environment->legacyLuckLimits})
		require(range.first <= 0 && range.second >= 0);
	std::set<uint32_t> ids;
	for(auto & input : occurrences)
	{
		// Freeze public builder aliases; immutable descriptor tables/programs are
		// still shared, while no caller can mutate this receiver's identity.
		if(input.descriptor) input.descriptor = std::make_shared<const NativeReadDescriptor>(*input.descriptor);
		validateOccurrence(input);
		const auto id = input.descriptor->identity.id;
		require(input.descriptor->identity.owner == environment->inputs.at(input.descriptor->identity.side)->color);
		require(ids.insert(id).second);
		const auto warfare = environment->warfare.find(id);
		require(warfare != environment->warfare.end() && warfare->second >= 0);
	}
}
NativeReadView::Group::Tables::Tables(std::vector<NativeReadOccurrence> inputs,
	std::shared_ptr<const NativeReadEnvironment> metadata, SealedSuccessor)
	: occurrences(std::move(inputs)), environment(std::move(metadata))
{
	// Private successorViews has checked exact sealed owners and every new binding.
}
NativeReadView::Group::Group(std::vector<NativeReadOccurrence> inputs,
	std::shared_ptr<const NativeReadEnvironment> environment, std::shared_ptr<NativeReadWork> decisionWork)
	: Group(std::make_shared<const Tables>(std::move(inputs), std::move(environment)), std::move(decisionWork))
{
}
NativeReadView::Group::Group(std::shared_ptr<const Tables> tables, std::shared_ptr<NativeReadWork> decisionWork)
	: tables(std::move(tables)), occurrences(this->tables->occurrences), environment(this->tables->environment),
	decisionWork(std::move(decisionWork)), context(*this)
{
	retaliationCaches.reserve(occurrences.size());
	for(const auto & occurrence : occurrences) retaliationCaches.push_back(occurrence.values->retaliationCache());
	views.resize(occurrences.size()); // Empty slots; no native receiver/provider/HP copies.
}
NativeReadView::Group::Group(const Group & source)
	: std::enable_shared_from_this<Group>(), tables(source.tables), occurrences(tables->occurrences), environment(tables->environment),
	decisionWork(source.decisionWork), context(*this)
{
	retaliationCaches.reserve(occurrences.size());
	for(size_t index = 0; index < occurrences.size(); ++index)
		retaliationCaches.push_back(source.currentRetaliationCache(index));
	views.resize(occurrences.size());
}
int32_t NativeReadView::Group::currentRetaliationCache(size_t index) const
{
	const auto & view = views.at(index);
	return view && view->ready ? battle::UnitStateValueSnapshot::retaliationCache(*view->state) : retaliationCaches.at(index);
}
std::shared_ptr<const battle::UnitStateValueSnapshot> NativeReadView::Group::currentValues(size_t index) const
{
	const auto & original = occurrences.at(index).values;
	const auto cache = currentRetaliationCache(index);
	if(original->retaliationCache() == cache) return original;
	return std::make_shared<const battle::UnitStateValueSnapshot>(original->withRetaliationCache(cache));
}
void NativeReadView::Group::record(uint64_t NativeReadWork::* field) const noexcept
{
#ifdef NH_PERF_EXPERIMENTS
	const auto increment = [field](NativeReadWork & work) noexcept
	{
		auto & value = work.*field;
		if(value == std::numeric_limits<uint64_t>::max()) work.saturated = true;
		else ++value;
	};
	increment(counters);
	if(decisionWork) increment(*decisionWork);
#else
	(void)field;
#endif
}
const NativeReadView::Group::Receiver & NativeReadView::Group::view(size_t index) const
{
	if(index >= views.size()) throw std::out_of_range("Unknown owned native read occurrence");
	if(views[index])
	{
		if(!views[index]->ready) throw std::logic_error("Recursive incomplete native read materialization");
		return *views[index];
	}
	auto & self = const_cast<Group &>(*this);
	views[index] = std::make_unique<Receiver>(self, index, occurrences[index].queries);
	try
	{
		views[index]->restore(*currentValues(index), false);
		record(&NativeReadWork::materialized);
		return *views[index];
	}
	catch(...)
	{
		views[index].reset();
		throw;
	}
}
const battle::ProjectedUnitEnvironmentState::SideInputs & NativeReadView::Group::inputs(BattleSide side) const
{
	return *environment->inputs.at(side);
}
const battle::ProjectedUnitEnvironmentState::SideState & NativeReadView::Group::sideState(BattleSide side) const
{
	return *environment->states.at(side);
}
NativeReadView::Group::Receiver::Provider::Provider(Receiver & receiver,
	std::shared_ptr<const bonusDependencies::OwnedBonusQueryFrame> frame)
	: receiver(receiver), frame(std::move(frame)) {}
const battle::Unit * NativeReadView::Group::Receiver::Provider::evaluatedUnit() const { return receiver.state.get(); }
const std::shared_ptr<const frozenScoring::OwnedSource> & NativeReadView::Group::Receiver::Provider::scoring() const
{
	const auto & source = receiver.group.occurrences[receiver.slot].scoring;
	if(source && receiver.ready)
	{
		const auto & profile = source->binding();
		const auto & unit = *receiver.state;
		require(unit.creatureId() == profile.creature && unit.isGhost() == profile.ghost
			&& unit.summoned == profile.summoned && unit.natureSummoned == profile.natureSummoned);
	}
	return source;
}
std::shared_ptr<const bonusDependencies::OwnedBonusQueryFrame> NativeReadView::Group::Receiver::Provider::current() const
{
	if(scoring()) return {}; // Intentionally no raw recipe/Count refresh capability.
	if(!receiver.ready) return frame; // Restore validates health against its captured complete profile.
	const auto & previous = frame->binding();
	const auto & unit = *receiver.state;
	// Static identity/descriptor ownership was sealed once by the factory.
	// Only native mutable profile bits and the owned count can change here.
	require(unit.creatureId() == previous.creature && unit.isGhost() == previous.ghost
		&& unit.summoned == previous.summoned && unit.natureSummoned == previous.natureSummoned);
	const auto count = unit.getHealth().getCount(); // Direct owned ledger read: no virtual/provider recursion.
	if(count != previous.count)
	{
		auto next = previous;
		next.count = count;
		const auto & recipe = receiver.group.occurrences[receiver.slot].modifiers;
		auto replacement = recipe->refreshQueries(next, bonusDependencies::Change::Count, frame);
		require(replacement && recipe->ownsQueryFrame(*replacement));
		frame = std::move(replacement);
		receiver.group.record(&NativeReadWork::queryRebindings);
	}
	return frame;
}
void NativeReadView::Group::Receiver::Provider::onBattleFormChanged(const battle::CUnitState &) const
{
	throw std::logic_error("Owned native read capability does not admit creature-form replacement");
}
TConstBonusListPtr NativeReadView::Group::Receiver::Provider::getAllBonuses(const CSelector & selector,
	const std::string & key) const
{
	if(const auto & source = scoring())
		return receiver.stateConstructed ? source->query(selector,false,receiver.state->veteranCohesionEarned)
			: source->getAllBonuses(selector,key);
	return current()->getAllBonuses(selector,key);
}
TConstBonusListPtr NativeReadView::Group::Receiver::Provider::getUnstackedBonuses(const CSelector & selector) const
{
	if(const auto & source = scoring())
		return receiver.stateConstructed ? source->query(selector,true,receiver.state->veteranCohesionEarned)
			: source->getUnstackedBonuses(selector);
	return current()->getUnstackedBonuses(selector);
}
bool NativeReadView::Group::Receiver::Provider::mayHaveBonusType(BonusType type) const
{
	if(scoring()) return true; // Frozen selected absence proves no raw-source absence.
	return current()->mayHaveBonusType(type);
}
std::vector<Bonus> NativeReadView::Group::Receiver::Provider::sourceLocalSpellEffects() const
{
	if(scoring()) throw std::logic_error("Frozen scoring answers have no source-local raw spell provenance");
	const auto owner = current();
	const auto rows = owner->sourceLocalSpellEffects();
	return {rows.begin(), rows.end()};
}
NativeReadView::Group::Receiver::Receiver(Group & group, size_t slot,
	std::shared_ptr<const bonusDependencies::OwnedBonusQueryFrame> frame)
	: provider(*this, std::move(frame)), group(group), slot(slot), info(*group.occurrences[slot].descriptor),
	state(std::make_unique<battle::CUnitStateDetached>(&info, &provider)) { stateConstructed = true; }
void NativeReadView::Group::Receiver::restore(const battle::UnitStateValueSnapshot & snapshot, bool loan)
{
	if(loan) loanContext = std::make_unique<Context>(group, this);
	state->bindStateEnvironment(loan ? loanContext.get() : &group.context);
	snapshot.restore(*state);
	require(!state->hasBattleFormState());
	ready = true;
	(void)provider.current(); // Validate exact restored count/profile before returning a view/loan.
}
bool NativeReadView::Group::Receiver::owns(const battle::Unit & candidate) const noexcept
{
	return state.get() == &candidate;
}
std::shared_ptr<battle::CUnitState> NativeReadView::Group::loan(const battle::CUnitState & source, size_t index) const
{
	const auto & original = occurrences.at(index);
	// The caller's exact bound context has already certified its own subject.
	const auto values = battle::UnitStateValueSnapshot::capture(source);
	require(values.identity() == original.descriptor->identity);
	auto frame = original.queries;
	const auto count = values.status().count;
	if(!original.scoring && count != frame->binding().count)
	{
		auto binding = frame->binding(); binding.count = count;
		frame = original.modifiers->refreshQueries(binding, bonusDependencies::Change::Count, frame);
		require(frame != nullptr);
		record(&NativeReadWork::queryRebindings);
	}
	return loanSnapshot(values, index, std::move(frame));
}
std::shared_ptr<battle::CUnitState> NativeReadView::Group::loanSnapshot(
	const battle::UnitStateValueSnapshot & values, size_t index,
	std::shared_ptr<const bonusDependencies::OwnedBonusQueryFrame> frame) const
{
	struct Loan
	{
		std::shared_ptr<Group> group; // Retains descriptor/roster/context through nested native loans.
		std::unique_ptr<Receiver> receiver;
		Loan(std::shared_ptr<Group> owner, size_t slot,
			std::shared_ptr<const bonusDependencies::OwnedBonusQueryFrame> frame,
			const battle::UnitStateValueSnapshot & values) : group(std::move(owner)),
			receiver(std::make_unique<Receiver>(*group, slot, std::move(frame))) { receiver->restore(values, true); }
	};
	auto owner = std::make_shared<Loan>(const_cast<Group *>(this)->shared_from_this(), index, std::move(frame), values);
	record(&NativeReadWork::owningLoans);
	return std::shared_ptr<battle::CUnitState>(owner, owner->receiver->state.get());
}
NativeReadView::NativeReadView(std::shared_ptr<Group> group, size_t index) : group(std::move(group)), slot(index) {}
std::vector<std::shared_ptr<const NativeReadView>> NativeReadView::fromOwned(
	std::vector<NativeReadOccurrence> inputs, std::shared_ptr<const NativeReadEnvironment> environment,
	std::shared_ptr<NativeReadWork> decisionWork)
{
	auto group = std::make_shared<Group>(std::move(inputs), std::move(environment), std::move(decisionWork));
	std::vector<std::shared_ptr<const NativeReadView>> result;
	result.reserve(group->occurrences.size());
	for(size_t index = 0; index < group->occurrences.size(); ++index)
		result.emplace_back(new NativeReadView(group, index));
	return result;
}
std::optional<std::vector<std::shared_ptr<const NativeReadView>>> NativeReadView::successorViews(
	std::span<const std::shared_ptr<const NativeReadView>> sources,
	std::vector<NativeReadOccurrence> & inputs, std::shared_ptr<NativeReadWork> decisionWork)
{
	require(!sources.empty() && sources.size() == inputs.size());
	std::shared_ptr<const NativeReadEnvironment> environment;
	for(size_t index = 0; index < inputs.size(); ++index)
	{
		const auto & source = sources[index];
		require(source && source->group && source->slot < source->group->occurrences.size());
		// Partial publication leaves handles from DIFFERENT groups. Exact frozen
		// environment ownership proves the same context, not merely equal magic.
		if(source->slot != index || source->group->occurrences.size() != inputs.size()) return std::nullopt;
		if(!environment) environment = source->group->environment;
		else if(environment != source->group->environment) return std::nullopt;
		const auto & canonical = source->group->occurrences[source->slot].descriptor;
		const auto & descriptor = inputs[index].descriptor;
		if(!descriptor || descriptor->identity != canonical->identity || descriptor->creatures != canonical->creatures
			|| descriptor->healthBasis.battleStartMaximumHP != canonical->healthBasis.battleStartMaximumHP
			|| descriptor->healthBasis.rebirthOriginalHP != canonical->healthBasis.rebirthOriginalHP) return std::nullopt;
		validateOccurrence(inputs[index], false); // New values/program/count and dynamic bits still need exact proof.
	}
	// Each slot and its canonical descriptor was admitted with this EXACT owner;
	// roster uniqueness, side membership, prototype and warfare facts stay sealed.
	for(size_t index = 0; index < inputs.size(); ++index)
		inputs[index].descriptor = sources[index]->group->occurrences[sources[index]->slot].descriptor;
	// Only a successful successor consumes the caller's vector. No public
	// descriptor aliases enter the sealed table; no second roster copy is needed.
	auto tables = std::make_shared<const Group::Tables>(std::move(inputs), std::move(environment),
		Group::Tables::SealedSuccessor{});
	auto group = std::make_shared<Group>(std::move(tables), std::move(decisionWork));
	std::vector<std::shared_ptr<const NativeReadView>> result;
	result.reserve(sources.size());
	for(size_t index = 0; index < sources.size(); ++index)
		result.emplace_back(new NativeReadView(group, index));
	return result;
}
std::vector<std::shared_ptr<const NativeReadView>> NativeReadView::forkViews(
	std::span<const std::shared_ptr<const NativeReadView>> sources,
	std::span<const std::optional<int32_t>> currentNativeCaches)
{
	require(currentNativeCaches.empty() || currentNativeCaches.size() == sources.size());
	std::map<const Group *, std::shared_ptr<Group>> groups;
	std::vector<std::shared_ptr<const NativeReadView>> result;
	result.reserve(sources.size());
	for(size_t input = 0; input < sources.size(); ++input)
	{
		const auto & source = sources[input];
		require(source && source->group && source->slot < source->group->occurrences.size());
		auto found = groups.find(source->group.get());
		if(found == groups.end())
			found = groups.emplace(source->group.get(), std::make_shared<Group>(*source->group)).first;
		if(!currentNativeCaches.empty() && currentNativeCaches[input])
		{
			require(*currentNativeCaches[input] >= 0);
			found->second->retaliationCaches[source->slot] = *currentNativeCaches[input];
		}
		result.emplace_back(new NativeReadView(found->second, source->slot));
	}
	return result;
}
NativeReadAcquisition NativeReadView::acquireFromOwned(std::vector<NativeReadOccurrence> inputs,
	std::shared_ptr<const NativeReadEnvironment> environment, uint32_t id,
	std::shared_ptr<NativeReadWork> decisionWork)
{
	auto group = std::make_shared<Group>(std::move(inputs), std::move(environment), std::move(decisionWork));
	for(size_t index = 0; index < group->occurrences.size(); ++index)
		if(group->occurrences[index].descriptor->identity.id == id)
		{
			auto owner = std::shared_ptr<const NativeReadView>(new NativeReadView(group, index));
			auto unit = group->loanSnapshot(*group->occurrences[index].values, index, group->occurrences[index].queries);
			return {std::move(unit), std::move(owner)};
		}
	throw std::invalid_argument("Owned native acquisition recipient is not in its prepared roster");
}
std::optional<NativeReadQuerySource> NativeReadView::querySource(const battle::CUnitState & source)
{
	const IBonusBearer * bearer = nullptr;
	// Native detached wrappers reject a borrowed projected binding in this
	// getter already. That unknown source is not an owned query certificate.
	try { bearer = source.getBonusBearer(); }
	catch(const std::logic_error &) { return std::nullopt; }
	const auto * provider = dynamic_cast<const Group::Receiver::Provider *>(bearer);
	if(!provider || provider->evaluatedUnit() != &source || !provider->receiver.owns(source))
		return std::nullopt;
	const auto & receiver = provider->receiver;
	require(receiver.ready);
	const auto & occurrence = receiver.group.occurrences.at(receiver.slot);
	if(const auto & scoring = provider->scoring())
		return NativeReadQuerySource{occurrence.descriptor,{}, {},scoring};
	auto queries = provider->current();
	require(occurrence.modifiers->ownsQueryFrame(*queries));
	return NativeReadQuerySource{occurrence.descriptor, occurrence.modifiers, std::move(queries),{}};
}
const battle::CUnitState & NativeReadView::unit() const { return *group->view(slot).state; }
std::shared_ptr<const battle::Unit> NativeReadView::retainUnit() const
{
	return std::shared_ptr<const battle::Unit>(group, &unit());
}
std::shared_ptr<const battle::UnitStateValueSnapshot> NativeReadView::valuesOwner() const { return group->currentValues(slot); }
std::shared_ptr<const bonusDependencies::OwnedBonusQueryFrame> NativeReadView::queryFrame() const
{
	if(group->occurrences[slot].scoring) return {};
	return group->view(slot).frame();
}
std::shared_ptr<const frozenScoring::OwnedSource> NativeReadView::scoringSource() const { return group->occurrences[slot].scoring; }
std::shared_ptr<const NativeReadEnvironment> NativeReadView::environmentOwner() const { return group->environment; }
std::shared_ptr<const void> NativeReadView::workIdentity() const { return group; }
NativeReadWork NativeReadView::work() const noexcept { return group->counters; }
}
