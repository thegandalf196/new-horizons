/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedUnitBonusFrameData.h"
#include <stdexcept>
namespace
{
bool peerOutput(const Bonus * bonus)
{
	return bonus->type == BonusType::SPELL_RESISTANCE_AURA || bonus->type == BonusType::HYPNOTIZED
		|| bonus->type == BonusType::CREATURE_ABILITY_SUPPRESSION;
}
}
namespace battle
{
class OwnedUnitBonusFrame::Data::Peer
{
	const Data & owner;
	Identity identity, body;
	static Identity effectiveBody(const PeerInput & input)
	{
		if(!input.query.facts.creatureContext || !input.query.facts.creatureContext->creature)
			throw std::invalid_argument("Owned peer effective metadata is unknown");
		auto value = input.identity; value.originalCreature = input.query.facts.creatureContext->creature->id;
		return value;
	}
	std::shared_ptr<const OwnedRecipientBonusGraph> graph;
	OwnedRecipientBonusGraph::RootQuery source;
	std::map<CreatureID, QuerySource> variants;
	Info info;
	class View final : public ProjectedBonusView
	{
		const Peer & peer;
	public:
		explicit View(const Peer & peer) : peer(peer) {}
		const Unit * evaluatedUnit() const override { return peer.subject.get(); }
		void onBattleFormChanged(const CUnitState & unit) const override
		{
			if(&unit != peer.subject.get() || unit.battleFormCreature() != peer.source.facts.creatureContext->creature->id)
				throw std::logic_error("Opaque peer form is outside its captured context");
		}
		TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string &) const override { return peer.query(selector, false); }
		TConstBonusListPtr getUnstackedBonuses(const CSelector & selector) const override { return peer.query(selector, true); }
		int32_t getTreeVersion() const override { return 0; }
	} view;
public:
	std::shared_ptr<CUnitStateDetached> subject; // Retire before view/info/graph; no env/native provider alias.
	Peer(const Data & owner, const PeerInput & input)
		: owner(owner), identity(input.identity), body(effectiveBody(input)), graph(input.graph), source(input.query), variants(input.sourceVariants), info(body, owner.creatures), view(*this)
	{
		if(!input.values || !graph || (identity.side != BattleSide::ATTACKER && identity.side != BattleSide::DEFENDER)
			|| !owner.environment.colors.at(identity.side) || *owner.environment.colors.at(identity.side) != identity.owner
			|| identity.id != input.values->unitId() || identity.side != input.values->unitSide()
			|| identity.owner != input.values->unitOwner() || identity.baseAmount != input.values->unitBaseAmount()
			|| identity.slot != input.values->unitSlot() || identity.originalCreature != input.values->battleFormOriginalCreature()
			|| identity.baseAmount < 0 || identity.healthBasis.battleStartMaximumHP < 0 || identity.healthBasis.rebirthOriginalHP < 0
			|| source.facts.nodeType != BonusNodeType::STACK_BATTLE
			|| !source.facts.acceptance || !source.facts.creatureContext || !source.facts.creatureContext->creature)
			throw std::invalid_argument("Incomplete or mismatched owned peer input");
		graph->validateRootQuery(source);
		if(!variants.empty() && variants.size() != owner.forms.size())
			throw std::invalid_argument("Opaque peer form recipe set is not exact");
		if(!variants.empty())
			for(const auto & [form, recipe] : owner.forms)
			{
				const auto found = variants.find(form);
				if(found == variants.end() || !found->second.graph
					|| !OwnedUnitBonusFrame::samePayloadOwner(*owner.graph, *found->second.graph))
					throw std::invalid_argument("Missing same-pool opaque peer form recipe");
				auto context = found->second.query; context.facts = source.facts;
				found->second.graph->validateRootQuery(context);
				// Context facts stay the base peer facts; variants supply only exact parent/local recipe addresses.
				if(found->second.query.facts.nodeType != source.facts.nodeType)
					throw std::invalid_argument("Peer form recipe type mismatch");
			}
		owner.creatures->creature(identity.originalCreature);
		const auto form = input.values->battleFormCreature(); const auto * type = owner.creatures->creature(form);
		const auto & state = source.facts.acceptance->state;
		if(source.facts.creatureContext->creature->id != form
			|| source.facts.creatureContext->creature->level != type->getLevel()
			|| source.facts.creatureContext->faction != type->getFactionID() || state.hasForm != input.values->hasBattleForm()
			|| state.currentForm != form || state.originalForm != input.values->battleFormOriginalCreature()
			|| state.ghost != input.values->isGhost() || state.summoned != input.values->summoned
			|| state.natureSummoned != input.values->natureSummoned || source.facts.acceptance->metadata.slot != identity.slot)
			throw std::invalid_argument("Owned peer effective descriptor/state does not match its raw source facts");
		subject = std::make_shared<CUnitStateDetached>(&info, &view);
		// Opaque peers need ONLY count/alive, ghost and geometry plus canonical query metadata.
		// Keep the original/effective form receipt in source.facts.acceptance, not a mutable injury clone.
		subject->health = input.values->health; subject->ghost = input.values->ghost; subject->position = input.values->position;
	}
	TConstBonusListPtr query(const CSelector & selector, bool unstacked) const
	{
		const auto * selected = graph.get(); auto recipe = source;
		if(!variants.empty())
		{
			const auto & variant = variants.at(owner.subject->battleFormCreature());
			selected = variant.graph.get(); recipe.parents = variant.query.parents; recipe.records = variant.query.records;
		}
		auto context = owner.decorate(std::move(recipe), *subject);
		BonusList raw;
		for(const auto index : context.records) raw.push_back(selected->payload(index));
		const auto accepted = OwnedUnitBonusFrame::queryGraph(*selected, context, raw, Selector::all, unstacked, *subject);
		auto result = std::make_shared<BonusList>();
		accepted.getBonuses(*result, CSelector(peerOutput).And(selector)); // Dependencies never become peer outputs.
		return result;
	}
	Peer(const Data & owner, const Peer & other)
		: owner(owner), identity(other.identity), body(other.body), graph(other.graph), source(other.source), variants(other.variants), info(body, owner.creatures), view(*this)
	{
		subject = std::make_shared<CUnitStateDetached>(&info, &view);
		subject->health = other.subject->health; subject->ghost = other.subject->ghost; subject->position = other.subject->position;
	}
};
std::vector<std::shared_ptr<const Unit>> OwnedUnitBonusFrame::Data::buildPeers(const std::vector<PeerInput> & sources)
{
	if(!peers.empty() || recipientPosition > sources.size())
		throw std::invalid_argument("Opaque peers require their exact ordered recipient insertion slot");
	std::vector<std::shared_ptr<const Unit>> result;
	for(const auto & source : sources)
	{
		auto peer = std::make_shared<Peer>(*this, source);
		result.push_back(peer->subject); peerQueries->push_back(std::move(peer));
	}
	return result;
}
std::vector<std::shared_ptr<const Unit>> OwnedUnitBonusFrame::Data::copyPeers(const Data & source)
{
	if(!peers.empty() || recipientPosition > source.peerQueries->size())
		throw std::invalid_argument("Owned peer fork requires its exact ordered insertion slot");
	std::vector<std::shared_ptr<const Unit>> result;
	for(const auto & original : *source.peerQueries)
	{
		auto peer = std::make_shared<Peer>(*this, *original);
		result.push_back(peer->subject); peerQueries->push_back(std::move(peer));
	}
	return result;
}
void OwnedUnitBonusFrame::Data::validateValues(const CUnitState & values) const
{
	if(values.unitId() != identity.id || values.unitSide() != identity.side || values.unitOwner() != identity.owner
		|| values.unitBaseAmount() != identity.baseAmount || values.unitSlot() != identity.slot || !forms.contains(values.battleFormCreature())
		|| values.battleFormOriginalCreature() != identity.originalCreature)
		throw std::invalid_argument("Owned scalar source identity/form does not match captured inputs");
}
}
