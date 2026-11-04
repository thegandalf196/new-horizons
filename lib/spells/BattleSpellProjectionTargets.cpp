/*
 * BattleSpellProjectionTargets.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"

#include "BattleSpellMechanics.h"
#include "NewHorizonsMetamagicEffects.h"
#include "NewHorizonsRegeneration.h"
#include "NewHorizonsPurify.h"
#include "NewHorizonsHandOfFateTargeting.h"
#include "CSpell.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsSpellcraft.h"
#include "../mapObjects/CGHeroInstance.h"
#include "NewHorizonsPandemonium.h"
#include "NewHorizonsVengefulVines.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../battle/Unit.h"
#include "../ScopeGuard.h"

#include <set>

namespace spells
{
bool BattleSpellMechanics::delegateConcentratedProjection(ServerCallback * server, const Target & target)
{
	if(!eventSnapshot || eventSnapshot->hasSpellcraftTargetSnapshot() || mode != Mode::HERO
		|| !getHeroCaster() || !getHeroCaster()->hasActivePerk(std::string(newHorizonsMagic::SPELLCRAFT_SKILL),
			std::string(newHorizonsSpellcraft::CONCENTRATION)))
		return false;
	auto concentrated = eventSnapshot->mechanicsForTarget(target);
	concentrated->castEval(server, target);
	vinesProjectionReceipt = concentrated->vengefulVinesProjectionReceipt();
	lifeDrainReceipt = concentrated->lifeDrainProjectionReceipt();
	return true;
}

BattleSpellMechanics::PreparedProjectionReceipts BattleSpellMechanics::prepareProjectedEffects(
	const Target & target, bool completedHeroProjection)
{
	if(newHorizonsPandemonium::enabled(*this))
		capturePandemoniumDebuffs();
	const auto spellTarget = transformSpellTarget(target);
	effectsToApply = effects->prepare(this, target, spellTarget);
	filterSpellLockedEffects(target);
	PreparedProjectionReceipts result;
	if(!completedHeroProjection || isCounterspellNegated())
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
				&& (!isMagicalEffect() || !isSpellLocked(unit)))
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

int32_t Mechanics::getTargetAwareManaCost(const Target &) const
{
	const auto * hero = getHeroCaster();
	const auto * callback = battle();
	if(!isGenuineHeroCast() || !hero || !callback
		|| getAutomaticHeroCast().kind != AutomaticHeroCastKind::NONE)
		return 0;
	int32_t result = callback->battleGetSpellCost(getSpellId().toSpell(), hero, isMassSlow() ? 3 : 1,
		isMetamagicFollowup());
	if(newHorizonsMagic::magicArrowOverchargeEnabled(callback->getBattle()->getMagicRules(), getSpellId()))
		result += getOvercharge();
	return result;
}

Target Mechanics::getHandOfFateCollateralPool(const battle::Unit * primary) const
{
	return primary && battle() ? newHorizonsHandOfFate::collateralPool(*battle(), primary->unitId()) : Target{};
}

int32_t BattleSpellMechanics::getTargetAwareManaCost(const Target & target) const
{
	const int32_t ordinary = Mechanics::getTargetAwareManaCost(target);
	const auto * cast = getMetamagicCastProfile();
	if(!cast || ordinary == 0)
		return ordinary;
	if(cast->perks.pointPolicy)
		return newHorizonsMetamagic::singularityManaCost(ordinary, cast->perks, cast->singularityReached);
	if(!cast->perks.manaAttunement)
		return ordinary;
	if(newHorizonsPurify::enabled(battle()->getBattle()->getMagicRules(), getSpellId()) && !target.empty())
	{
		const auto recipients = newHorizonsPurify::prospectiveRecipients(*battle(), casterSide,
			target.front().hexValue, getHeroCaster()->getPrimSkillLevel(PrimarySkill::SPELL_POWER),
			newHorizonsPurify::hasPurifierPerk(getHeroCaster()), getPurifyChoices());
		return newHorizonsMetamagic::manaCostForRecipients(ordinary, cast, *battle(), recipients);
	}
	std::vector<uint32_t> ids;
	for(const auto * unit : transformedRecipients(target, true))
		ids.push_back(unit->unitId());
	const auto counts = newHorizonsMetamagic::manaTargetCounts(cast, *battle(), ids);
	if(owner->getJsonKey() != "new-horizons:handOfFate" || counts.affected != 1)
		return newHorizonsMetamagic::adjustedManaCost(ordinary, cast->perks, counts.qualifying, counts.affected);

	const auto * primary = battle()->battleGetUnitByID(ids.front());
	const auto pool = getHandOfFateCollateralPool(primary);
	if(pool.empty())
		return newHorizonsMetamagic::adjustedManaCost(ordinary, cast->perks, counts.qualifying, 1);
	const bool fateDealer = usesNewHorizonsMagicV3()
		&& getHeroCaster()->hasActivePerk("new-horizons:chaosMagic", "new-horizons:chaosMagic.fateDealer");
	uint32_t hostile = 0;
	for(const auto & destination : pool)
		if(battle()->battleGetOwner(destination.unitValue) != getCasterColor())
			++hostile;
	const auto size = static_cast<uint32_t>(pool.size());
	const uint64_t denominator = newHorizonsHandOfFate::selectionDenominator(size, fateDealer);
	uint64_t legalWeight = 0;
	uint64_t qualifiedLegalWeight = 0;
	for(const auto & destination : pool)
	{
		const auto * unit = destination.unitValue;
		if(unit && unit->isValidTarget(false) && !unit->isInvincible() && isReceptive(unit)
			&& (!isMagicalEffect() || !isSpellLocked(unit)))
		{
			const auto weight = newHorizonsHandOfFate::selectionWeight(size, hostile,
				battle()->battleGetOwner(unit) != getCasterColor(), fateDealer);
			legalWeight += weight;
			if(unit->metamagicMarks(cast->heroSide) >= newHorizonsMetamagic::QUALIFYING_MARKS)
				qualifiedLegalWeight += weight;
		}
	}
	const auto [qualifying, affected] = newHorizonsHandOfFate::expectedManaFraction(
		counts.qualifying != 0, denominator, legalWeight, qualifiedLegalWeight);
	return newHorizonsMetamagic::adjustedManaCost(ordinary, cast->perks, qualifying, affected);
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
