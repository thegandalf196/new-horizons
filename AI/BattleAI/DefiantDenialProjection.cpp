/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "DefiantDenialProjection.h"
#include "AttackPossibility.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/bonuses/BonusSelector.h"
#include <algorithm>
#include <array>
namespace battleDefiantAI
{
bool needsProjection(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	return !attack.shooting && attack.physicalDamage
			&& std::ranges::any_of(std::array{BattleSide::ATTACKER, BattleSide::DEFENDER}, [&battle](BattleSide side)
			{
				const auto * hero = battle.getBattle()->getSideHero(side);
				return hero && hero->hasActivePerk("new-horizons:armorer", "new-horizons:armorer.defiant")
					&& battle.getBattle()->getArmorerDefiantState(side).availableAt(battle.battleGetRound());
			});
}

std::pair<bool, bool> projectMeleeDenials(HypotheticBattle * battle, const BattleAttackInfo & attack,
	bool blocked, bool admitted, const CSelector & firstStrike, FortuneStrikeProjection & receipt)
{
	bool ignoredInnate = false;
	bool ignoredShroud = false;
	if(admitted && battle)
	{
		if(blocked)
			ignoredInnate = project(*battle, attack, newHorizonsArmorer::DefiantDenialCause::INNATE_BLOCK, receipt);
		if(!attack.defender->hasBonus(firstStrike) && battle->battleShroudDeniesRetaliation(attack))
			ignoredShroud = project(*battle, attack, newHorizonsArmorer::DefiantDenialCause::EXPERT_SHROUD, receipt);
	}
	return {ignoredInnate, ignoredShroud};
}

bool project(HypotheticBattle & battle, const BattleAttackInfo & attack,
	newHorizonsArmorer::DefiantDenialCause cause, FortuneStrikeProjection & strike)
{
	if(!battle.battleCanUseDefiant(attack, cause))
		return false;
	const auto side = battle.playerToSide(battle.battleGetActionController(attack.defender));
	const auto round = battle.battleGetRound();
	auto state = battle.getArmorerDefiantState(side);
	if(!state.consumeAt(round))
		return false;
	battle.setArmorerDefiantState(side, state);
	strike.defiantDenials.push_back({side, cause, attack.defender->unitId(), round,
		DefiantDenialProjection::Phase::AFTER_TARGET_HIT});
	return true;
}

bool project(HypotheticBattle & battle, const BattleAttackInfo & attack,
	newHorizonsArmorer::DefiantDenialCause cause)
{
	if(!battle.battleCanUseDefiant(attack, cause))
		return false;
	const auto side = battle.playerToSide(battle.battleGetActionController(attack.defender));
	auto state = battle.getArmorerDefiantState(side);
	if(!state.consumeAt(battle.battleGetRound()))
		return false;
	battle.setArmorerDefiantState(side, state);
	return true;
}

void replay(HypotheticBattle & battle, const DefiantDenialProjection & receipt)
{
	if(receipt.round != battle.battleGetRound()
		|| (receipt.side != BattleSide::ATTACKER && receipt.side != BattleSide::DEFENDER))
		throw std::runtime_error("Invalid projected Defiant consumption receipt");
	auto state = battle.getArmorerDefiantState(receipt.side);
	if(!state.consumeAt(receipt.round))
		throw std::runtime_error("Projected Defiant allowance was already consumed");
	battle.setArmorerDefiantState(receipt.side, state);
}

void applyNoQuarterOrDefiant(HypotheticBattle & battle, const BattleAttackInfo & attack,
	uint32_t target, int32_t moraleActivations, FortuneStrikeProjection & receipt)
{
	if(!project(battle, attack, newHorizonsArmorer::DefiantDenialCause::NO_QUARTER, receipt))
	{
		battle.getForUpdate(target)->applyNoQuarter(moraleActivations, true);
		receipt.noQuarterTargets.emplace_back(target, moraleActivations);
	}
}

std::pair<bool, bool> projectMeleeDenials(HypotheticBattle * battle, const BattleAttackInfo & attack,
	bool blocked, bool admitted, const CSelector & firstStrike)
{
	bool ignoredInnate = false;
	bool ignoredShroud = false;
	if(admitted && battle)
	{
		if(blocked)
			ignoredInnate = project(*battle, attack, newHorizonsArmorer::DefiantDenialCause::INNATE_BLOCK);
		if(!attack.defender->hasBonus(firstStrike) && battle->battleShroudDeniesRetaliation(attack))
			ignoredShroud = project(*battle, attack, newHorizonsArmorer::DefiantDenialCause::EXPERT_SHROUD);
	}
	return {ignoredInnate, ignoredShroud};
}

}
