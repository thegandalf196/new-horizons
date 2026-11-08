/*
 * SpellRecipientDamage.cpp, part of VCMI engine / New Horizons
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "ISpellMechanics.h"
#include "CSpell.h"
#include "MagicalDamageReduction.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsLightDamage.h"
#include "NewHorizonsMetamagicEffects.h"
#include "NewHorizonsWisdom.h"
#include "NewHorizonsImplosion.h"
#include "ObstacleCasterProxy.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../battle/HeroActionAllowanceState.h"
#include "../battle/NewHorizonsPassiveSpecialties.h"
#include "../battle/Unit.h"
#include "../mapObjects/CGHeroInstance.h"
#include <limits>
namespace spells
{
int64_t Mechanics::adjustRecipientDamage(const battle::Unit * target, int64_t rawDamage) const
{
	const auto * spell = dynamic_cast<const CSpell *>(getSpell());
	if(!spell || !target || rawDamage <= 0)
		return 0;
	const auto * callback = battle();
	const int holdReductionBasisPoints = callback && spell->isMagical()
		? callback->battleGetHoldTheLineMagicalReductionBasisPoints(target) : 0;
	const int perkReductionBasisPoints = callback && spell->isMagical()
		? callback->battleGetPerkMagicalReductionBasisPoints(target) : 0;
	const bool hostileRecipient = callback && caster
		&& callback->battleGetOwner(target) != caster->getCasterOwner();
	const auto penetrations = hostileRecipient
		? capturedMdrPenetrations(getCapturedMdrPenetration(), target->unitId(),
			callback ? callback->getBattle() : nullptr) : std::vector<int>{};
	const auto ordinary = spell->adjustRawDamage(caster, target, rawDamage, 0,
		holdReductionBasisPoints, 100, usesNewHorizonsMultiplicativeMDR(),
		usesNewHorizonsMagicV3(), false, perkReductionBasisPoints, penetrations);
	return callback ? newHorizonsMetamagic::spellDamage(ordinary, getMetamagicCastProfile(), *callback,
		target, callback->sideToPlayer(getEffectCasterSide()), spell->isMagical()) : ordinary;
}

int64_t BaseMechanics::adjustEffectValue(const battle::Unit * target) const
{
	return adjustEffectValueImpl(target, true);
}

int64_t BaseMechanics::adjustEffectValueBeforeExecution(const battle::Unit * target) const
{
	return adjustEffectValueImpl(target, false);
}

int64_t BaseMechanics::adjustEffectValueImpl(const battle::Unit * target, const bool applyExecution) const
{
	const auto * hero = caster ? caster->getHeroCaster() : nullptr;
	const bool hostileRecipient = cb && caster && target
		&& cb->battleGetOwner(target) != caster->getCasterOwner();
	const bool spellPenetration = mode == Mode::HERO && isNegativeSpell() && target
		&& hostileRecipient && hero
		&& newHorizonsMagic::rulesActive(hero->getMagicRules())
		&& hero->hasActivePerk(std::string(newHorizonsMagic::SPELLCRAFT_SKILL),
			"new-horizons:spellcraft.spellPenetration");
	std::vector<int> penetrations;
	if(mode == Mode::HERO && hostileRecipient)
		if(const auto * meta = getMetamagicCastProfile(); meta && meta->perks.pointPolicy)
			penetrations.push_back(newHorizonsMetamagic::effectivenessBonusPercent(meta->perks, meta->points) - meta->perks.wisdomEffectivenessBonus);
	if(mode == Mode::HERO && hostileRecipient && hero)
		if(const int wisdom = newHorizonsWisdom::archmagePenetrationPercent(hero, hero->getMagicRules(), owner->getId()))
			penetrations.push_back(wisdom);
	if(metamagicFollowup && isNegativeSpell() && target
		&& metamagicFocusedPairingEligible && target->unitId() == metamagicFirstTargetUnitId)
		penetrations.push_back(20);
	if(newHorizonsMagic::hasAnnihilatorPerk(hero, owner))
		penetrations.push_back(20);
	if(spellPenetration)
		penetrations.push_back(20);
	if(overwhelmingFormulaEligible && mode == Mode::HERO && hostileRecipient && cb && cb->getBattle())
	{
		const auto & state = cb->getBattle()->getOverwhelmingFormulaState(casterSide);
		if(overwhelmingFormulaToken != 0 ? state.canPenetrate(overwhelmingFormulaToken)
			: state.winningCastToken == 0 && state.lastCandidateCastToken != std::numeric_limits<uint64_t>::max())
			penetrations.push_back(50);
	}
	if(mode == Mode::PASSIVE && dynamic_cast<const ObstacleCasterProxy *>(caster) && hostileRecipient)
		penetrations = capturedMdrPenetrations(getCapturedMdrPenetration(), target->unitId(),
			cb ? cb->getBattle() : nullptr);
	const int holdReductionBasisPoints = cb && owner->isMagical() && target
		? cb->battleGetHoldTheLineMagicalReductionBasisPoints(target) : 0;
	const int perkReductionBasisPoints = cb && owner->isMagical() && target
		? cb->battleGetPerkMagicalReductionBasisPoints(target) : 0;
	const auto * battleState = cb ? cb->getBattle() : nullptr;
	const bool soulReaper = battleState && newHorizonsMagic::soulReaperEnabled(
		battleState->getMagicRules(), owner->getId());
	const bool magicalHpDamage = owner->isMagical() && (owner->isDamage() || soulReaper);
	const bool useIndependentMagicalDamageReduction = usesNewHorizonsMultiplicativeMDR();
	const int legacyIgnoreReduction = penetrations.empty() ? 0 : *std::max_element(penetrations.begin(), penetrations.end());
	const int finalDamageMultiplierPercent = target && battleState
		? newHorizonsMagic::holyWrathRecipientMultiplier(battleState->getMagicRules(), owner->getId(),
			target->hasBonusOfType(BonusType::UNDEAD) || target->getFactionID() == FactionID::INFERNO) : 100;
	int64_t rawDamage = getEffectValue();
	if(target && battleState)
	{
		const auto & magicRules = battleState->getMagicRules();
		if(const auto implosion = newHorizonsImplosion::rulesFor(magicRules, owner->getId()))
			rawDamage = newHorizonsImplosion::damage(*implosion, target->getAvailableHealth(),
				std::max(0, getEffectPower()), getDirectDamageSpellPowerCoefficientBasisPoints(), getEmpowerSpellBonusPercent());
		if(const auto missingHealthDamage = newHorizonsMagic::soulReaperMissingHealthDamage(
			magicRules, owner->getId(), target->getShadowGiftMaximumHealth(), target->getAvailableHealth()))
		{
			if(*missingHealthDamage > 0 && rawDamage > std::numeric_limits<int64_t>::max() - *missingHealthDamage)
				rawDamage = std::numeric_limits<int64_t>::max();
			else
				rawDamage += *missingHealthDamage;
		}
	}
	int64_t adjustedDamage = owner->adjustRawDamage(caster, target, scaleHeroLuckDamage(rawDamage),
		useIndependentMagicalDamageReduction ? 0 : legacyIgnoreReduction,
		holdReductionBasisPoints, finalDamageMultiplierPercent, useIndependentMagicalDamageReduction,
		usesNewHorizonsMagicV3(), true, perkReductionBasisPoints,
		useIndependentMagicalDamageReduction ? penetrations : std::vector<int>{});
	adjustedDamage = newHorizonsMetamagic::spellDamage(adjustedDamage, getMetamagicCastProfile(), *cb,
		target, cb->sideToPlayer(getEffectCasterSide()), magicalHpDamage);
	if(applyExecution && target && soulReaper)
		adjustedDamage = newHorizonsMagic::soulReaperDamageAfterExecution(
			target->getShadowGiftMaximumHealth(), target->getAvailableHealth(), adjustedDamage);
	return magicalHpDamage && target
		&& newHorizonsPassiveSpecialties::magicalDamageImmune(*target) ? 0 : adjustedDamage;
}

}
