/*
 * BattleEffectExchange.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "BattleEffectExchange.h"
#include "CUnitState.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusParameters.h"
#include "../bonuses/Propagators.h"
#include "../bonuses/Updaters.h"
#include "../serializer/CMemorySerializer.h"
#include "../spells/NewHorizonsMagic.h"
#include <limits>

namespace battle
{
namespace
{
class ReplacementBonusView final : public IBonusBearer
{
public:
	BonusList bonuses;
	int32_t version = 1;
	TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string & = {}) const override
	{
		auto result = std::make_shared<BonusList>();
		bonuses.getBonuses(*result, selector);
		result->stackBonuses();
		return result;
	}
	TConstBonusListPtr getUnstackedBonuses(const CSelector & selector) const override
	{
		auto result = std::make_shared<BonusList>();
		bonuses.getBonuses(*result, selector);
		return result;
	}
	int32_t getTreeVersion() const override { return version; }
};

std::vector<std::byte> exactBonusBytes(const Bonus & bonus)
{
	// The complete typed serialization includes fields omitted by config JSON
	// (icon, dynamic owner, propagation updater and exact MetaString components).
	CMemorySerializer memory;
	auto copy = bonus;
	memory.oser & copy;
	return memory.extractBuffer();
}

void validateSnapshot(const BattleEffectSnapshot & snapshot, bool preserveLegacyPending = false)
{
	snapshot.sidecars.validate();
	if(!snapshot.recipientHealth.isStruct() || snapshot.capacityHealthReferenceMax < 0)
		throw std::runtime_error("Invalid recipient health guard in spell-effect exchange");
	size_t pendingMarkers = 0;
	for(const auto & effect : snapshot.effects)
	{
		if(effect.source != BonusSource::SPELL_EFFECT || !effect.hasValidStatusMetadata())
			throw std::runtime_error("Spell-effect exchange requires valid local SPELL_EFFECT bonuses");
		effect.validateConfusionPendingMarker();
		if(effect.type == BonusType::CONFUSION_PENDING)
		{
			++pendingMarkers;
			if(!snapshot.sidecars.confusionPending
				|| effect.spellCasterOwner != snapshot.sidecars.confusionCaster
				|| (effect.val == 2) != snapshot.sidecars.confusionConfounder)
				throw std::runtime_error("Confusion marker and exchange pending sidecars disagree");
		}
	}
	if(pendingMarkers > 1 || (snapshot.sidecars.confusionPending && pendingMarkers == 0 && !preserveLegacyPending))
		throw std::runtime_error("Spell-effect exchange requires one coherent Confusion pending marker");
}
}

void BattleEffectSidecars::validate() const
{
	if(regenerationRateMillionths < 0 || regenerationRateMillionths > newHorizonsMagic::REGENERATION_MAX_RATE_MILLIONTHS
		|| regenerationPendingMicroHealth < 0 || guardianSpiritHitPoints < 0
		|| guardianSpiritRoundsRemaining < 0 || capacityRegenerationRemainderTenths < 0
		|| capacityRegenerationRemainderTenths >= 10
		|| ((guardianSpiritHitPoints == 0) != (guardianSpiritRoundsRemaining == 0)))
		throw std::runtime_error("Invalid spell-effect exchange sidecars");
	ConfusionState pending;
	pending.pending = confusionPending;
	pending.pendingCaster = confusionCaster;
	pending.pendingConfounder = confusionConfounder;
	pending.validate();
}

void BattleEffectExchange::validateShape() const
{
	if(endpoints[0].id == endpoints[1].id)
		throw std::runtime_error("Spell-effect exchange requires two distinct unit IDs");
	for(const auto & endpoint : endpoints)
	{
		if(endpoint.id > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()))
			throw std::runtime_error("Spell-effect exchange unit ID exceeds the supported battle range");
		// Pre-marker saved pending activations remain valid when untouched. They
		// cannot be newly manufactured or transferred without the source marker.
		validateSnapshot(endpoint.expected, true);
		const bool unchangedLegacyPending = endpoint.expected.sidecars.confusionPending
			&& endpoint.replacement.sidecars.confusionPending
			&& endpoint.expected.sidecars.confusionCaster == endpoint.replacement.sidecars.confusionCaster
			&& endpoint.expected.sidecars.confusionConfounder == endpoint.replacement.sidecars.confusionConfounder
			&& std::ranges::none_of(endpoint.expected.effects,
				[](const Bonus & effect) { return effect.type == BonusType::CONFUSION_PENDING; });
		validateSnapshot(endpoint.replacement, unchangedLegacyPending);
		if(endpoint.expected.recipientHealth != endpoint.replacement.recipientHealth
			|| endpoint.expected.capacityHealthReferenceMax != endpoint.replacement.capacityHealthReferenceMax)
			throw std::runtime_error("Spell-effect exchange cannot replace recipient HP or capacity baseline");
		std::vector<bool> matched(endpoint.replacement.effects.size(), false);
		for(const auto & before : endpoint.expected.effects)
			if(before.propagator)
			{
				size_t index = 0;
				while(index < matched.size() && (matched[index]
					|| !exactBattleEffectEqual(before, endpoint.replacement.effects[index])))
					++index;
				if(index == matched.size())
					throw std::runtime_error("Spell-effect exchange cannot change local propagated effects");
				matched[index] = true;
			}
		for(size_t index = 0; index < matched.size(); ++index)
			if(endpoint.replacement.effects[index].propagator && !matched[index])
				throw std::runtime_error("Spell-effect exchange cannot add local propagated effects");
	}
}

bool exactBattleEffectEqual(const Bonus & first, const Bonus & second)
{
	return first.bonusOwner == second.bonusOwner && exactBonusBytes(first) == exactBonusBytes(second);
}

bool exactBattleEffectsEqual(const std::vector<Bonus> & first, const std::vector<Bonus> & second)
{
	if(first.size() != second.size())
		return false;
	for(size_t index = 0; index < first.size(); ++index)
		if(!exactBattleEffectEqual(first[index], second[index]))
			return false;
	return true;
}

bool exactBattleEffectSnapshotEqual(const BattleEffectSnapshot & first, const BattleEffectSnapshot & second)
{
	return first.sidecars == second.sidecars
		&& first.recipientHealth == second.recipientHealth
		&& first.capacityHealthReferenceMax == second.capacityHealthReferenceMax
		&& exactBattleEffectsEqual(first.effects, second.effects);
}

BattleEffectSidecars captureBattleEffectSidecars(const CUnitState & unit)
{
	BattleEffectSidecars result;
	result.regenerationRateMillionths = unit.regenerationRateMillionths;
	result.regenerationPendingMicroHealth = unit.regenerationPendingMicroHealth;
	result.guardianSpiritHitPoints = unit.guardianSpiritHitPoints;
	result.guardianSpiritRoundsRemaining = unit.guardianSpiritRoundsRemaining;
	result.capacityRegenerationRemainderTenths = unit.capacityRegenerationRemainderTenths;
	result.confusionPending = unit.confusionState.pending;
	result.confusionCaster = unit.confusionState.pendingCaster;
	result.confusionConfounder = unit.confusionState.pendingConfounder;
	return result;
}

void commitBattleEffectSidecars(CUnitState & unit, const BattleEffectSidecars & sidecars) noexcept
{
	unit.regenerationRateMillionths = sidecars.regenerationRateMillionths;
	unit.regenerationPendingMicroHealth = sidecars.regenerationPendingMicroHealth;
	unit.guardianSpiritHitPoints = sidecars.guardianSpiritHitPoints;
	unit.guardianSpiritRoundsRemaining = sidecars.guardianSpiritRoundsRemaining;
	unit.capacityRegenerationRemainderTenths = sidecars.capacityRegenerationRemainderTenths;
	unit.confusionState.pending = sidecars.confusionPending;
	unit.confusionState.pendingCaster = sidecars.confusionCaster;
	unit.confusionState.pendingConfounder = sidecars.confusionConfounder;
}

PreparedBattleEffectHealth prepareBattleEffectHealth(const CUnitState & unit,
	const BattleEffectSnapshot & expected, const BattleEffectSnapshot & replacement)
{
	auto view = std::make_shared<ReplacementBonusView>();
	const auto original = unit.getBonusesBeforeCreatureAbilitySuppression(Selector::all, {}, true);
	for(const auto & bonus : *original)
		view->bonuses.push_back(bonus);
	auto projected = std::make_shared<CUnitStateDetached>(&unit, view.get());
	*projected = unit;
	std::vector<Bonus> originalCapacity, replacementCapacity;
	for(const auto & bonus : expected.effects)
		if(bonus.type == BonusType::STACK_HEALTH)
			originalCapacity.push_back(bonus);
	for(const auto & bonus : replacement.effects)
		if(bonus.type == BonusType::STACK_HEALTH)
			replacementCapacity.push_back(bonus);
	const bool capacityChanged = !exactBattleEffectsEqual(originalCapacity, replacementCapacity);
	if(capacityChanged)
		// Capture the recipient's existing creature HP against its OLD capacity,
		// before changing the view. Otherwise rear survivors would silently heal.
		projected->preserveCreatureHealthOnCapacityIncrease();
	std::vector<bool> retained(replacement.effects.size(), false);
	for(const auto & before : expected.effects)
	{
		size_t identical = 0;
		while(identical < replacement.effects.size()
			&& (retained[identical] || !exactBattleEffectEqual(before, replacement.effects[identical])))
			++identical;
		if(identical < replacement.effects.size())
		{
			retained[identical] = true;
			continue;
		}
		if(before.type != BonusType::STACK_HEALTH)
			continue;
		// Changed capacity effects must be directly representable. Retained
		// inherited/updater effects stay in the actual raw view, unchanged.
		if(before.updater || before.propagationUpdater || before.propagator || before.limiter)
			throw std::runtime_error("Cannot project exchanged health capacity with updater, limiter or propagation");
		const auto match = std::find_if(view->bonuses.begin(), view->bonuses.end(), [&](const auto & bonus)
		{
			return exactBattleEffectEqual(*bonus, before);
		});
		if(match == view->bonuses.end())
			throw std::runtime_error("Cannot locate source-local capacity effect in recipient bonus view");
		view->bonuses.erase(static_cast<int>(std::distance(view->bonuses.begin(), match)));
	}
	for(size_t index = 0; index < replacement.effects.size(); ++index)
	{
		const auto & after = replacement.effects[index];
		if(retained[index] || after.type != BonusType::STACK_HEALTH)
			continue;
		if(after.updater || after.propagationUpdater || after.propagator || after.limiter)
			throw std::runtime_error("Cannot project exchanged health capacity with updater, limiter or propagation");
		view->bonuses.push_back(std::make_shared<Bonus>(after));
	}
	++view->version;
	if(projected->health.isCapacityHealthTracking())
		projected->normalizeCapacityHealth();
	const auto hydra = BonusSourceID(SpellID(SpellID::decode("new-horizons:hydrasVitality")));
	std::vector<Bonus> originalHydraRegeneration, replacementHydraRegeneration;
	for(const auto & bonus : expected.effects)
		if(bonus.sid == hydra && bonus.type == BonusType::HP_REGENERATION)
			originalHydraRegeneration.push_back(bonus);
	for(const auto & bonus : replacement.effects)
		if(bonus.sid == hydra && bonus.type == BonusType::HP_REGENERATION)
			replacementHydraRegeneration.push_back(bonus);
	const auto retainsHydra = std::ranges::any_of(replacement.effects, [&hydra](const Bonus & bonus)
	{
		return bonus.sid == hydra && (bonus.type == BonusType::STACK_HEALTH || bonus.type == BonusType::HP_REGENERATION);
	});
	if(!retainsHydra && (capacityChanged
		|| !exactBattleEffectsEqual(originalHydraRegeneration, replacementHydraRegeneration)))
		projected->clearCapacityHealthReference();
	return {view, projected};
}
}
