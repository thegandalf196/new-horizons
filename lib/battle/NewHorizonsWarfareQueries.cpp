/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CBattleInfoCallback.h"
#include "NewHorizonsCommandResources.h"
#include "NewHorizonsWarfare.h"
#include "IBattleState.h"
#include "OrderFormation.h"
#include "HeroCommandCoefficient.h"
#include "../mapObjects/CGHeroInstance.h"

namespace
{
const HeroActionAllowanceState & allowanceLedger(const CBattleInfoEssentials & callback, BattleSide side)
{
	if(const auto * state = dynamic_cast<const IBattleState *>(&callback))
		return state->getHeroActionAllowances(side);
	if(const auto * state = dynamic_cast<const IBattleState *>(callback.getBattle()))
		return state->getHeroActionAllowances(side);
	static const HeroActionAllowanceState empty;
	return empty;
}
}

std::optional<HeroActionAllowanceState::Selection> CBattleInfoEssentials::battleGetOrderActionAllowance(
	BattleSide side) const
{
	if(!getBattle() || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| !newHorizonsCommand::usesRules(getBattle()->getHeroCommandRules())) return {};
	const auto round = battleGetRound();
	const auto & allowances = allowanceLedger(*this, side);
	if(round < 0 || allowances.currentRound != round) return {};
	if(newHorizonsCommand::usesFiniteResources(getBattle()->getHeroCommandRules()))
	{
		const auto & resources = getBattle()->getCommandResources(side);
		if(!resources.initialized || resources.remaining == 0) return {};
	}
	using Ledger = HeroActionAllowanceState;
	return allowances.eligibleAllowance(Ledger::ActionKind::ORDER, round,
		[this, side](const Ledger::Grant & grant)
		{
			if(grant.allowance == Ledger::AllowanceKind::HERO)
				return grant.source == Ledger::GrantSource::ROUND;
			if(grant.allowance != Ledger::AllowanceKind::ORDER || grant.source != Ledger::GrantSource::DIVINE_MANDATE)
				return false;
			// Captured paired Mandate may spend only its own active follow-up.
			const auto mandate = battleGetDivineMandateStatus(side);
			return mandate.pendingFollowup && mandate.pendingFollowup->id == grant.id;
		});
}

int CBattleInfoCallback::battleWarfareStatBonus(const battle::Unit * unit) const
{
	return newHorizonsWarfare::statBonus(*this, unit);
}

int CBattleInfoCallback::battleSecondWindPercent(BattleSide side, const HeroOrderState & order,
	uint32_t recipient, bool crown) const
{
	const auto * hero = battleGetFightingHero(side);
	if(!hero) throw std::runtime_error("Second Wind requires a visible issuing hero");
	const auto & rules = getBattle()->getHeroCommandRules();
	if(!newHorizonsWarfare::usesRules(rules))
		return heroCommands::secondWindPercent(*hero, order.divineMandateEfficiencyBonusPercent(),
			crown && order.crownAndAltarAppliesTo(recipient));
	const auto inputs = heroCommands::CoefficientInputs::projected(*hero,
		visibleHeroBonuses(side, Selector::type()(BonusType::PRIMARY_SKILL), false));
	return orderFormation::secondWindPercent(rules, inputs.primary(PrimarySkill::ATTACK), inputs.primary(PrimarySkill::DEFENSE),
		orderFormation::command(*hero), heroCommands::leadershipEfficiencyPercent(*hero, order.divineMandateEfficiencyBonusPercent()),
		crown && order.crownAndAltarAppliesTo(recipient));
}
