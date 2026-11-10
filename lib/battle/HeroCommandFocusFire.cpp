/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CBattleInfoCallback.h"
#include "IBattleState.h"
#include "Unit.h"
#include "NewHorizonsDivineMandate.h"
#include "NewHorizonsCommandResources.h"
#include "../mapObjects/CGHeroInstance.h"

std::optional<FocusFireState> CBattleInfoCallback::battlePrepareFocusFireState(BattleSide side,
	uint32_t targetUnitId) const
{
	if(!battleCanConfirmHeroCommand(side, HeroCommand::FOCUS_FIRE, targetUnitId)) return {};
	const auto * hero = battleGetFightingHero(side);
	const auto & formula = getBattle()->getHeroCommandRules()["commands"]["focusFire"]["effects"]["rangedDamagePercent"];
	int efficiency = newHorizonsPassiveDivineMandate::orderEfficiencyPercent(
		newHorizonsPassiveDivineMandate::capturePolicy(hero));
	if(heroCommands::isCanonicalRules(getBattle()->getHeroCommandRules()))
	{
		const auto order = battlePrepareHeroOrderState(side, HeroCommand::FOCUS_FIRE, {targetUnitId});
		if(!order) return {};
		efficiency = order->divineMandateEfficiencyBonusPercent();
	}
	else if(const auto allowance = battleGetOrderActionAllowance(side);
		allowance && allowance->source == HeroActionAllowanceState::GrantSource::DIVINE_MANDATE)
		efficiency += newHorizonsDivineMandate::sacredCommandEfficiencyBonusPercent(hero)
			+ newHorizonsDivineMandate::knightlySequenceOrderBonusPercent(hero);
	FocusFireState result;
	result.targetUnitId = targetUnitId;
	result.issuedRound = battleGetRound();
	result.rangedDamagePercent = heroCommandCoefficient(formula, *hero, efficiency);
	const bool includeMelee = heroCommands::isCanonicalRules(getBattle()->getHeroCommandRules())
		&& heroCommands::hasCombinedArms(hero);
	for(const auto * unit : battleGetUnitsIf([this, side, includeMelee](const battle::Unit * unit)
	{
		return battleIsFocusFireRecipient(unit, side) || (includeMelee && unit && unit->alive()
			&& !unit->isGhost() && unit->isMeleeAttacker() && battleGetOwner(unit) == sideToPlayer(side)
			&& !unit->isTurret() && !unit->hasBonusOfType(BonusType::SIEGE_WEAPON)
			&& unit->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER);
	})) result.recipientUnitIds.push_back(unit->unitId());
	std::sort(result.recipientUnitIds.begin(), result.recipientUnitIds.end());
	result.validateShape();
	return result;
}
