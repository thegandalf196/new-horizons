/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/LiveUnitEnvironmentContext.h"
#include "../../lib/battle/CommittedBonusOperations.h"
#include "../../lib/bonuses/ProjectedBonusRoster.h"
#include "../../lib/CStack.h"

void HypotheticBattle::captureCurrentBonusOperations(const Subject & source)
{
	if(const auto * parent = dynamic_cast<const HypotheticBattle *>(source.get()))
		committedBonusOperations = parent->committedBonusOperations;
	else
		committedBonusOperations = battle::LiveUnitEnvironmentContext(*source).committedBonusOperations();
}
void HypotheticBattle::validateCurrentBonusOperationSource() const
{
	if(!committedBonusOperations) return;
	const HypotheticBattle * current = this;
	std::set<const HypotheticBattle *> seen;
	while(current)
	{
		if(!seen.insert(current).second || seen.size() > 512
			|| !current->subject || !current->sourceBinding.isCurrentSource(current->subject)
			|| current->committedBonusOperations != committedBonusOperations)
			throw std::invalid_argument("Current bonus operations lost their exact source interval");
		current = dynamic_cast<const HypotheticBattle *>(current->subject.get());
	}
}
TConstBonusListPtr HypotheticBattle::currentBonusOperationBaseline(const CStack & native,
	const StackWithBonuses & unit, const ProjectedBonusRoster & roster,
	const BonusList & nativeBaseline) const
{
	if(!committedBonusOperations) return {};
	if(!committedBonusOperations->containsSource(native)) return {}; // Existing cold-created lane only.
	const auto found = committedElementalFacts.find(unit.unitId());
	if(found == committedElementalFacts.end() || found->second.source.get() != &native
		|| !admitsCommittedElementalFacts(unit, found->second))
		throw std::invalid_argument("Current bonus operations require exact native info/provider ancestry");
	const auto expected = battleGetUnitsIf([](const battle::Unit *) { return true; });
	const auto actual = roster.orderedUnits();
	if(actual.size() != expected.size() || roster.usesOrderedOccurrences())
		throw std::invalid_argument("Current bonus operations require the complete current roster");
	for(size_t index = 0; index < expected.size(); ++index)
	{
		const auto * peer = actual[index];
		if(!peer || (peer != expected[index] && peer != &unit)
			|| peer->unitId() != expected[index]->unitId())
			throw std::invalid_argument("Current bonus roster changed an admitted occurrence");
		if(const auto source = committedElementalFacts.find(peer->unitId()); source != committedElementalFacts.end())
		{
			if(!admitsCommittedElementalFacts(*peer, source->second))
				throw std::invalid_argument("Current bonus roster lost native info/provider ancestry");
		}
		else
		{
			// Existing summon creation owns a cold CStack and publishes its exact
			// wrapper in this branch. Its raw identity may participate as a neighbor;
			// this grants no native operation-source or hidden-policy authority.
			const auto * created = dynamic_cast<const StackWithBonuses *>(peer);
			if(!created || !created->ownedBearer || battleGetUnitByID(peer->unitId()) != peer)
				throw std::invalid_argument("Current bonus roster lacks a registered creation recipe");
			const IBonusBearer * provider = created;
			std::set<const StackWithBonuses *> layers;
			while(const auto * layer = dynamic_cast<const StackWithBonuses *>(provider))
			{
				if(!layers.insert(layer).second || layers.size() > 512 || !layer->originalUnit
					|| layer->originalUnit->getBonusBearer() != layer->origBearer
					|| layer->owner->committedBonusOperations != committedBonusOperations)
					throw std::invalid_argument("Current bonus neighbor lost its creation provider lineage");
				provider = layer->origBearer;
			}
			if(provider != created->ownedBearer.get())
				throw std::invalid_argument("Current bonus neighbor changed its retained creation source");
		}
	}
	return committedBonusOperations->replaceCurrent(native, unit, roster, nativeBaseline);
}
