/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedUnitBonusFrameData.h"
#include <stdexcept>
namespace
{
recipientBonusOverlay::Layer emptyLedger()
{
	recipientBonusOverlay::Records added, updated;
	std::set<std::shared_ptr<Bonus>> removed;
	std::optional<recipientBonusOverlay::Values> stacked;
	std::optional<recipientBonusOverlay::Records> raw;
	recipientBonusOverlay::Groups groups;
	const bool veteran = false;
	return recipientBonusOverlay::Layer({added, updated, removed, stacked, raw, groups, veteran});
}
}
namespace battle
{
OwnedUnitBonusFrame::Data::Data(Identity id, std::shared_ptr<const OwnedUnitCreatureResolver> types,
	std::shared_ptr<const OwnedRecipientBonusGraph> raw, std::map<CreatureID, FormSources> sources,
	std::vector<size_t> spells, OwnedUnitEnvironment::Facts metadata,
	std::vector<std::shared_ptr<const Unit>> units, size_t position, recipientBonusOverlay::Layer effects)
	: creatures(std::move(types)), identity(id), graph(std::move(raw)), forms(std::move(sources)),
	 sourceLocalSpells(std::move(spells)), environment(std::move(metadata)), peers(std::move(units)),
	 recipientPosition(position), ledger(std::move(effects)), info(*this), view(*this)
{
	validate();
	subject = std::make_shared<CUnitStateDetached>(&info, &view);
	auto roster = peers; roster.at(recipientPosition) = subject;
	env = std::make_unique<OwnedUnitEnvironment>(std::move(roster), environment);
	for(const auto index : graph->rootLocalRecords())
	{
		const auto & record = graph->payload(index);
		if(record->type == BonusType::PHYSICAL_AFFLICTION) ledger.capturedGroups.emplace(record->source, record->sid);
	}
	subject->bindStateEnvironment(env.get()); // No localInit/reset of copied/prepared state.
}
void OwnedUnitBonusFrame::Data::validate() const
{
	if(!creatures || !graph || identity.baseAmount < 0 || identity.healthBasis.battleStartMaximumHP < 0
		|| identity.healthBasis.rebirthOriginalHP < 0 || !forms.contains(identity.originalCreature)
		|| (identity.side != BattleSide::ATTACKER && identity.side != BattleSide::DEFENDER))
		throw std::invalid_argument("Incomplete owned unit frame identity/context");
	if(!environment.colors.at(identity.side) || *environment.colors.at(identity.side) != identity.owner
		|| recipientPosition >= peers.size() || peers[recipientPosition])
		throw std::invalid_argument("Owned frame requires explicit exact affiliation/recipient roster slot");
	for(size_t i = 0; i < peers.size(); ++i)
		if(i != recipientPosition && !peers[i]) throw std::invalid_argument("Missing owned environment peer occurrence");
	for(const auto & [id, sources] : forms)
	{
		const auto * type = creatures->creature(id);
		for(const auto * query : {&sources.full, &sources.native})
		{
			graph->validateRootQuery(*query);
			const auto & facts = query->facts;
			if(facts.nodeType != BonusNodeType::STACK_BATTLE || !facts.acceptance || !facts.creatureContext
				|| !facts.creatureContext->creature || facts.creatureContext->creature->id != id
				|| facts.creatureContext->creature->level != type->getLevel()
				|| facts.creatureContext->faction != type->getFactionID()
				|| facts.acceptance->metadata.slot != identity.slot)
				throw std::invalid_argument("Owned form facts do not match descriptor/recipient identity");
		}
	}
	for(const auto index : sourceLocalSpells)
	{
		auto query = forms.at(identity.originalCreature).full; query.records = {index}; graph->validateRootQuery(query);
		if(graph->payload(index)->source != BonusSource::SPELL_EFFECT)
			throw std::invalid_argument("Owned spell export handle is not a raw spell record");
	}
}
void OwnedUnitBonusFrame::Data::validateSnapshot(const JsonNode & snapshot) const
{
	if(!snapshot.isStruct() || !snapshot["state"].isStruct()) throw std::invalid_argument("Missing owned unit state");
	const auto & state = snapshot["state"];
	for(const auto * name : {"battleFormCreature", "battleFormOriginalCreature"})
	{
		const auto found = state.Struct().find(name);
		if(found == state.Struct().end() || found->second.isNull()) continue;
		if(!found->second.isString()) throw std::invalid_argument("Owned form recipe key must be a string");
		if(found->second.String().empty()) continue;
		const CreatureID id(creatures->decode(found->second.String()));
		if(!forms.contains(id) || (std::string_view(name) == "battleFormOriginalCreature" && id != identity.originalCreature))
			throw std::invalid_argument("Missing or mismatched owned form source recipe");
	}
}
std::shared_ptr<OwnedUnitBonusFrame> OwnedUnitBonusFrame::fromOwnedInputs(Inputs inputs)
{
	if(!inputs.state.isStruct() || !inputs.state["state"].isStruct() || !inputs.state["state"]["health"].isStruct())
		throw std::invalid_argument("Owned frame requires a full captured state snapshot");
	auto result = std::shared_ptr<OwnedUnitBonusFrame>(new OwnedUnitBonusFrame);
	auto graph = OwnedRecipientBonusGraph::fromOwnedInputs(std::move(inputs.nodes), inputs.root, std::move(inputs.extraRecords));
	result->data = std::make_unique<Data>(inputs.identity, std::move(inputs.creatures), std::move(graph),
		std::move(inputs.forms), std::move(inputs.sourceLocalSpells), std::move(inputs.environment),
		std::move(inputs.environmentUnits), inputs.recipientPosition, emptyLedger());
	result->data->validateSnapshot(inputs.state);
	result->data->subject->load(inputs.state);
	return result;
}
OwnedUnitBonusFrame::~OwnedUnitBonusFrame() = default;
const CUnitState & OwnedUnitBonusFrame::unit() const { return *data->subject; }
JsonNode OwnedUnitBonusFrame::save() const { return data->subject->save(); }
void OwnedUnitBonusFrame::load(const JsonNode & snapshot)
{
	data->validateSnapshot(snapshot);
	data->subject->load(snapshot);
}
void OwnedUnitBonusFrame::Data::requireRecord(size_t index) const
{
	auto query = forms.at(identity.originalCreature).full; query.records = {index}; graph->validateRootQuery(query);
	const auto & record = graph->payload(index);
	if(record->propagator || record->propagationUpdater)
		throw std::invalid_argument("Recipient-local edits cannot invent propagation changes");
}
void OwnedUnitBonusFrame::removeRecords(const std::vector<size_t> & records)
{
	auto next = data->ledger;
	for(const auto index : records)
	{
		data->requireRecord(index); const auto & record = data->graph->payload(index);
		next.removed.insert(record);
		std::erase(next.added, record); std::erase(next.updated, record);
	}
	data->ledger = std::move(next);
}
std::shared_ptr<OwnedUnitBonusFrame> OwnedUnitBonusFrame::fork() const
{
	auto result = std::shared_ptr<OwnedUnitBonusFrame>(new OwnedUnitBonusFrame);
	result->data = std::make_unique<Data>(data->identity, data->creatures, data->graph, data->forms,
		data->sourceLocalSpells, data->environment, data->peers, data->recipientPosition, data->ledger);
	*result->data->subject = static_cast<const CUnitState &>(*data->subject); // Canonical value copy; owners remain private/exact.
	return result;
}
OwnedUnitBonusFrame::Scratch::Scratch(std::shared_ptr<const OwnedUnitBonusFrame> frame,
	std::unique_ptr<OwnedUnitEnvironment> env, std::shared_ptr<CUnitState> unit)
	: owner(std::move(frame)), environment(std::move(env)), value(std::move(unit)) {}
OwnedUnitBonusFrame::Scratch & OwnedUnitBonusFrame::Scratch::operator=(Scratch && other) noexcept
{
	if(this != &other)
	{
		value.reset(); // Retire borrowed state BEFORE releasing its old owner.
		environment.reset();
		owner = std::move(other.owner);
		environment = std::move(other.environment);
		value = std::move(other.value);
	}
	return *this;
}
CUnitState & OwnedUnitBonusFrame::Scratch::unit() const { return *value; }
OwnedUnitBonusFrame::Scratch OwnedUnitBonusFrame::Scratch::nested() const
{
	auto result = std::make_shared<CUnitStateDetached>(&owner->data->info, owner->data->subject.get());
	*result = static_cast<const CUnitState &>(*value); // Frozen info/provider, no borrowed mutable parent descriptor.
	auto env = OwnedUnitBonusFrame::derivedEnvironment(*owner, *result);
	result->bindStateEnvironment(env.get());
	return Scratch(owner, std::move(env), std::move(result));
}
OwnedUnitBonusFrame::Scratch OwnedUnitBonusFrame::scratch() const
{
	auto frozen = fork(); // Deep scalar/ledger isolation, immutable raw payload owner shared.
	auto result = std::make_shared<CUnitStateDetached>(&frozen->data->info, frozen->data->subject.get());
	*result = static_cast<const CUnitState &>(*frozen->data->subject);
	auto env = derivedEnvironment(*frozen, *result);
	result->bindStateEnvironment(env.get());
	return Scratch(frozen, std::move(env), std::move(result));
}
std::unique_ptr<OwnedUnitEnvironment> OwnedUnitBonusFrame::derivedEnvironment(
	const OwnedUnitBonusFrame & source, const CUnitStateDetached & derived)
{
	return source.data->env->forDerivedSubject(*source.data->subject, derived, source.data->info);
}
Units OwnedUnitBonusFrame::adjacentEnvironment(const OwnedUnitEnvironment & environment, const Unit & geometry)
{
	return environment.adjacentGeometry(geometry);
}
BonusList OwnedUnitBonusFrame::queryGraph(const OwnedRecipientBonusGraph & graph,
	const OwnedRecipientBonusGraph::RootQuery & query, const BonusList & raw, const CSelector & selector, bool unstacked, const Unit & unit)
{
	return graph.queryRootRecords(selector, unstacked, query, raw, &unit);
}
}
