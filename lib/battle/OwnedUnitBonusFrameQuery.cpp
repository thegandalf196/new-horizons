/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedUnitBonusFrameData.h"
#include "../CStack.h"
#include <stdexcept>
namespace battle
{
OwnedRecipientBonusGraph::RootQuery OwnedUnitBonusFrame::Data::context(const CUnitState & unit, const FormSources & source, bool native) const
{
	auto query = decorate(native ? source.native : source.full, unit);
	auto & acceptance = query.facts.acceptance->state;
	acceptance.hasForm = unit.hasBattleForm(); acceptance.currentForm = unit.battleFormCreature();
	acceptance.originalForm = unit.battleFormOriginalCreature(); acceptance.ghost = unit.isGhost();
	acceptance.summoned = unit.summoned; acceptance.natureSummoned = unit.natureSummoned;
	return query;
}
OwnedRecipientBonusGraph::RootQuery OwnedUnitBonusFrame::Data::decorate(
	OwnedRecipientBonusGraph::RootQuery query, const CUnitState & unit) const
{
	query.facts.stackCount = unit.getCount(); query.facts.owner = unit.unitOwner();
	if(query.facts.adjacencyUnits)
	{
		query.facts.adjacencyUnits->clear();
		for(const auto * neighbor : OwnedUnitBonusFrame::adjacentEnvironment(*env, unit))
			query.facts.adjacencyUnits->push_back({neighbor->unitId(), neighbor->creatureId(), neighbor->unitOwner(), neighbor->alive(), neighbor->isGhost()});
	}
	return query;
}
const OwnedRecipientBonusGraph & OwnedUnitBonusFrame::Data::sourceGraph(const FormSources & source, bool native) const
{
	const auto & selected = native ? source.nativeGraph : source.fullGraph;
	return selected ? *selected : *graph;
}
BonusList OwnedUnitBonusFrame::Data::raw(const OwnedRecipientBonusGraph & owner, const std::vector<size_t> & records) const
{
	BonusList original, result;
	for(const auto index : records) original.push_back(owner.payload(index));
	// All inputs here are RAW source/owned pending records. No effective snapshots are promoted.
	recipientBonusOverlay::copyInherited(result, original, ledger.view(), true);
	recipientBonusOverlay::append(result, ledger.view(), Selector::all, true);
	return result;
}
TConstBonusListPtr OwnedUnitBonusFrame::Data::query(const CSelector & selector, bool unstacked) const
{
	const auto & source = forms.at(subject->battleFormCreature());
	const auto & selected = sourceGraph(source, false);
	const auto query = context(*subject, source, false);
	return std::make_shared<BonusList>(OwnedUnitBonusFrame::queryGraph(selected, query, raw(selected, query.records), selector, unstacked, *subject));
}
void OwnedUnitBonusFrame::Data::View::onBattleFormChanged(const CUnitState & unit) const
{
	if(&unit != data.subject.get() || !data.forms.contains(unit.battleFormCreature()))
		throw std::logic_error("Owned complete-form view requires its exact unit/captured form");
}
TConstBonusListPtr OwnedUnitBonusFrame::Data::View::getAllBonuses(const CSelector & selector, const std::string &) const
{
	return data.query(selector, false);
}
TConstBonusListPtr OwnedUnitBonusFrame::Data::View::getUnstackedBonuses(const CSelector & selector) const
{
	return data.query(selector, true);
}
TConstBonusListPtr OwnedUnitBonusFrame::Data::View::ownedNativeFormBonuses(
	const CUnitState & form, const CSelector & selector, bool unstacked) const
{
	const auto * detached = dynamic_cast<const CUnitStateDetached *>(&form);
	if(form.unitCreatureResolver() != data.creatures.get() || form.unitId() != data.identity.id
		|| (&form != data.subject.get() && (!detached || !detached->hasUnitInfoAncestor(data.info)
			|| !detached->hasBonusProviderAncestor(*data.subject))))
		throw std::logic_error("Owned native-form query is outside its retained recipient ancestry");
	const auto & source = data.forms.at(form.battleFormCreature());
	const auto & selected = data.sourceGraph(source, true);
	const auto query = data.context(form, source, true);
	BonusList raw;
	for(const auto index : query.records) raw.push_back(selected.payload(index));
	const CSelector nativeSelector([&](const Bonus * record)
	{
		return isBattleFormNativeBonus(record, form.battleFormCreature()) && selector(record);
	});
	return std::make_shared<BonusList>(OwnedUnitBonusFrame::queryGraph(selected, query, raw, nativeSelector, unstacked, form));
}
std::vector<Bonus> OwnedUnitBonusFrame::Data::View::sourceLocalSpellEffects() const
{
	BonusList raw;
	for(const auto index : data.sourceLocalSpells) raw.push_back(data.graph->payload(index));
	BonusList result;
	recipientBonusOverlay::copyInherited(result, raw, data.ledger.view(), true);
	recipientBonusOverlay::append(result, data.ledger.view(), Selector::sourceTypeSel(BonusSource::SPELL_EFFECT), true);
	std::vector<Bonus> values;
	for(const auto & record : result) values.push_back(*record);
	return values;
}
}
