/*
 * RecipientBonusOverlay.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "RecipientBonusOverlay.h"
#include "BonusList.h"
#include "../battle/NewHorizonsDiscipline.h"
#include "../battle/NewHorizonsDivineMandate.h"
#include "../battle/NewHorizonsElementalRebirth.h"
#include "../battle/NewHorizonsEnchantedCommand.h"
#include "../battle/NewHorizonsSwiftRebirth.h"
#include "../entities/creature/NewHorizonsRecruitmentTraining.h"
#include <unordered_map>

namespace
{
bool projectedEffect(const Bonus * bonus)
{
	return bonus && (bonus->source == BonusSource::SPELL_EFFECT || bonus->source == BonusSource::HERO_COMMAND
		|| recipientBonusOverlay::isSnapshotHeroSpecialEffect(bonus)
		|| newHorizonsEnchantedCommand::isMoraleBonus(bonus)
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
Layer::Layer(const View & source)
	: added(source.added), updated(source.updated), removed(source.removed),
	stackedSnapshot(source.stackedSnapshot), unstackedSnapshot(source.unstackedSnapshot),
	capturedGroups(source.capturedGroups), veteranCohesionEarned(source.veteranCohesionEarned) {}

View Layer::view() const
{
	return {added, updated, removed, stackedSnapshot, unstackedSnapshot, capturedGroups, veteranCohesionEarned};
}

bool Layer::hasEdits() const
{
	return !added.empty() || !updated.empty() || !removed.empty()
		|| stackedSnapshot.has_value() || unstackedSnapshot.has_value() || veteranCohesionEarned;
}

bool isSnapshotHeroSpecialEffect(const Bonus * bonus)
{
	if(!bonus || bonus->source != BonusSource::HERO_SPECIAL)
		return false;
	return Bonus::NTurns(bonus)
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
