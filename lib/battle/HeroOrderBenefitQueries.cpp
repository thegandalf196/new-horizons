/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CBattleInfoCallback.h"
#include "IBattleState.h"
#include "NewHorizonsCommandResources.h"
#include "NewHorizonsWarfare.h"
#include "NewHorizonsArchery.h"
#include "HeroCommandAdjacency.h"
#include "HeroCommandRoyalStandard.h"
#include "Unit.h"
#include "../mapObjects/CGHeroInstance.h"

bool CBattleInfoCallback::battleOrderBenefitAppliesTo(const HeroOrderState & state, BattleSide side,
	const battle::Unit * unit) const
{
	if(!getBattle() || !unit || !unit->alive() || unit->isGhost()
		|| (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| !heroCommands::isCanonicalRules(getBattle()->getHeroCommandRules())
		|| !heroCommands::supportedByRules(getBattle()->getHeroCommandRules(), state.command)
		|| (state.command != HeroCommand::VANGUARD && !state.scheduledFor(unit->unitId(), battleGetRound()))
		|| battleGetOwner(unit) != sideToPlayer(side)
		|| unit->isTurret() || unit->hasBonusOfType(BonusType::SIEGE_WEAPON)
		|| unit->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER)
		return false;

	switch(state.command)
	{
	case HeroCommand::CHARGE:
		return !state.containsConsumed(unit->unitId());
	case HeroCommand::FOCUS_FIRE:
	{
		const auto mark = battleGetFocusFireState(side);
		const auto * hero = battleGetFightingHero(side);
		const bool eligibleFocusAttacker = battleIsFocusFireRecipient(unit, side)
			|| (heroCommands::hasCombinedArms(hero) && unit->isMeleeAttacker()
				&& !unit->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));
		return eligibleFocusAttacker && battleIsFocusFireTargetActive(side) && mark
			&& mark->issuedRound == state.issuedRound
			&& mark->targetUnitId == state.primaryTargetUnitId
			&& std::binary_search(mark->recipientUnitIds.begin(), mark->recipientUnitIds.end(), unit->unitId());
	}
	case HeroCommand::VANGUARD:
		return heroCommands::activeVanguardBonuses(unit).has_value();
	case HeroCommand::FEINT:
	case HeroCommand::RIPOSTE:
	case HeroCommand::BRACE:
		return true;
	case HeroCommand::HOLD_THE_LINE:
		return battleIsHoldTheLineRecipient(state, unit);
	case HeroCommand::PROTECT:
	{
		if(state.protectBroken || state.protectInterceptionsConsumed >= state.protectInterceptionLimit)
			return false;
		const auto * protector = battleGetUnitByID(state.primaryTargetUnitId);
		const auto * ward = battleGetUnitByID(state.secondaryTargetUnitId);
		if(!protector || !ward || !protector->alive() || !ward->alive()
			|| protector->isGhost() || ward->isGhost()
			|| battleGetOwner(protector) != sideToPlayer(side)
			|| battleGetOwner(ward) != sideToPlayer(side)
			|| !heroCommands::orderUnitsAdjacent(protector, ward))
			return false;
		return unit->unitId() == protector->unitId() || unit->unitId() == ward->unitId();
	}
	case HeroCommand::FLANK:
	{
		const auto * target = battleGetUnitByID(state.primaryTargetUnitId);
		if(!target || !target->alive() || target->isGhost() || target->isTurret()
			|| battleGetOwner(target) == sideToPlayer(side) || !state.flankFor(target->unitId()))
			return false;
		if(unit->isMeleeAttacker())
			return true;
		const auto * hero = battleGetFightingHero(side);
		return heroCommands::hasCombinedArms(hero) && newHorizonsArchery::isOrdinaryPhysicalShooter(unit);
	}
	case HeroCommand::SECOND_WIND:
		return state.secondWindActive && state.primaryTargetUnitId == unit->unitId();
	default:
		return false;
	}
}

bool CBattleInfoCallback::battleHasCommandingPresence(const battle::Unit * unit) const
{
	if(!getBattle() || !unit || !unit->alive() || unit->isGhost()
		|| !heroCommands::isCanonicalRules(getBattle()->getHeroCommandRules()))
		return false;
	const auto side = playerToSide(battleGetOwner(unit));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return false;
	const auto * controllerHero = battleGetOwnerHero(unit);
	if(!controllerHero || !controllerHero->hasActivePerk(
		"new-horizons:command", "new-horizons:command.commandingPresence"))
		return false;
	return newHorizonsCommand::beneficiary(*this, unit);
}

namespace newHorizonsCommand
{
bool beneficiary(const CBattleInfoCallback & battle, const battle::Unit * unit)
{
	if(!battle.getBattle() || !unit || !unit->alive() || unit->isGhost()
		|| !heroCommands::isCanonicalRules(battle.getBattle()->getHeroCommandRules())
		|| unit->isTurret() || unit->hasBonusOfType(BonusType::SIEGE_WEAPON)
		|| unit->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER) return false;
	const auto side = battle.playerToSide(battle.battleGetOwner(unit));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER) return false;
	if(heroCommands::activeVanguardBonuses(unit)) return true;
	const auto orders = battle.battleGetHeroOrderStates(side);
	return std::ranges::any_of(orders, [&battle, side, unit](const HeroOrderState & order)
		{ return battle.battleOrderBenefitAppliesTo(order, side, unit); });
}
int64_t mitigateIncomingDamage(const CBattleInfoCallback & battle, const battle::Unit * unit, int64_t ordinary)
{
	if(newHorizonsWarfare::enabled(unit ? battle.battleGetOwnerHero(unit) : nullptr))
		return newHorizonsWarfare::reduceDamage(ordinary, newHorizonsWarfare::incomingReduction(battle, unit));
	if(ordinary <= 0 || !unbrokenRanks(battle, unit)) return ordinary;
	return ordinary / 5 * 4 + ordinary % 5 * 4 / 5;
}
bool unbrokenRanks(const CBattleInfoCallback & battle, const battle::Unit * unit)
{
	const auto * hero = unit ? battle.battleGetOwnerHero(unit) : nullptr;
	if(newHorizonsWarfare::enabled(hero)) return newHorizonsWarfare::incomingReduction(battle, unit) > 0;
	return usesPerkRules(hero) && hero->hasActivePerk("new-horizons:command", "new-horizons:command.crisisCommand") && beneficiary(battle, unit);
}
}
