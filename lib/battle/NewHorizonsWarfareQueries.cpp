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
	return allowances.eligibleAllowance(HeroActionAllowanceState::ActionKind::ORDER, round,
		[](const HeroActionAllowanceState::Grant & grant)
		{ return grant.allowance == HeroActionAllowanceState::AllowanceKind::HERO
			&& grant.source == HeroActionAllowanceState::GrantSource::ROUND; });
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
