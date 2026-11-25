/*
 * ProjectedAdjacencyBonuses.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../lib/bonuses/FrozenScoringBonuses.h"
#include "ProjectedAdjacencyBonuses.h"
#include "StackWithBonuses.h"
#include "HypotheticBattle.h"
#include "InitialExportBattleSubject.h"
#include "ProjectedAdjacencyMemo.h"
#include "../../lib/CStack.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/NewHorizonsPassiveSpecialties.h"
#include "../../lib/battle/BattleForm.h"
#include "../../lib/bonuses/BonusGraphQuery.h"
#include "../../lib/bonuses/BonusLimitation.h"
#include "../../lib/bonuses/NativeBonusGraphAccess.h"
#include "../../lib/bonuses/ProjectedBonusRoster.h"
#include "../../lib/battle/NewHorizonsDiscipline.h"
#include "../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include <map>
#include <set>
#include <string_view>
#include <typeinfo>
#include <vector>

namespace
{
// The inherited selected list has already run normal source, limiter, updater,
// adjacency and clock evaluation. Preserve the old merge's predicate order and
// call count; allocate a replacement only when membership really changes.
TConstBonusListPtr mergeSelectedOverlay(TConstBonusListPtr inherited,
	const recipientBonusOverlay::View & overlay, const CSelector & selector, bool unstacked)
{
	TBonusListPtr changed;
	const bool hasSnapshot = unstacked ? overlay.unstackedSnapshot.has_value()
		: overlay.stackedSnapshot.has_value();
	for(size_t index = 0; index < inherited->size(); ++index)
	{
		const auto & record = (*inherited)[index];
		const bool keep = !overlay.removed.contains(record)
			&& !(hasSnapshot && recipientBonusOverlay::isCapturedProjectionEffect(record.get(), overlay.capturedGroups));
		if(!keep && !changed)
		{
			changed = std::make_shared<BonusList>();
			for(size_t previous = 0; previous < index; ++previous)
				changed->push_back((*inherited)[previous]);
		}
		else if(keep && changed)
			changed->push_back(record);
	}
	const auto writable = [&]() -> BonusList &
	{
		if(!changed)
			changed = std::make_shared<BonusList>(*inherited);
		return *changed;
	};
	if(unstacked && overlay.unstackedSnapshot)
		for(const auto & record : *overlay.unstackedSnapshot)
			if(record && selector(record.get()))
				writable().push_back(record);
	if(!unstacked && overlay.stackedSnapshot)
		for(const auto & record : *overlay.stackedSnapshot)
			if(selector(&record))
				writable().push_back(std::make_shared<Bonus>(record));
	for(const auto & record : overlay.added)
		if(selector(record.get()))
			writable().push_back(record);
	return changed ? TConstBonusListPtr(changed) : inherited;
}

constexpr std::string_view PASIS_STACKING = "new-horizons:ordinary-creature-specialty:core:pasis";

/// Native topology with ONLY the authorized source pointers. Hidden records
/// never enter collection or run an updater, even when inactive natively.
class AuthorizedGraph final : public BonusGraphQuery
{
	const std::set<const Bonus *> & authorized;
	mutable std::map<const CBonusSystemNode *, BonusList> selected;
public:
	explicit AuthorizedGraph(const std::set<const Bonus *> & records) : authorized(records) {}
	const BonusList & local(const CBonusSystemNode & node) const override
	{
		auto [entry, inserted] = selected.try_emplace(&node);
		if(inserted)
			for(const auto & record : liveBonusGraph().local(node))
				if(authorized.contains(record.get()))
					entry->second.push_back(record);
		return entry->second;
	}
	const TCNodesVector & parents(const CBonusSystemNode & node) const override { return liveBonusGraph().parents(node); }
	const TNodesVector & propagatingParents(const CBonusSystemNode & node) const override { return liveBonusGraph().propagatingParents(node); }
	const TNodesVector & children(const CBonusSystemNode & node) const override { return liveBonusGraph().children(node); }
	BonusEvaluationContext evaluation(const CBonusSystemNode & node) const override { return node; }
};
}

struct ProjectedAdjacencyQuery
{
	ProjectedAdjacencyBaseline baseline;

	TConstBonusListPtr nativeBonuses(const CSelector & selector, bool unstacked) const
	{
		return newHorizonsDiscipline::veteranCohesionBonuses(
			selectProjectedAdjacencyRecords(baseline, selector, unstacked), selector, baseline.veteranCohesionEarned);
	}
};

namespace
{
TConstBonusListPtr projectedNativeBaseline(const CStack & native,
	const BonusEvaluationContext & evaluation, const ProjectedAdjacencySources & sources,
	const TConstBonusListPtr & baseline, bool dynamicTemplates)
{
	std::set<const Bonus *> identities;
	for(const auto & record : sources.records)
		if(dynamicTemplates || !newHorizonsPassiveSpecialties::isDynamicPassiveBonus(*record))
			identities.insert(record.get());
	{
		NativeBonusGraphAccess access;
		AuthorizedGraph graph(identities);
		BonusList raw, accepted;
		collectBonusGraph(graph, native, raw);
		limitBonuses(raw, accepted, evaluation);
		auto replacement = std::make_shared<BonusList>();
		std::map<const Bonus *, size_t> remaining;
		for(const auto & record : accepted)
			++remaining[record.get()];
		for(const auto & record : *baseline)
		{
			if(dynamicTemplates && newHorizonsPassiveSpecialties::isDynamicPassiveBonus(*record)) continue;
			if(!identities.contains(record.get()))
				replacement->push_back(record);
			else if(remaining[record.get()] > 0)
			{
				replacement->push_back(record);
				--remaining[record.get()];
			}
		}
		for(const auto & record : accepted)
			if(remaining[record.get()] > 0)
			{
				replacement->push_back(record);
				--remaining[record.get()];
			}
		return replacement;
	}
}
}

std::shared_ptr<const ProjectedAdjacencySources> ProjectedAdjacencyBonuses::captureSources(
	const CBattleInfoCallback & source)
{
	if(dynamic_cast<const InitialExportBattleSubject *>(&source))
		return {}; // The existing owned graph already projects these records.
	if(const auto * parent = dynamic_cast<const HypotheticBattle *>(&source))
		return parent->initialExportSubject() ? nullptr : sourcesFor(*parent);
	auto result = std::make_shared<ProjectedAdjacencySources>();
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto policy = source.battleGetVisibleHeroPolicy(side);
		const auto * hero = policy.legacyHero();
		if(policy.kind() != VisibleHeroPolicy::Knowledge::VISIBLE_HERO || !hero || !hero->getHeroType()) continue;
		NativeBonusGraphAccess access;
		for(const auto & record : hero->getExportedBonusList())
		{
			if(!record || record->source != BonusSource::HERO_SPECIAL
				|| record->sid != BonusSourceID(hero->getHeroTypeID())) continue;
			const bool pasis = record->stacking == PASIS_STACKING
				&& record->type == BonusType::SPELL_DAMAGE_REDUCTION && record->val > 0
				&& record->limiter && !record->updater && !record->propagationUpdater;
			if(pasis || newHorizonsPassiveSpecialties::isDynamicPassiveBonus(*record))
				result->records.push_back(record);
		}
	}
	return result->records.empty() ? nullptr : result;
}

std::shared_ptr<const ProjectedAdjacencySources> ProjectedAdjacencyBonuses::sourcesFor(
	const HypotheticBattle & owner)
{
	const auto sources = owner.sourceBinding.adjacencySources(owner.subject);
	if(!sources)
		return {};
	std::set<const HypotheticBattle *> seen;
	const auto * current = &owner;
	while(current)
	{
		if(!seen.insert(current).second || seen.size() > 512
			|| current->sourceBinding.adjacencySources(current->subject) != sources)
			return {}; // An ancestor's proxy-base rebind also retires this fact.
		current = dynamic_cast<const HypotheticBattle *>(current->subject.get());
	}
	return sources;
}

const CStack * ProjectedAdjacencyBonuses::nativeSource(const StackWithBonuses & unit)
{
	const IBonusBearer * current = &unit;
	std::set<const StackWithBonuses *> seen;
	while(const auto * layer = dynamic_cast<const StackWithBonuses *>(current))
	{
		if(typeid(*layer) != typeid(StackWithBonuses) || !seen.insert(layer).second || seen.size() > 512
			|| !layer->originalUnit || layer->originalUnit->unitId() != unit.unitId()
			|| layer->originalUnit->getBonusBearer() != layer->origBearer
			|| layer->damageScoringBonuses || layer->damageScoringUnstackedBonuses
			|| layer->owner->initialExportSubject())
			return nullptr;
		current = layer->origBearer;
	}
	const auto * native = dynamic_cast<const CStack *>(current);
	return native && typeid(*native) == typeid(CStack) && native->unitId() == unit.unitId() ? native : nullptr;
}

bool ProjectedAdjacencyBonuses::canProveTypeAbsence(const StackWithBonuses & unit)
{
	if(!unit.owner || unit.owner->initialExportSubject()) return false;
	const auto * branch = unit.owner;
	for(size_t depth = 0; branch && depth < 512; ++depth)
	{
		if(branch->committedBonusOperations || !branch->subject
			|| !branch->sourceBinding.isCurrentSource(branch->subject)) return false;
		branch = dynamic_cast<const HypotheticBattle *>(branch->subject.get());
	}
	// The adjacency lane only reruns AuthorizedGraph's native raw records.
	// Their possible types (including inactive limiters) are in the native mask;
	// custom type-changing updaters make that mask unknown instead of absent.
	return !branch;
}

TConstBonusListPtr ProjectedAdjacencyBonuses::query(const StackWithBonuses & unit,
	const CSelector & selector, const std::string & cachingStr, bool unstacked)
{
	const auto sources = sourcesFor(*unit.owner);
	if((!sources && !unit.owner->committedBonusOperations)
		|| unit.damageScoringBonuses || unit.owner->initialExportSubject())
		return {};
	// Reject a rebound proxy certificate before following borrowed native
	// providers or asking the graph to execute any updater.
	unit.owner->validateCurrentBonusOperationSource();
	const auto & memo = unit.owner->sourceBinding.adjacencyMemo;
	const auto layerSource = [&memo](const StackWithBonuses & layer)
	{
		const bool scoring = layer.damageScoringBonuses.has_value() || layer.damageScoringUnstackedBonuses.has_value();
		const ProjectedAdjacencyLayerFacts facts{layer.origBearer, layer.originalUnit, scoring, layer.getTreeVersion()};
		return memo.nativeSource(layer, facts, [&layer]() { return nativeSource(layer); });
	};
	const auto * native = layerSource(unit);
	if(!native)
		return {}; // Foreign providers retain their existing unsupported semantics.
	auto units = unit.owner->battleGetUnitsIf([](const battle::Unit *) { return true; });
	const auto found = std::ranges::find_if(units, [&unit](const battle::Unit * candidate)
		{ return candidate && candidate->unitId() == unit.unitId(); });
	if(found == units.end())
		return {};
	const auto * projected = dynamic_cast<const StackWithBonuses *>(*found);
	if(*found != native && (!projected || layerSource(*projected) != native))
		return {};
	*found = &unit; // Same authorized occurrence; preserve its current scalar view.
	const std::vector<const battle::Unit *> roster(units.begin(), units.end());
	const auto & owner = *unit.owner;
	const ProjectedAdjacencyBranchFacts branch{native, sources, owner.committedBonusOperations,
		owner.getTreeVersion(), owner.getRound(), owner.getActiveStackID()};
	ProjectedAdjacencyQuery query{memo.lookup(unit, branch, roster,
		[&unit, native, &sources, &roster]() { return projectedBaseline(unit, *native, sources, roster); })};
	if(!query.baseline.records)
		return {};
	return unit.mergeBonusesImpl(selector, cachingStr, unstacked, &query);
}

ProjectedAdjacencyBaseline ProjectedAdjacencyBonuses::projectedBaseline(const StackWithBonuses & unit,
	const CStack & native, const std::shared_ptr<const ProjectedAdjacencySources> & sources,
	const std::vector<const battle::Unit *> & units)
{
	const auto policy = unit.owner->battleGetVisibleHeroPolicy(native.unitSide());
	const ProjectedBonusRoster roster(units);
	const auto * nativeBattle = native.getBattle();
	// First-party projected additions own a cold CStack bearer. Retain the real
	// subject chain for this query, rather than storing a borrowed callback.
	auto source = unit.owner->subject;
	std::set<const HypotheticBattle *> seen;
	while(!nativeBattle)
	{
		const auto * parent = dynamic_cast<const HypotheticBattle *>(source.get());
		if(!parent)
		{
			nativeBattle = dynamic_cast<const BattleInfo *>(source->getBattle());
			break;
		}
		if(!seen.insert(parent).second || seen.size() > 512)
			return {};
		source = parent->subject;
	}
	if(!nativeBattle)
		return {}; // Foreign provenance has no authorized adjacency domain.
	TConstBonusListPtr nativeList;
	{
		NativeBonusGraphAccess access;
		nativeList = native.CBonusSystemNode::getUnstackedBonuses(Selector::all);
	}
	auto baseline = nativeList;
	const auto numeric = unit.owner->currentBonusOperationBaseline(native, unit, roster, *baseline);
	if(!numeric && !sources) return {};
	if(numeric) baseline = numeric;
	if(sources)
	{
		const BonusEvaluationContext evaluation(native, unit, roster,
			{policy.legacyHero(), nativeBattle});
		baseline = projectedNativeBaseline(native, evaluation, *sources, baseline, !numeric);
	}
	return {baseline, native.veteranCohesionEarned, nativeList};
}

TConstBonusListPtr StackWithBonuses::mergeBonuses(const CSelector & selector,
	const std::string & cachingStr, bool unstacked) const
{
	if(const auto projected = ProjectedAdjacencyBonuses::query(*this, selector, cachingStr, unstacked))
		return projected;
	return mergeBonusesImpl(selector, cachingStr, unstacked, nullptr);
}

TConstBonusListPtr StackWithBonuses::mergeBonusesImpl(const CSelector & selector,
	const std::string & cachingStr, bool unstacked, const ProjectedAdjacencyQuery * adjacency) const
{
	if(damageScoringBonuses)
		return frozenScoring::query({*damageScoringBonuses, damageScoringUnstackedBonuses,
			veteranCohesionEarned}, selector, unstacked);
	const CreatureID sourceCreature = originalUnit ? originalUnit->unitType()->getId() : sourceCreatureType;
	const CreatureID effectiveCreature = battleFormCreature();
	const bool replaceNativeCreatureBonuses = effectiveCreature != sourceCreature;
	// Refresh changes duration, not value. Filtering before merging can hide the
	// existing identity and incorrectly turn a refresh into a new effect.
	const auto & mergeSelector = bonusesToUpdate.empty() ? selector : Selector::all;
	const auto & sourceSelector = mergeSelector;
	const auto originalCachingStr = bonusesToUpdate.empty() && &sourceSelector == &mergeSelector ? cachingStr : std::string();
	TConstBonusListPtr originalList;
	if(adjacency && dynamic_cast<const CStack *>(origBearer))
		originalList = adjacency->nativeBonuses(sourceSelector, unstacked);
	else if(adjacency)
		originalList = static_cast<const StackWithBonuses *>(originalUnit)->mergeBonusesImpl(
			sourceSelector, originalCachingStr, unstacked, adjacency);
	else if(originalUnit)
		originalList = originalUnit->getBonusesBeforeCreatureAbilitySuppression(
			sourceSelector, originalCachingStr, unstacked);
	else
		originalList = unstacked
			? origBearer->getUnstackedBonuses(sourceSelector)
			: origBearer->getAllBonuses(sourceSelector, originalCachingStr);
	const auto overlay = recipientOverlayView();
	if(!replaceNativeCreatureBonuses && overlay.updated.empty()
		&& !overlay.veteranCohesionEarned && hasStableInheritedBonusList())
	{
		// The old empty-overlay lane needs no scan. Edits remain query-local:
		// refreshes, forms and foreign/unstable sources keep the original merge.
		const bool empty = overlay.added.empty() && overlay.removed.empty()
			&& !overlay.stackedSnapshot && !overlay.unstackedSnapshot;
		const auto selected = empty ? originalList
			: mergeSelectedOverlay(originalList, overlay, selector, unstacked);
		return newHorizonsDiscipline::veteranCohesionBonuses(selected, selector, veteranCohesionEarned);
	}

	auto ret = std::make_shared<BonusList>();
	recipientBonusOverlay::copyInherited(*ret, *originalList, overlay, unstacked);
	if(replaceNativeCreatureBonuses)
		ret->remove_if(CSelector([this, sourceCreature](const Bonus * bonus)
		{
			return isBattleFormNativeBonus(bonus, battleFormOriginalCreature())
				|| isBattleFormNativeBonus(bonus, sourceCreature);
		}));

	recipientBonusOverlay::append(*ret, overlay, mergeSelector, unstacked);
	if(replaceNativeCreatureBonuses)
	{
		const CStack * sourceStack = nullptr;
		for(const StackWithBonuses * projected = this; projected && !sourceStack;
			projected = projected->projectedBearer.get())
			sourceStack = dynamic_cast<const CStack *>(projected->origBearer);
		if(!sourceStack && owner)
			sourceStack = dynamic_cast<const CStack *>(owner->battleGetUnitByID(id));
		if(!sourceStack)
			sourceStack = ownedBearer.get();
		const CArmedInstance * fallbackArmy = owner ? owner->getSideArmy(side) : nullptr;
		const auto effectiveBonuses = getBattleFormNativeBonuses(
			*this, sourceStack, fallbackArmy, mergeSelector, unstacked);
		for(const auto & bonus : *effectiveBonuses)
			ret->push_back(std::make_shared<Bonus>(*bonus));
		if(!unstacked)
			ret->stackBonuses();
	}
	// Recipient-local additions intentionally retain the legacy no-limiter policy.
	return recipientBonusOverlay::finish(ret, overlay, selector);
}
