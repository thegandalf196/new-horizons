/*
 * RecipientBonusOverlay.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "RecipientBonusOverlay.h"
#include "../battle/NewHorizonsStun.h"
#include "../battle/ArcaneInspiration.h"
#include <type_traits>
#include "../battle/NewHorizonsSepulchralGrip.h"
#include "BonusList.h"
#include "../battle/TimeStopState.h"
#include "../battle/Unit.h"
#include "../GameLibrary.h"
#include "../spells/CSpellHandler.h"
#include <stdexcept>
#include "../battle/NewHorizonsDiscipline.h"
#include "../battle/NewHorizonsDivineMandate.h"
#include "../battle/NewHorizonsElementalRebirth.h"
#include "../battle/NewHorizonsSwiftRebirth.h"
#include "../entities/creature/NewHorizonsRecruitmentTraining.h"
#include <unordered_map>
#include <map>

namespace
{
bool projectedEffect(const Bonus * bonus)
{
	return bonus && (bonus->source == BonusSource::SPELL_EFFECT || bonus->source == BonusSource::HERO_COMMAND
		|| recipientBonusOverlay::isSnapshotHeroSpecialEffect(bonus)
		|| arcaneInspiration::isEffect(bonus)
		|| newHorizonsDivineMandate::isSharedPurposeMoraleBonus(bonus)
		|| newHorizonsTraining::isTrainingBonus(bonus)
		|| newHorizonsSwiftRebirth::isLifecycleMarker(*bonus)
		|| newHorizonsElementalRebirth::isElementalMemoryBonus(*bonus));
}

void actualizeEffect(BonusList & target, const Bonus & ef)
{
	std::unordered_map<const Bonus *, std::shared_ptr<Bonus>> updatedCopies;
	for(auto & bonus : target)
	{
		if(bonus->source == ef.source && bonus->sid == ef.sid && bonus->type == ef.type
			&& bonus->subtype == ef.subtype && bonus->valType == ef.valType)
		{
			if(bonus->turnsRemain < ef.turnsRemain)
			{
				auto [copy, inserted] = updatedCopies.try_emplace(bonus.get());
				if(inserted)
				{
					copy->second = std::make_shared<Bonus>(*bonus);
					copy->second->turnsRemain = ef.turnsRemain;
				}
				bonus = copy->second;
			}
		}
	}
}
}

namespace recipientBonusOverlay
{
// Member container swap is allocation-free only with interchangeable default
// allocators and nonthrowing comparator exchange. Keep these concrete guards
// next to the Layer owner rather than assuming generic Layer move is noexcept.
static_assert(std::allocator_traits<Records::allocator_type>::is_always_equal::value);
static_assert(std::allocator_traits<Values::allocator_type>::is_always_equal::value);
static_assert(std::allocator_traits<decltype(Layer::removed)::allocator_type>::is_always_equal::value);
static_assert(std::allocator_traits<Groups::allocator_type>::is_always_equal::value);
static_assert(std::is_nothrow_swappable_v<decltype(Layer::removed)::key_compare>);
static_assert(std::is_nothrow_swappable_v<Groups::key_compare>);
static_assert(std::is_nothrow_swappable_v<Layer>);

void swap(Layer & left, Layer & right) noexcept(noexcept(left.swap(right)))
{
	left.swap(right);
}

Layer::Layer(const View & source)
	: added(source.added), updated(source.updated), removed(source.removed),
	stackedSnapshot(source.stackedSnapshot), unstackedSnapshot(source.unstackedSnapshot),
	capturedGroups(source.capturedGroups), veteranCohesionEarned(source.veteranCohesionEarned), clockParents(source.clockParents) {}

View Layer::view() const
{
	return {added, updated, removed, stackedSnapshot, unstackedSnapshot, capturedGroups, veteranCohesionEarned, clockParents};
}

bool Layer::hasEdits() const
{
	return !added.empty() || !updated.empty() || !removed.empty()
		|| stackedSnapshot.has_value() || unstackedSnapshot.has_value() || veteranCohesionEarned || clockParents;
}

bool isSnapshotHeroSpecialEffect(const Bonus * bonus)
{
	if(newHorizonsStun::isMarker(bonus)) return true;
	if(arcaneInspiration::isEffect(bonus) || (bonus && bonus->isExtendedOrdinarySpecialtyEffect()))
		return true;
	if(!bonus || bonus->source != BonusSource::HERO_SPECIAL)
		return false;
	return newHorizonsSepulchralGrip::isEffect(bonus) || Bonus::NTurns(bonus)
		// Identified activation-expiry hero effects must belong to the branch,
		// even when an ancestor expires its own copy before a lazy read.
		|| (bonus->duration == BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION && bonus->hasStatusMetadata()
			&& std::ranges::find(bonus->statusTags, BonusStatusTag::NON_TRANSFERABLE) != bonus->statusTags.end())
		|| (bonus->duration == BonusDuration::STACK_ACTIVATION
			&& bonus->type == BonusType::PERCENTAGE_DAMAGE_BOOST
			&& bonus->stacking == "new-horizons:discipline.inspirationalLeader"
			&& (bonus->subtype == BonusSubtypeID(BonusCustomSubtype::damageTypeMelee)
				|| bonus->subtype == BonusSubtypeID(BonusCustomSubtype::damageTypeRanged)));
}

bool isInPhysicalAfflictionGroup(const Bonus * bonus, const Groups & history)
{
	return bonus && history.contains({bonus->source, bonus->sid});
}

bool isCapturedProjectionEffect(const Bonus * bonus, const Groups & history)
{
	return projectedEffect(bonus) || isInPhysicalAfflictionGroup(bonus, history);
}

void copyInherited(BonusList & target, const BonusList & inherited, const View & layer, bool unstacked)
{
	const bool hasProjectionSnapshot = unstacked
		? layer.unstackedSnapshot.has_value() : layer.stackedSnapshot.has_value();
	vstd::copy_if(inherited, std::back_inserter(target), [&](const std::shared_ptr<Bonus> & b)
	{
		return !vstd::contains(layer.removed, b)
			&& !(hasProjectionSnapshot && isCapturedProjectionEffect(b.get(), layer.capturedGroups));
	});
}

void append(BonusList & target, const View & layer, const CSelector & mergeSelector, bool unstacked)
{
	if(unstacked)
	{
		if(layer.unstackedSnapshot)
			for(const auto & bonus : *layer.unstackedSnapshot)
				if(bonus && mergeSelector(bonus.get()))
					target.push_back(bonus);
	}
	else if(layer.stackedSnapshot)
		for(const auto & bonus : *layer.stackedSnapshot)
			if(mergeSelector(&bonus))
				target.push_back(std::make_shared<Bonus>(bonus));

	for(const auto & record : layer.updated)
	{
		const Bonus & bonus = *record;
		if(mergeSelector(&bonus))
		{
			const bool markedAfflictionGroup = isInPhysicalAfflictionGroup(&bonus, layer.capturedGroups);
			const bool refreshableSource = bonus.source == BonusSource::SPELL_EFFECT || markedAfflictionGroup;
			if(refreshableSource && target.getFirst(Selector::source(bonus.source, bonus.sid)
				.And(Selector::typeSubtypeValueType(bonus.type, bonus.subtype, bonus.valType))))
			{
				actualizeEffect(target, bonus);
			}
			else
			{
				target.push_back(record);
			}
		}
	}

	for(const auto & bonus : layer.added)
	{
		if(mergeSelector(bonus.get()))
			target.push_back(bonus);
	}
}

TConstBonusListPtr finish(TBonusListPtr target, const View & layer, const CSelector & selector)
{
	if(!layer.updated.empty())
		target->remove_if([&](const Bonus * bonus) { return !selector(bonus); });
	return newHorizonsDiscipline::veteranCohesionBonuses(target, selector, layer.veteranCohesionEarned);
}
}

namespace recipientBonusOverlay
{
namespace
{
std::optional<bool> clockCaptureMembership(const Bonus * record, const battle::Unit & subject)
{
	if(!record) return false;
	if(record->type == BonusType::TIME_STOP) return true;
	if((record->type != BonusType::NONE && record->type != BonusType::NOT_ACTIVE && record->type != BonusType::INVINCIBLE)
		|| record->source != BonusSource::SPELL_EFFECT || !record->sid.as<SpellID>().hasValue()) return false;
	const auto id = record->sid.as<SpellID>();
	if(const auto captured = subject.legacyTimeStopSpellMatch(id)) return captured;
	// UNKNOWN is not a clock-state answer. Leave this original record in the
	// ordinary query so the canonical legacy lookup retains its previous error.
	if(!LIBRARY || !LIBRARY->spellh || id.getNum() < 0
		|| static_cast<size_t>(id.getNum()) >= LIBRARY->spellh->size()) return std::nullopt;
	return timeStopState::isStateBonus(*record, subject);
}
}

bool isCapturedClockRecord(const Bonus * record, const battle::Unit & subject)
{
	const auto membership = clockCaptureMembership(record, subject);
	return membership.has_value() && *membership;
}

std::shared_ptr<ParentClockRecipe> captureParentClocks(const View & parent, const battle::Unit & subject)
{
	const Records noRecords;
	const std::set<std::shared_ptr<Bonus>> noRemovals;
	const std::optional<Values> noValues;
	const std::optional<Records> noRawValues;
	const bool notVeteran = false;
	const View empty{noRecords, noRecords, noRemovals, noValues, noRawValues,
		parent.capturedGroups, notVeteran, nullptr};
	auto result = std::make_shared<ParentClockRecipe>(Layer(empty));
	result->parent = parent.clockParents;
	const auto copy = [&](const std::shared_ptr<Bonus> & record, bool removal)
	{
		std::set<const ParentClockRecipe *> seen;
		for(const ParentClockRecipe * node = result.get(); node; node = node->parent.get())
		{
			if(!seen.insert(node).second) throw std::invalid_argument("Cyclic parent clock recipe");
			for(const auto & [source, captured] : node->aliases)
				if(source.get() == record.get() || captured.get() == record.get()) return captured;
		}
		if(removal) return record;
		auto captured = std::make_shared<Bonus>(*record);
		result->aliases.emplace_back(record, captured);
		return captured;
	};
	for(const auto & record : parent.added)
		if(isCapturedClockRecord(record.get(), subject)) result->clocks.added.push_back(copy(record, false));
	for(const auto & record : parent.updated)
		if(isCapturedClockRecord(record.get(), subject)) result->clocks.updated.push_back(copy(record, false));
	for(const auto & record : parent.removed)
		if(isCapturedClockRecord(record.get(), subject)) result->clocks.removed.insert(copy(record, true));
	if(parent.stackedSnapshot)
	{
		result->clocks.stackedSnapshot.emplace(); // Preserve prior selective policy, including empty presence.
		for(const auto & record : *parent.stackedSnapshot)
			if(isCapturedClockRecord(&record, subject)) result->clocks.stackedSnapshot->push_back(record);
	}
	if(parent.unstackedSnapshot)
	{
		result->clocks.unstackedSnapshot.emplace();
		for(const auto & record : *parent.unstackedSnapshot)
			if(isCapturedClockRecord(record.get(), subject)) result->clocks.unstackedSnapshot->push_back(copy(record, false));
	}
	result->hasAliases = !result->aliases.empty() || (result->parent && result->parent->hasAliases);
	return result;
}

namespace
{
Records::const_iterator witnessOccurrence(const Records & records, const std::shared_ptr<Bonus> & record, size_t occurrence)
{
	for(auto at = records.begin(); at != records.end(); ++at)
		if(*at == record && occurrence-- == 0) return at;
	return records.end();
}

void insertClockAtWitness(Records & result, const std::shared_ptr<Bonus> & record, const Records & witness, size_t occurrence)
{
	auto slot = witness.begin();
	for(; slot != witness.end(); ++slot)
		if(*slot == record && occurrence-- == 0) break;
	if(slot == witness.end()) { result.push_back(record); return; }
	for(auto next = slot + 1; next != witness.end(); ++next)
	{
		const auto ordinal = static_cast<size_t>(std::count(witness.begin(), next, *next));
		const auto anchor = witnessOccurrence(result, *next, ordinal);
		if(anchor != result.end()) { result.insert(anchor, record); return; }
	}
	for(auto before = slot; before != witness.begin();)
	{
		--before;
		const auto ordinal = static_cast<size_t>(std::count(witness.begin(), before, *before));
		const auto anchor = witnessOccurrence(result, *before, ordinal);
		if(anchor != result.end()) { result.insert(anchor + 1, record); return; }
	}
	const auto index = std::min(static_cast<size_t>(slot - witness.begin()), result.size());
	result.insert(result.begin() + index, record);
}
}

void replaceInheritedClocks(BonusList & inherited, const BonusList & terminal,
	const ParentClockRecipe & recipe, const battle::Unit & subject, const CSelector & selector, bool unstacked)
{
	const CSelector clocks([&](const Bonus * record) { return isCapturedClockRecord(record, subject); });
	auto current = std::make_shared<BonusList>();
	vstd::copy_if(terminal, std::back_inserter(*current), [&](const auto & record) { return clocks(record.get()); });
	std::vector<const ParentClockRecipe *> layers;
	std::set<const ParentClockRecipe *> seen;
	for(const auto * node = &recipe; node; node = node->parent.get())
	{
		if(!seen.insert(node).second) throw std::invalid_argument("Cyclic parent clock recipe");
		layers.push_back(node);
	}
	for(auto at = layers.rbegin(); at != layers.rend(); ++at)
	{
		auto next = std::make_shared<BonusList>();
		const auto view = (*at)->clocks.view();
		copyInherited(*next, *current, view, unstacked);
		append(*next, view, clocks, unstacked);
		current = std::move(next);
	}
	current->remove_if([&](const Bonus * record) { return !selector(record); });
	Records result;
	std::map<const Bonus *, size_t> remaining;
	for(const auto & record : *current) ++remaining[record.get()];
	const auto frozen = [&](const std::shared_ptr<Bonus> & record)
	{
		for(const auto * node = &recipe; node; node = node->parent.get())
			for(const auto & alias : node->aliases)
				if(alias.second.get() == record.get()) return true;
		return false;
	};
	for(const auto & record : inherited)
	{
		if(!clocks(record.get())) result.push_back(record);
		else
		{
			const auto stable = capturedClockAlias(recipe, record);
			if(!frozen(stable) && remaining[stable.get()] > 0)
			{
				result.push_back(stable);
				--remaining[stable.get()];
			}
		}
	}
	std::map<const Bonus *, size_t> occurrences;
	const Records terminalOrder(terminal.begin(), terminal.end());
	for(const auto & record : *current)
		if(remaining[record.get()] > 0)
		{
			insertClockAtWitness(result, record, frozen(record) ? recipe.orderWitness : terminalOrder, occurrences[record.get()]++);
			--remaining[record.get()];
		}
	inherited.clear();
	for(const auto & record : result) inherited.push_back(record);

}
}

namespace recipientBonusOverlay
{
std::shared_ptr<Bonus> capturedClockAlias(const ParentClockRecipe & recipe, const std::shared_ptr<Bonus> & record)
{
	std::set<const ParentClockRecipe *> seen;
	for(const auto * node = &recipe; node; node = node->parent.get())
	{
		if(!seen.insert(node).second) throw std::invalid_argument("Cyclic parent clock recipe");
		for(const auto & [source, captured] : node->aliases)
			if(source.get() == record.get() || captured.get() == record.get()) return captured;
	}
	return record;
}
}
