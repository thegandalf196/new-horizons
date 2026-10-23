/*
 * NewHorizonsFirstAid.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "CBattleInfoCallback.h"
#include "IBattleInfoCallback.h"
#include "CUnitState.h"
#include "../CStack.h"
#include "../entities/hero/NewHorizonsHeroRules.h"
#include "../entities/hero/NewHorizonsCapabilityRules.h"
#include "../mapObjects/CGHeroInstance.h"
namespace
{
int64_t firstAidHealingBase(const CGHeroInstance * owner)
{
	if(!owner)
		return 0;
	if(const auto siege = owner->getSiegeCapabilities();
		siege && owner->getCapabilityRules()["rulesetVersion"].Integer() >= 3)
		return siege->firstAidHealing;
	return owner->valOfBonuses(BonusType::MANUAL_CONTROL,
		BonusSubtypeID(CreatureID(CreatureID::FIRST_AID_TENT)));
}

int64_t scaledFirstAidOutput(int64_t base, int32_t outputPercent)
{
	if(base <= 0 || outputPercent <= 0)
		return 0;
	return (base / 100) * outputPercent + ((base % 100) * outputPercent) / 100;
}
}

int64_t CBattleInfoCallback::battleGetFirstAidHealingOutput(const battle::Unit * healer) const
{
	if(!healer || !healer->isFirstAidTent())
		return 0;
	return scaledFirstAidOutput(firstAidHealingBase(battleGetOwnerHero(healer)),
		battleGetActivationOutputPercent(healer));
}

int64_t CBattleInfoCallback::battleGetFirstAidCreatureHealingOutput(
	const battle::Unit * healer, const battle::Unit * target) const
{
	const auto ordinary = battleGetFirstAidHealingOutput(healer);
	if(!healer || !target || !target->alive() || !target->isValidTarget()
		|| target->isTurret() || target->hasBonusOfType(BonusType::SIEGE_WEAPON)
		|| !battleMatchActionController(healer, target, true))
		return ordinary;
	const auto * hero = battleGetOwnerHero(healer);
	const auto physician = newHorizonsHeroes::fieldPhysicianHealing(ordinary, hero);
	if(target->getFirstHPleft() >= target->getMaxHealth())
		return physician;
	const auto specialty = hero
		? hero->getDruidSpecialtyRules(newHorizonsHeroes::DruidSpecialtyKind::RESTORATIVE_SORCERESS) : std::nullopt;
	if(!specialty)
		return physician;
	return physician + newHorizonsHeroes::druidSpecialtyExtra(ordinary,
		newHorizonsHeroes::druidSpecialtyPercent(hero->level, specialty->restorativeSorceress));
}
int64_t CBattleInfoCallback::battleGetBattlefieldMedicRestorationBudget(
	const battle::Unit * healer, const battle::Unit * target) const
{
	if(!getBattle() || !healer || !healer->isFirstAidTent() || !healer->alive()
		|| !target || !target->alive() || !target->isValidTarget()
		|| target->hasBonusOfType(BonusType::SIEGE_WEAPON) || target->isTurret()
		|| target->isSummoned() || target->isClone() || target->getPhantomInitialIntegrity() > 0
		|| static_cast<int64_t>(target->unitBaseAmount()) - target->getCount() <= target->getUnusableRemains()
		|| !battleMatchActionController(healer, target, true))
		return 0;

	const auto * hero = battleGetOwnerHero(healer);
	if(!hero || hero->getCapabilityRules()["rulesetVersion"].Integer() < 3
		|| !hero->hasActivePerk("new-horizons:warMachines", "new-horizons:warMachines.battlefieldMedic"))
		return 0;

	// Calculated output, not the HP actually healed or the unused healing remainder.
	return newHorizonsHeroes::fieldPhysicianHealing(battleGetFirstAidHealingOutput(healer), hero) / 2;
}

FirstAidHealingPreview CBattleInfoCallback::battleGetFirstAidHealingPreview(
	const battle::Unit * healer, const battle::Unit * target) const
{
	FirstAidHealingPreview result;
	if(!getBattle() || !healer || !healer->isFirstAidTent() || !healer->alive()
		|| !target || !target->alive() || !target->isValidTarget()
		|| (target->hasBonusOfType(BonusType::SIEGE_WEAPON) && !battleCanRepairWarMachine(healer, target))
		|| !battleMatchActionController(healer, target, true))
		return result;

	auto state = target->acquireState();
	auto healing = battleGetFirstAidCreatureHealingOutput(healer, target);
	result.survivorHealedHP = state->heal(healing, EHealLevel::HEAL,
		EHealPower::PERMANENT).healedHealthPoints;
	auto restoration = battleGetBattlefieldMedicRestorationBudget(healer, target);
	const auto restored = state->heal(restoration, EHealLevel::RESURRECT, EHealPower::PERMANENT);
	result.restoredHP = restored.healedHealthPoints;
	result.restoredCount = restored.resurrectedCount;
	return result;
}

bool CBattleInfoCallback::battleCanHealWithFirstAidTent(
	const battle::Unit * healer, const battle::Unit * target) const
{
	if(!healer || !healer->isFirstAidTent() || !target
		|| !battleMatchActionController(healer, target, true))
		return false;

	if(battleCanRepairWarMachine(healer, target))
		return true;

	// Preserve ordinary Tent admission; only Medic introduces casualty-only recipients.
	const auto * stack = dynamic_cast<const CStack *>(target);
	if(stack && stack->canBeHealed())
		return true;
	return battleGetBattlefieldMedicRestorationBudget(healer, target) > 0
		&& battleGetFirstAidHealingPreview(healer, target).restoredHP > 0;
}
int64_t CBattleInfoCallback::getFirstAidHealValue(const CGHeroInstance * owner, const battle::Unit * target) const
{
	RETURN_IF_NOT_BATTLE(0);
	if(!owner || !target)
		return 0;

	int64_t base = firstAidHealingBase(owner);
	if(const auto * activeHealer = battleActiveUnit(); activeHealer && activeHealer->isFirstAidTent()
		&& battleGetOwnerHero(activeHealer) == owner)
		base = battleGetFirstAidCreatureHealingOutput(activeHealer, target);

	if(base <= 0)
		return 0;

	auto state = target->acquireState();

	return state->heal(base, EHealLevel::HEAL, EHealPower::PERMANENT).healedHealthPoints;
}
