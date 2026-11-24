/*
 * BattleSpellProjectionTargets.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../battle/NewHorizonsWarcast.h"

#include "BattleSpellMechanics.h"
#include "NewHorizonsMetamagicEffects.h"
#include "NewHorizonsRegeneration.h"
#include "NewHorizonsPurify.h"
#include "NewHorizonsHandOfFateTargeting.h"
#include "CSpell.h"
#include "NewHorizonsSpellAvailability.h"
#include "TargetCondition.h"
#include "../serializer/JsonDeserializer.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsSpellcraft.h"
#include "../mapObjects/CGHeroInstance.h"
#include "NewHorizonsPandemonium.h"
#include "NewHorizonsVengefulVines.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../battle/Unit.h"
#include "../networkPacks/PacksForClientBattle.h"
#include "../ScopeGuard.h"

#include <set>
#include <limits>
#include <typeinfo>

namespace spells
{
RecipientConditionResult BattleSpellMechanics::checkOverflowRecipient(const battle::Unit * unit) const
{
	const auto * condition = dynamic_cast<const TargetCondition *>(targetCondition.get());
	if(!unit || !condition || typeid(*condition) != typeid(TargetCondition)) return RecipientConditionResult::UNSUPPORTED_CONDITION;
	const auto family = newHorizonsMagic::spellVariantBase(battle()->getBattle()->getMagicRules(), owner->getId());
	return condition->checkRecipient({owner, family, battle()->battleGetSpellLevel(owner->getId()),
		usesNewHorizonsMagicV3(), true, unit->getShadowGiftMaximumHealth()}, unit);
}
bool BattleSpellMechanics::isOverflowRecipientReceptive(const battle::Unit * unit) const
{
	if(!unit) return false;
	const JsonNode empty;
	JsonDeserializer reader(nullptr, empty);
	TargetCondition ordinary;
	ordinary.serializeJson(reader, TargetConditionItemFactory::getDefault());
	return ordinary.isReceptive(this, unit) && targetCondition->isReceptive(this, unit);
}

bool BattleSpellMechanics::delegateConcentratedProjection(ServerCallback * server, const Target & target, uint64_t evaluationToken)
{
	if(!eventSnapshot || eventSnapshot->hasSpellcraftTargetSnapshot() || mode != Mode::HERO
		|| !getHeroCaster() || !getHeroCaster()->hasActivePerk(std::string(newHorizonsMagic::SPELLCRAFT_SKILL),
			std::string(newHorizonsSpellcraft::CONCENTRATION)))
		return false;
	if(server) server->allowSpellEvaluationDelegation(evaluationToken);
	auto concentrated = eventSnapshot->mechanicsForTarget(target);
	concentrated->setMetamagicOverflowProjectionOutcome(overflowProjectionOutcome);
	concentrated->castEval(server, target);
	evaluatedOverflow = concentrated->metamagicOverflowReceipt();
	vinesProjectionReceipt = concentrated->vengefulVinesProjectionReceipt();
	lifeDrainReceipt = concentrated->lifeDrainProjectionReceipt();
	evaluatedHeroRecipients.assign(concentrated->getResolvedHeroRecipients().begin(),
		concentrated->getResolvedHeroRecipients().end());
	evaluatedHeroInjuries.assign(concentrated->getResolvedHeroInjuries().begin(),
		concentrated->getResolvedHeroInjuries().end());
	return true;
}

BattleSpellMechanics::PreparedProjectionReceipts BattleSpellMechanics::prepareProjectedEffects(
	const Target & target, bool completedHeroProjection)
{
	if(newHorizonsPandemonium::enabled(*this))
		capturePandemoniumDebuffs();
	const auto spellTarget = transformSpellTarget(target);
	effectsToApply = effects->prepare(this, target, spellTarget);
	PreparedProjectionReceipts result;
	if(!completedHeroProjection)
		return result;
	if(newHorizonsVengefulVines::enabled(battle()->getBattle()->getMagicRules(), getSpellId())
		&& newHorizonsVengefulVines::footprint(target).size() == 3)
		result.vines = newHorizonsVengefulVines::captureProjection(*this, effectsToApply, resolvedConcentrationBonusPercent());
	if(newHorizonsLifeDrain::supportsProjection(*this, effectsToApply))
		result.lifeDrain = newHorizonsLifeDrain::captureProjection(*this, effectsToApply, target, resolvedConcentrationBonusPercent());
	return result;
}

battle::Units BattleSpellMechanics::transformedRecipients(const Target & target, bool prospectiveCost) const
{
	const bool previous = countingSpellTargets;
	if(prospectiveCost)
		countingSpellTargets = true;
	const auto restore = vstd::makeScopeGuard([this, previous]() { countingSpellTargets = previous; });
	const auto spellTarget = transformSpellTarget(target);
	const bool massRegeneration = newHorizonsRegenerationEffects::isMassSpell(owner, battle()->getBattle()->getMagicRules());
	battle::Units result;
	effects->forEachEffect(getEffectLevel(), [&](const effects::Effect * effect, bool &)
	{
		auto recipients = effect->transformTarget(this, target, spellTarget);
		if(prospectiveCost)
			recipients = effect->filterTarget(this, recipients);
		for(const auto & destination : recipients)
		{
			const auto * unit = destination.unitValue;
			if(unit && (!massRegeneration || newHorizonsRegenerationEffects::eligible(unit)) && !unit->isInvincible()
				)
				result.push_back(unit);
		}
	});
	return result;
}

std::vector<const CStack *> BattleSpellMechanics::getAffectedStacks(const Target & target) const
{
	std::set<const CStack *> stacks;
	for(const auto * unit : transformedRecipients(target, false))
		// Preserve historical pointer ordering and native-stack reconstruction.
		stacks.insert(battle()->battleGetStackByID(unit->unitId(), false));
	return {stacks.begin(), stacks.end()};
}

int64_t BattleSpellMechanics::getTargetAwareEffectValue(const battle::Unit * target) const
{
	if(!target || !eventSnapshot || eventSnapshot->hasSpellcraftTargetSnapshot())
		return getEffectValue();
	return eventSnapshot->mechanicsForTarget({Destination(target)})->getEffectValue();
}

int32_t Mechanics::getTargetAwareManaCost(const Target & target) const
{
	const auto * hero = getHeroCaster();
	const auto * callback = battle();
	if(!isGenuineHeroCast() || !hero || !callback
		|| getAutomaticHeroCast().kind != AutomaticHeroCastKind::NONE)
		return 0;
	int32_t result = callback->battleGetSpellCost(getSpellId().toSpell(), hero, isMassSlow() ? 3 : 1);
	if(newHorizonsMagic::magicArrowOverchargeEnabled(callback->getBattle()->getMagicRules(), getSpellId()))
		result = newHorizonsMagic::magicArrowManaCost(callback->getBattle()->getMagicRules(), getSpellId(),
			result, getOvercharge()).value_or(std::numeric_limits<int32_t>::max());
	if(!target.empty() && target.front().unitValue && result != std::numeric_limits<int32_t>::max())
		result = newHorizonsWarcast::fieldEnchanterCost(*this, target.front().unitValue, result);
	return result;
}

Target Mechanics::getHandOfFateCollateralPool(const battle::Unit * primary) const
{
	return primary && battle() ? newHorizonsHandOfFate::collateralPool(*battle(), primary->unitId()) : Target{};
}

int32_t BattleSpellMechanics::getTargetAwareManaCost(const Target & target) const
{
	return Mechanics::getTargetAwareManaCost(target);
}

size_t BattleSpellMechanics::getTargetedStackCount(const Target & target) const
{
	const bool previous = countingSpellTargets;
	countingSpellTargets = true;
	const auto restore = vstd::makeScopeGuard([this, previous]() { countingSpellTargets = previous; });
	const auto spellTarget = transformSpellTarget(target);
	std::set<uint32_t> ids;
	effects->forEachEffect(getEffectLevel(), [this, &target, &spellTarget, &ids](const effects::Effect * effect, bool &)
	{
		for(const auto & destination : effect->transformTarget(this, target, spellTarget))
			if(destination.unitValue)
				ids.insert(destination.unitValue->unitId());
	});
	// Hand of Fate targets a random additional living stack even when that
	// stack's defenses later negate its spill. Count that before any draw.
	if(owner->getJsonKey() == "new-horizons:handOfFate" && ids.size() == 1)
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit->alive() && unit->getPosition().isValid() && !unit->isTurret() && !ids.contains(unit->unitId()))
				return 2;
	return ids.size();
}

battle::Units BattleSpellMechanics::collectTargets() const
{
	// Preserve effect/hop order while deduplicating, including client reconstruction.
	battle::Units result;
	for(const auto & effect : effectsToApply)
		for(const Destination & destination : effect.second)
			if(destination.unitValue && !vstd::contains(result, destination.unitValue))
				result.push_back(destination.unitValue);
	return result;
}
}
