/*
 * HeroCommandPreparation.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "CBattleInfoCallback.h"
#include "HeroCommandAdjacency.h"
#include "HeroCommandCoefficient.h"
#include "IBattleState.h"
#include "CUnitState.h"
#include "NewHorizonsArchery.h"
#include "NewHorizonsDivineMandate.h"
#include "NewHorizonsIronWill.h"
#include "NewHorizonsSwiftRebirth.h"
#include "NewHorizonsWarcasting.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../bonuses/BonusSelector.h"

std::optional<HeroOrderState> CBattleInfoCallback::battleFinalizeHeroOrderState(BattleSide side,
	HeroOrderState result) const
{
	using Reason = heroCommands::TargetRejection;
	const auto command = result.command;
	const auto * hero = battleGetFightingHero(side);
	const auto allowance = battleGetOrderActionAllowance(side);
	const auto * rules = &getBattle()->getHeroCommandRules()["commands"][heroCommands::key(command)];
	const auto ownCombatUnit = [this, side](const battle::Unit * unit)
	{
		return battleOwnOrderUnitRejection(side, unit) == Reason::NONE;
	};
	if(allowance && allowance->allowance == HeroActionAllowanceState::AllowanceKind::ORDER
		&& allowance->source == HeroActionAllowanceState::GrantSource::DIVINE_MANDATE
		&& (newHorizonsDivineMandate::hasRoyalStandardPerk(hero)
			|| newHorizonsDivineMandate::hasDivineDisciplinePerk(hero)
			|| newHorizonsDivineMandate::hasCrownAndAltarPerk(hero)))
	{
		for(const auto * unit : battleAliveUnits())
		{
			if(!ownCombatUnit(unit))
				continue;
			bool recipient = true;
			switch(command)
			{
			case HeroCommand::VANGUARD:
				recipient = result.revision && result.revision->contains(unit->unitId());
				break;
			case HeroCommand::FOCUS_FIRE:
				recipient = battleIsFocusFireRecipient(unit, side)
					|| (heroCommands::hasCombinedArms(hero) && unit->isMeleeAttacker()
						&& !unit->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));
				break;
			case HeroCommand::FLANK:
				recipient = unit->isMeleeAttacker() || (heroCommands::hasCombinedArms(hero)
					&& newHorizonsArchery::isOrdinaryPhysicalShooter(unit));
				break;
			case HeroCommand::PROTECT:
				recipient = unit->unitId() == result.primaryTargetUnitId
					|| unit->unitId() == result.secondaryTargetUnitId;
				break;
			case HeroCommand::SECOND_WIND:
				recipient = unit->unitId() == result.primaryTargetUnitId;
				break;
			default:
				break;
			}
			if(recipient)
				result.royalStandardRecipientUnitIds.push_back(unit->unitId());
		}
		std::sort(result.royalStandardRecipientUnitIds.begin(), result.royalStandardRecipientUnitIds.end());
		if(newHorizonsDivineMandate::hasCrownAndAltarPerk(hero))
		{
			const auto & ledger = getBattle()->getHeroActionAllowances(side);
			const auto grant = std::find_if(ledger.grants.begin(), ledger.grants.end(),
				[&allowance](const auto & entry) { return entry.id == allowance->grantId; });
			if(grant != ledger.grants.end())
				std::set_intersection(result.royalStandardRecipientUnitIds.begin(), result.royalStandardRecipientUnitIds.end(),
					grant->divineMandateRecipients.begin(), grant->divineMandateRecipients.end(),
					std::back_inserter(result.crownAndAltarRecipientUnitIds));
			if(!result.crownAndAltarRecipientUnitIds.empty())
			{
				if(command == HeroCommand::FOCUS_FIRE)
					result.crownAndAltarFocusFirePercent = heroCommands::coefficient(
						newHorizonsDivineMandate::crownOrderFormula((*rules)["effects"]["rangedDamagePercent"], true),
						*hero, result.warcastingBonusPercent, result.divineMandateEfficiencyBonusPercent());
				if(command == HeroCommand::HOLD_THE_LINE && result.holdMagicalReductionBasisPoints != 0)
					result.crownAndAltarHoldReductionBasisPoints = static_cast<uint16_t>(std::clamp(
						heroCommands::coefficient(newHorizonsDivineMandate::crownOrderFormula(
							(*rules)["effects"]["damageReductionPercent"], true), *hero,
							result.warcastingBonusPercent, result.divineMandateEfficiencyBonusPercent()), 0, 100)
						* newHorizonsIronDiscipline::BASIS_POINTS_PER_PHYSICAL_PERCENT);
			}
		}
		if(result.revision && !result.crownAndAltarRecipientUnitIds.empty())
			result.revision->crown = battlePrepareRevisedOrderSnapshot(side, result, true).value().crown;
		if(command != HeroCommand::SECOND_WIND && command != HeroCommand::VANGUARD
			&& newHorizonsDivineMandate::hasDivineDisciplinePerk(hero))
			result.divineDisciplineRecipientUnitIds = result.royalStandardRecipientUnitIds;
		if(!newHorizonsDivineMandate::hasRoyalStandardPerk(hero))
			result.royalStandardRecipientUnitIds.clear();
	}
	const auto ironRecipients = newHorizonsIronWill::recipients(*this, side, result);
	if(!ironRecipients.empty())
	{
		result.ironWillLifetime = true;
		result.divineDisciplineRecipientUnitIds = ironRecipients;
	}
	result.validateShape();
	return result;
}

std::optional<heroCommands::RevisedOrderSnapshot> CBattleInfoCallback::battlePrepareRevisedOrderSnapshot(
	BattleSide side, const HeroOrderState & order, bool crown) const
{
	const auto command = order.command;
	const auto issuedRound = order.issuedRound;
	const auto * hero = battleGetFightingHero(side);
	const auto & originalRules = getBattle()->getHeroCommandRules()["commands"][heroCommands::key(command)];
	const auto rules = crown ? heroCommands::crownRevisedOrderDefinition(originalRules, command) : originalRules;
	const auto ownCombatUnit = [this, side](const battle::Unit * unit)
	{
		return battleOwnOrderUnitRejection(side, unit) == heroCommands::TargetRejection::NONE;
	};
	int64_t leadership = 0;
	if(const auto capacity = hero->getLeadershipCapacity())
		leadership = capacity->capacity;
	const auto ratings = heroCommands::CoefficientInputs::projected(*hero,
		visibleHeroBonuses(side, Selector::type()(BonusType::PRIMARY_SKILL), false));
	auto revision = heroCommands::scaledRevisedOrderSnapshot(command, issuedRound,
		ratings.primary(PrimarySkill::ATTACK), ratings.primary(PrimarySkill::DEFENSE),
		leadership, rules,
		heroCommands::attributeEfficiencyPercent(*hero, PrimarySkill::ATTACK, order.warcastingBonusPercent,
			order.divineMandateEfficiencyBonusPercent(), true),
		heroCommands::attributeEfficiencyPercent(*hero, PrimarySkill::DEFENSE, order.warcastingBonusPercent,
			order.divineMandateEfficiencyBonusPercent(), true),
		heroCommands::leadershipEfficiencyPercent(*hero, order.warcastingBonusPercent,
			order.divineMandateEfficiencyBonusPercent()));
	if(crown)
		revision.crown = heroCommands::CrownRevisedOrderPotency{
			revision.feintReductionPercent, revision.vanguardStatBonus, revision.vanguardInitiativePercent};
	revision.feintCompanionPercent = command == HeroCommand::FEINT
		? heroCommands::revisedPerkBonus(getBattle()->getHeroCommandRules(), hero, command) : 0;
	bool hasRecipient = false;
	for(const auto * unit : battleAliveUnits())
	{
		if(!ownCombatUnit(unit) || (command == HeroCommand::VANGUARD && !heroCommands::isVanguardCreature(unit)))
			continue;
		hasRecipient = true;
		if(command == HeroCommand::VANGUARD)
			revision.recipientUnitIds.push_back(unit->unitId());
	}
	if(!hasRecipient)
		return {};
	std::sort(revision.recipientUnitIds.begin(), revision.recipientUnitIds.end());
	return revision;
}

void CBattleInfoCallback::battleCaptureHeroOrderModifiers(BattleSide side, HeroOrderState & result) const
{
	const auto command = result.command;
	const auto * hero = battleGetFightingHero(side);
	const auto * rules = &getBattle()->getHeroCommandRules()["commands"][heroCommands::key(command)];
	const auto allowance = battleGetOrderActionAllowance(side);
	if(newHorizonsWarcasting::enabled(getBattle()->getMagicRules())
		&& allowance && allowance->allowance == HeroActionAllowanceState::AllowanceKind::HERO)
		result.warcastingBonusPercent = newHorizonsWarcasting::orderBonus(
			hero, getBattle()->getWarcastingState(side), result.issuedRound);
	if(allowance && allowance->allowance == HeroActionAllowanceState::AllowanceKind::ORDER
		&& allowance->source == HeroActionAllowanceState::GrantSource::DIVINE_MANDATE)
	{
		result.sacredCommandEfficiencyBonusPercent =
			newHorizonsDivineMandate::sacredCommandEfficiencyBonusPercent(hero);
		result.knightlySequenceEfficiencyBonusPercent =
			newHorizonsDivineMandate::knightlySequenceOrderBonusPercent(hero);
	}
	if(command == HeroCommand::HOLD_THE_LINE
		&& hero->hasActivePerk(newHorizonsIronDiscipline::SKILL, newHorizonsIronDiscipline::PERK))
	{
		const int physicalReduction = std::clamp(heroCommands::coefficient(
			(*rules)["effects"]["damageReductionPercent"], *hero, result.warcastingBonusPercent,
			result.divineMandateEfficiencyBonusPercent()), 0, 100);
		result.holdMagicalReductionBasisPoints = static_cast<uint16_t>(physicalReduction
			* newHorizonsIronDiscipline::BASIS_POINTS_PER_PHYSICAL_PERCENT);
	}

}

std::optional<HeroOrderState> CBattleInfoCallback::battlePrepareHeroOrderStateImpl(BattleSide side,
	HeroCommand command, const std::vector<uint32_t> & targetUnitIds, heroCommands::TargetRejection & rejection) const
{
	using Reason = heroCommands::TargetRejection;
	rejection = Reason::NONE;
	const auto reject = [&rejection](Reason reason) -> std::optional<HeroOrderState>
	{
		rejection = reason;
		return {};
	};
	if(!getBattle() || !heroCommands::isCanonicalRules(getBattle()->getHeroCommandRules())
		|| !heroCommands::isActive(command) || !battleHeroCommandCommonAvailable(side, command))
		return reject(Reason::UNAVAILABLE);
	const auto * hero = battleGetFightingHero(side);
	if(!hero)
		return reject(Reason::UNAVAILABLE);
	const auto owner = sideToPlayer(side);
	const auto ownCombatUnit = [this, side](const battle::Unit * unit)
	{
		return battleOwnOrderUnitRejection(side, unit) == Reason::NONE;
	};
	HeroOrderState result;
	result.command = command;
	result.issuedRound = battleGetRound();
	if(result.issuedRound < 1)
		return reject(Reason::UNAVAILABLE);
	battleCaptureHeroOrderModifiers(side, result);

	if(command == HeroCommand::FEINT || command == HeroCommand::VANGUARD)
	{
		if(!targetUnitIds.empty())
			return reject(Reason::TARGET_COUNT);
		result.revision = battlePrepareRevisedOrderSnapshot(side, result);
		if(!result.revision)
			return reject(Reason::NO_RECIPIENT);
		return battleFinalizeHeroOrderState(side, result);
	}
	if(command == HeroCommand::FOCUS_FIRE)
	{
		if(targetUnitIds.size() != 1)
			return reject(Reason::TARGET_COUNT);
		const auto reason = battleFocusFireTargetRejection(side, command, targetUnitIds.front());
		if(reason != Reason::NONE)
			return reject(reason);
		result.primaryTargetUnitId = targetUnitIds.front();
		return battleFinalizeHeroOrderState(side, result);
	}
	if(command == HeroCommand::FLANK)
	{
		if(targetUnitIds.size() != 1)
			return reject(Reason::TARGET_COUNT);
		const auto * target = battleGetUnitByID(targetUnitIds.front());
		if(!target)
			return reject(Reason::NO_STACK);
		if(!target->alive() || target->isGhost())
			return reject(Reason::NOT_LIVING);
		if(target->isTurret())
			return reject(Reason::ORDINARY_REQUIRED);
		if(battleGetOwner(target) == owner)
			return reject(Reason::ENEMY_REQUIRED);
		bool hasMelee = false;
		for(const auto * unit : battleAliveUnits())
			if(ownCombatUnit(unit) && unit->isMeleeAttacker())
				hasMelee = true;
		if(!hasMelee)
			return reject(Reason::NO_MELEE_RECIPIENT);
		result.primaryTargetUnitId = target->unitId();
		result.flankTargets.push_back({target->unitId(), 0});
		return battleFinalizeHeroOrderState(side, result);
	}
	if(command == HeroCommand::PROTECT)
	{
		if(targetUnitIds.size() != 2)
			return reject(Reason::TARGET_COUNT);
		if(targetUnitIds.front() == targetUnitIds.back())
			return reject(Reason::SAME_STACK);
		const auto * protector = battleGetUnitByID(targetUnitIds.front());
		const auto * ward = battleGetUnitByID(targetUnitIds.back());
		for(const auto * unit : {protector, ward})
		{
			const auto reason = battleOwnOrderUnitRejection(side, unit);
			if(reason != Reason::NONE)
				return reject(reason);
		}
		if(!heroCommands::orderUnitsAdjacent(protector, ward))
			return reject(Reason::NOT_ADJACENT);
		result.primaryTargetUnitId = protector->unitId();
		result.secondaryTargetUnitId = ward->unitId();
		result.protectInterceptionLimit = hero->hasActivePerk(
			newHorizonsShieldMaster::SKILL, newHorizonsShieldMaster::PERK)
			? newHorizonsShieldMaster::SHIELD_MASTER_PROTECT_INTERCEPTION_LIMIT
			: newHorizonsShieldMaster::ORDINARY_PROTECT_INTERCEPTION_LIMIT;
		return battleFinalizeHeroOrderState(side, result);
	}
	if(command == HeroCommand::SECOND_WIND)
	{
		if(targetUnitIds.size() != 1)
			return reject(Reason::TARGET_COUNT);
		const auto * target = battleGetUnitByID(targetUnitIds.front());
		const bool canonicalRules = heroCommands::isCanonicalRules(getBattle()->getHeroCommandRules());
		const auto targetState = target ? target->acquireState() : nullptr;
		const bool hasSpentActivation = target
			&& (target->moved() || (canonicalRules && targetState && targetState->defending));
		const auto reason = battleOwnOrderUnitRejection(side, target);
		if(reason != Reason::NONE)
			return reject(reason);
		if(newHorizonsSwiftRebirth::blocksAdditionalActivation(*target, battleGetRound()))
			return reject(Reason::ACTIVATION_UNSPENT);
		if(!hasSpentActivation)
			return reject(Reason::ACTIVATION_UNSPENT);
		result.primaryTargetUnitId = target->unitId();
		return battleFinalizeHeroOrderState(side, result);
	}
	if(!targetUnitIds.empty())
		return reject(Reason::TARGET_COUNT);
	bool hasRecipient = false;
	for(const auto * unit : battleAliveUnits())
	{
		if(!ownCombatUnit(unit))
			continue;
		hasRecipient = true;
		if(command == HeroCommand::HOLD_THE_LINE)
			result.anchors.push_back({unit->unitId(), unit->getPosition().toInt()});
	}
	if(!hasRecipient)
		return reject(Reason::NO_RECIPIENT);
	result.validateShape();
	return battleFinalizeHeroOrderState(side, result);
}
