/*
 * StackRecipientBonusOverlay.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "StackWithBonuses.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/bonuses/OwnedBonusPayloads.h"
#include "../../lib/CStack.h"

recipientBonusOverlay::View StackWithBonuses::recipientOverlayView() const
{
	return {bonusesToAdd, bonusesToUpdate, bonusesToRemove, projectedEffects,
		projectedUnstackedEffects, capturedPhysicalAfflictionGroups, veteranCohesionEarned, parentClockRecipe};
}

bool StackWithBonuses::captureRecipientOverlayLayers(std::vector<recipientBonusOverlay::Layer> & layers,
	const CStack * & native) const
{
	layers.clear();
	native = nullptr;
	std::set<const StackWithBonuses *> seen;
	const IBonusBearer * source = this;
	size_t remaining = 8192;
	while(const auto * layer = dynamic_cast<const StackWithBonuses *>(source))
	{
		if(typeid(*layer) != typeid(StackWithBonuses) || seen.size() >= 512 || !seen.insert(layer).second
			|| !layer->originalUnit || layer->originalUnit->unitId() != id || layer->hasBattleFormState()
			|| layer->damageScoringBonuses || layer->damageScoringUnstackedBonuses)
			return false;
		const auto view = layer->recipientOverlayView();
		for(const auto * records : {&view.added, &view.updated})
		{
			if(records->size() > remaining || std::ranges::any_of(*records, [](const auto & record) { return !record; }))
				return false;
			remaining -= records->size();
		}
		if(view.removed.size() > remaining || view.removed.contains(nullptr))
			return false;
		remaining -= view.removed.size();
		if(view.stackedSnapshot)
		{
			if(view.stackedSnapshot->size() > remaining)
				return false;
			remaining -= view.stackedSnapshot->size();
		}
		if(view.unstackedSnapshot)
		{
			if(view.unstackedSnapshot->size() > remaining
				|| std::ranges::any_of(*view.unstackedSnapshot, [](const auto & record) { return !record; }))
				return false;
			remaining -= view.unstackedSnapshot->size();
		}
		if(view.clockParents && !layer->clockRecipeImportNeutral()) return false;
		layers.emplace_back(view);
		layers.back().clockParents.reset(); // Proved no-op only; active clock data is never discarded.
		source = layer->origBearer;
	}
	native = dynamic_cast<const CStack *>(source);
	if(!native || typeid(*native) != typeid(CStack) || native->unitId() != id)
		return false;
	std::ranges::reverse(layers); // Original native recipient outward through the real parent order.
	return true;
}

void StackWithBonuses::captureParentClockRecipe()
{
	const auto * parent = dynamic_cast<const StackWithBonuses *>(origBearer);
	if(!parent) return; // Cold foreign/detached construction asks no provenance question.
	auto recipe = recipientBonusOverlay::captureParentClocks(parent->recipientOverlayView(), *this);
	if(recipe->hasAliases)
	{
		// Clock-present only: pointer order anchors, no unrelated scalar/deep capture.
		const auto order = parent->getUnstackedBonuses(Selector::all);
		for(const auto & record : *order)
			recipe->orderWitness.push_back(recipientBonusOverlay::capturedClockAlias(*recipe, record));
	}
	parentClockRecipe = std::move(recipe);
	parentClockTerminal = parent->parentClockTerminal ? parent->parentClockTerminal : parent->origBearer;
	parentClockVersions = parent->parentClockVersions;
	parentClockVersions.emplace_back(parent, parent->getTreeVersion());
}

bool StackWithBonuses::parentClockSourceChanged() const
{
	return std::ranges::any_of(parentClockVersions, [](const auto & source)
	{
		return source.first->getTreeVersion() != source.second;
	});
}

TConstBonusListPtr StackWithBonuses::clockRemovalRecords(TConstBonusListPtr original, const CSelector & selector) const
{
	if(!parentClockRecipe) return original;
	const CSelector clocks([&](const Bonus * record) { return recipientBonusOverlay::isCapturedClockRecord(record, *this); });
	const auto current = getUnstackedBonuses(clocks.And(selector));
	auto stable = std::make_shared<BonusList>(*original);
	stable->remove_if(clocks);
	for(const auto & record : *current) stable->push_back(record);
	return stable;
}

bool StackWithBonuses::clockRecipeImportNeutral() const
{
	const auto * node = parentClockRecipe.get();
	const auto * source = dynamic_cast<const StackWithBonuses *>(origBearer);
	while(node && source)
	{
		const auto view = source->recipientOverlayView();
		const auto & frozen = node->clocks;
		if(node->hasAliases || !frozen.added.empty() || !frozen.updated.empty() || !frozen.removed.empty()
			|| (frozen.stackedSnapshot && !frozen.stackedSnapshot->empty())
			|| (frozen.unstackedSnapshot && !frozen.unstackedSnapshot->empty())
			|| frozen.stackedSnapshot.has_value() != view.stackedSnapshot.has_value()
			|| frozen.unstackedSnapshot.has_value() != view.unstackedSnapshot.has_value()
			|| frozen.capturedGroups != view.capturedGroups) return false;
		for(const auto * records : {&view.added, &view.updated})
			for(const auto & record : *records)
				if(recipientBonusOverlay::isCapturedClockRecord(record.get(), *this)) return false;
		for(const auto & record : view.removed)
			if(recipientBonusOverlay::isCapturedClockRecord(record.get(), *this)) return false;
		if(view.stackedSnapshot)
			for(const auto & record : *view.stackedSnapshot)
				if(recipientBonusOverlay::isCapturedClockRecord(&record, *this)) return false;
		if(view.unstackedSnapshot)
			for(const auto & record : *view.unstackedSnapshot)
				if(recipientBonusOverlay::isCapturedClockRecord(record.get(), *this)) return false;
		node = node->parent.get();
		source = dynamic_cast<const StackWithBonuses *>(source->origBearer);
	}
	return !node && !source;
}

bool StackWithBonuses::hasCanonicalDamageQuerySources(const HypotheticBattle & branch) const
{
	if(!branch.hasCanonicalDamageQuerySources())
		return false;
	std::set<const StackWithBonuses *> seen;
	const StackWithBonuses * layer = this;
	while(layer)
	{
		if(typeid(*layer) != typeid(StackWithBonuses) || seen.size() >= 512 || !seen.insert(layer).second
			|| layer->ownedQuerySource || !layer->owner || !layer->usesStateEnvironment(*layer->owner->unitEnvironment())
			|| !layer->owner->hasCanonicalDamageQuerySources()
			|| layer->owner->initialExportSubject() || !layer->type || typeid(*layer->type) != typeid(CCreature)
			|| layer->hasBattleFormState() || layer->damageScoringBonuses || layer->damageScoringUnstackedBonuses
			|| !layer->originalUnit)
			return false;
		const auto & original = *layer->originalUnit;
		if((typeid(original) != typeid(CStack) && typeid(original) != typeid(StackWithBonuses))
			|| original.getBonusBearer() != layer->origBearer)
			return false;
		if(layer->originalUnit->unitId() != layer->id)
			return false;
		if(const auto * native = dynamic_cast<const CStack *>(&original))
			if(!native->usesStateEnvironment(*native) || native->hasBattleFormState()
				|| (native->getBattle() && !branch.hasCanonicalDamageQuerySources(512, native->getBattle())))
				return false;
		// The existing overlay view covers every other private record group. These
		// exact local spell values also retain nested modifier/parameter identities.
		if(layer->exactLocalSpellEffects)
		{
			std::vector<std::shared_ptr<const Bonus>> records;
			for(const auto & record : *layer->exactLocalSpellEffects)
				records.push_back(std::make_shared<Bonus>(record));
			ownedBonusPayloads::validateTypesAndTopology(records);
		}
		layer = dynamic_cast<const StackWithBonuses *>(layer->origBearer);
	}
	return true;
}
