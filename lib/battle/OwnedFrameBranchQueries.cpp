/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedFrameBranchQueries.h"
#include "OwnedUnitBonusFrameData.h"
#include "UnitStateValueExchange.h"
#include "StackEffectRefresh.h"
#include "../bonuses/BonusList.h"
#include <stdexcept>
namespace battle
{
void OwnedFrameBranchQueries::synchronize(OwnedUnitBonusFrame & recipient, const std::vector<CurrentUnit> & ordered)
{
	if(ordered.empty()) throw std::invalid_argument("Missing current owned roster");
	std::set<uint32_t> ids;
	bool containsRecipient = false;
	std::vector<std::shared_ptr<OwnedUnitBonusFrame>> staged;
	staged.reserve(ordered.size());
	for(const auto & row : ordered)
	{
		if(!row.frame || !ids.insert(row.frame->unit().unitId()).second)
			throw std::invalid_argument("Missing or duplicate current owned occurrence");
		containsRecipient |= row.frame.get() == &recipient;
		const auto & data = *row.frame->data;
		if(!OwnedUnitBonusFrame::samePayloadOwner(*recipient.data->graph, *data.graph)
			|| recipient.data->creatures != data.creatures
			|| recipient.data->creatures.owner_before(data.creatures)
			|| data.creatures.owner_before(recipient.data->creatures))
			throw std::invalid_argument("Current roster is outside its captured payload/descriptor domain");
		auto next = row.frame->fork();
		if(row.values)
		{
			data.validateValues(*row.values);
			if(row.values->battleFormCreature() != row.frame->unit().battleFormCreature()
				|| row.values->hasBattleFormState() != row.frame->unit().hasBattleFormState())
				throw std::invalid_argument("Current form requires an owned source-domain transition");
			*next->data->subject = *row.values;
		}
		// A fork must retain pending nested metadata owners, not only Bonus aliases.
		next->data->pendingPayloads = data.pendingPayloads;
		staged.push_back(std::move(next));
	}
	if(!containsRecipient) throw std::invalid_argument("Recipient is absent from current owned roster");
	std::vector<OwnedUnitBonusFrame::Data::CurrentPeers> peers;
	peers.reserve(ordered.size());
	for(const auto & row : ordered) peers.push_back(row.frame->data->prepareCurrentPeers(staged));
	// Everything that allocates or validates precedes this publication. Subjects
	// keep their exact descriptor/provider address; environments retire before peers.
	for(size_t i = 0; i < ordered.size(); ++i)
	{
		auto & data = *ordered[i].frame->data;
		exchangePreparedUnitStateValues(*data.subject, *staged[i]->data->subject);
		data.env.swap(peers[i].environment);
		data.peerQueries->swap(peers[i].queries);
		data.subject->bindStateEnvironment(data.env.get());
		data.hasCurrentRoster = true;
	}
}
const IUnitEnvironment & OwnedFrameBranchQueries::environment(const OwnedUnitBonusFrame & recipient)
{
	return *recipient.data->env;
}
std::shared_ptr<CUnitState> OwnedFrameBranchQueries::acquireState(const std::shared_ptr<const OwnedUnitBonusFrame> & recipient)
{
	if(!recipient) throw std::invalid_argument("Missing owned preparation source");
	auto retained = std::make_shared<OwnedUnitBonusFrame::Scratch>(recipient->scratch());
	auto * state = &retained->unit();
	return std::shared_ptr<CUnitState>(std::move(retained), state);
}
CUnitState & OwnedFrameBranchQueries::effectQueryState(OwnedUnitBonusFrame & recipient)
{
	return *recipient.data->subject;
}
BonusList OwnedFrameBranchQueries::sourceExports(const OwnedUnitBonusFrame & recipient)
{
	const auto & data = *recipient.data;
	BonusList original, result;
	for(const auto index : data.graph->sourceExportRecords())
	{
		const auto & raw = data.graph->payload(index);
		const auto found = data.sourceReplacements.find(raw);
		original.push_back(found == data.sourceReplacements.end() ? raw : found->second);
	}
	recipientBonusOverlay::copyInherited(result, original, data.ledger.view(), true);
	recipientBonusOverlay::append(result, data.ledger.view(), Selector::all, true);
	return result;
}
void OwnedFrameBranchQueries::removeMatching(OwnedUnitBonusFrame & recipient, const CSelector & selector)
{
	auto next = recipient.data->ledger;
	for(const auto & record : sourceExports(recipient))
		if(selector(record.get()))
		{
			if(record->propagator || record->propagationUpdater)
				throw std::invalid_argument("Recipient edits cannot change source propagation");
			next.removed.insert(record);
			std::erase(next.added, record); std::erase(next.updated, record);
		}
	recipient.data->ledger = std::move(next);
}
OwnedFrameBranchQueries::PreparedRecords OwnedFrameBranchQueries::prepareRecords(const std::vector<Bonus> & records)
{
	std::vector<std::shared_ptr<const Bonus>> roots;
	for(const auto & record : records)
	{
		if(record.propagator || record.propagationUpdater)
			throw std::invalid_argument("Recipient edits cannot invent source propagation");
		roots.push_back(std::make_shared<Bonus>(record));
	}
	return PreparedRecords(std::shared_ptr<const ownedBonusPayloads::Captured>(ownedBonusPayloads::copyOwnedValues(roots)));
}
void OwnedFrameBranchQueries::append(OwnedUnitBonusFrame & recipient, const PreparedRecords & records, size_t index)
{
	const auto & record = records.owner->roots().at(index);
	auto next = recipient.data->ledger;
	next.added.push_back(record);
	if(record->type == BonusType::PHYSICAL_AFFLICTION) next.capturedGroups.emplace(record->source, record->sid);
	auto owners = recipient.data->pendingPayloads;
	owners.push_back(records.owner);
	recipient.data->pendingPayloads.swap(owners);
	recipient.data->ledger = std::move(next);
}
void OwnedFrameBranchQueries::refresh(OwnedUnitBonusFrame & recipient, const Bonus & value)
{
	auto next = recipient.data->ledger;
	auto replacements = recipient.data->sourceReplacements;
	for(const auto & original : sourceExports(recipient))
		if(stackEffectRefresh::matchesIdentity(value, original.get()))
		{
			if(original->propagator || original->propagationUpdater)
				throw std::invalid_argument("Recipient refresh cannot change source propagation");
			// Refresh changes scalar tags/timer only. Keep the existing owned nested
			// aliases and their graph/pending pool rather than cloning a new domain.
			auto updated = std::make_shared<Bonus>(*original);
			stackEffectRefresh::refreshFields(*updated, value);
			const auto added = std::find(next.added.begin(), next.added.end(), original);
			if(added != next.added.end()) *added = updated;
			else
			{
				const auto previous = std::find_if(replacements.begin(), replacements.end(),
					[&](const auto & entry) { return entry.second == original; });
				if(previous == replacements.end()) replacements[original] = updated;
				else previous->second = updated;
			}
		}
	recipient.data->sourceReplacements.swap(replacements);
	recipient.data->ledger = std::move(next);
}
}
